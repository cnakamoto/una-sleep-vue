#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"

#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp"

#include "Service.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "SDK/UnaLogger/Logger.h"

namespace
{

// Poll interval for the message loop; the periodic alive check runs once
// per pass, so this also bounds marker jitter.
constexpr uint32_t kPollMs = 1000;

// Probe rates: HR at the 0.1 Hz the architecture sketch assumes for
// overnight tracking; battery only to measure drain.
constexpr float kHrPeriodSec   = 10.0f;
constexpr float kBattPeriodSec = 300.0f;

constexpr uint32_t kAliveEveryMs = 60000;

constexpr const char* kProbeFile = "probe.csv";

// One night is ~80 KB; rotate at boot if a previous run left much more.
constexpr size_t kRotateBytes = 200 * 1024;

} // namespace

Service::Service(SDK::Kernel& kernel)
    : mKernel(SDK::KernelProviderService::GetInstance().getKernel())
    , mGUIStarted(false)
    , mSensorHr(SDK::Sensor::Type::HEART_RATE, kHrPeriodSec)
    , mSensorBattery(SDK::Sensor::Type::BATTERY_LEVEL, kBattPeriodSec)
    , mBattD(CustomMessage::kBatteryUnknown)
    , mLastAliveMs(0)
{}

Service::~Service()
{
    if (mSensorHr.isConnected()) {
        mSensorHr.disconnect();
    }
    if (mSensorBattery.isConnected()) {
        mSensorBattery.disconnect();
    }
}

void Service::run()
{
    LOG_INFO("thread started\n");

    writeBootMarker();

    // The whole point of the probe: collect with the GUI never started.
    mSensorHr.connect();
    mSensorBattery.connect();

    while (true) {
        SDK::MessageBase *msg;
        if (mKernel.comm.getMessage(msg, kPollMs)) {
            switch (msg->getType()) {
                case SDK::MessageType::COMMAND_APP_STOP: {
                    LOG_INFO("Force exit from the application\n");
                    char line[48];
                    snprintf(line, sizeof(line), "X,%lu,%lu\n",
                             static_cast<unsigned long>(time(nullptr)),
                             static_cast<unsigned long>(mKernel.sys.getTimeMs()));
                    appendLine(line);
                    // We must release message because this is the last event.
                    mKernel.comm.releaseMessage(msg);
                    return;
                }

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN: {
                    LOG_INFO("GUI is now running\n");
                    // G lines discriminate "started at boot" (B, no G) from
                    // "started because the user opened the app" (B then G).
                    char line[48];
                    snprintf(line, sizeof(line), "G,%lu,%lu\n",
                             static_cast<unsigned long>(time(nullptr)),
                             static_cast<unsigned long>(mKernel.sys.getTimeMs()));
                    appendLine(line);
                    onStartGUI();
                    } break;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                    LOG_INFO("GUI has stopped\n");
                    onStopGUI();
                    break;

                case SDK::MessageType::EVENT_SENSOR_LAYER_DATA: {
                    auto* event = static_cast<SDK::Message::Sensor::EventData*>(msg);
                    SDK::Sensor::DataBatch batch(event->data, event->count, event->stride);
                    handleSensorData(event->handle, batch);
                    } break;

                default:
                    break;
            }

            mKernel.comm.releaseMessage(msg);
        }

        writeAliveMarkerIfDue();
    }
}

void Service::onStartGUI()
{
    mGUIStarted = true;
    SDK::send_msg<CustomMessage::ProbeStats>(mKernel, analyzeLog());
}

void Service::onStopGUI()
{
    mGUIStarted = false;
    // Sensors stay connected: the probe must keep logging overnight.
}

void Service::handleSensorData(uint16_t handle, SDK::Sensor::DataBatch& data)
{
    if (mSensorHr.matchesDriver(handle)) {
        for (uint16_t i = 0; i < data.size(); ++i) {
            SDK::SensorDataParser::HeartRate p(data[i]);
            if (p.isDataValid()) {
                char line[48];
                snprintf(line, sizeof(line), "H,%lu,%u,%d\n",
                         static_cast<unsigned long>(time(nullptr)),
                         static_cast<unsigned>(p.getBpm() + 0.5f),
                         static_cast<int>(p.getTrustLevel() + 0.5f));
                appendLine(line);
            }
        }
        return;
    }

    if (mSensorBattery.matchesDriver(handle)) {
        SDK::SensorDataParser::BatteryLevel p(data[0]);
        if (p.isDataValid()) {
            mBattD = static_cast<int16_t>(p.getCharge() * 10.0f + 0.5f);
        }
    }
}

void Service::appendLine(const char* line)
{
    auto file = mKernel.fs.file(kProbeFile);
    if (!file) {
        LOG_INFO("probe: fs.file failed\n");
        return;
    }
    if (!file->open(true, false)) {
        LOG_INFO("probe: open failed\n");
        return;
    }
    file->seek(file->size());
    size_t written = 0;
    file->write(line, strlen(line), written);
    file->flush();
    file->close();
}

void Service::writeBootMarker()
{
    // Start a fresh file when a previous run (or many) left it bloated;
    // the rotation itself is information, so note it in the new file.
    bool rotated = false;
    {
        auto file = mKernel.fs.file(kProbeFile);
        if (file && file->exist() && file->size() > kRotateBytes) {
            mKernel.fs.remove(kProbeFile);
            rotated = true;
        }
    }

    char line[64];
    snprintf(line, sizeof(line), "B,%lu,%lu%s\n",
             static_cast<unsigned long>(time(nullptr)),
             static_cast<unsigned long>(mKernel.sys.getTimeMs()),
             rotated ? ",rotated" : "");
    appendLine(line);
}

void Service::writeAliveMarkerIfDue()
{
    uint32_t now = mKernel.sys.getTimeMs();
    if (now - mLastAliveMs < kAliveEveryMs) {
        return;
    }
    mLastAliveMs = now;

    char line[48];
    snprintf(line, sizeof(line), "A,%lu,%lu,%d\n",
             static_cast<unsigned long>(time(nullptr)),
             static_cast<unsigned long>(now),
             static_cast<int>(mBattD));
    appendLine(line);
}

CustomMessage::ProbeStatsData Service::analyzeLog() const
{
    CustomMessage::ProbeStatsData s{};
    s.battFirstD = CustomMessage::kBatteryUnknown;
    s.battLastD  = CustomMessage::kBatteryUnknown;

    auto file = mKernel.fs.file(kProbeFile);
    if (!file || !file->open(false)) {
        return s;
    }

    uint32_t prevEpoch = 0;
    bool     seenAny   = false;

    char    chunk[256];
    char    line[72];
    size_t  lineLen = 0;
    size_t  bytesRead = 0;

    auto handleLine = [&](const char* l) {
        // Every line type carries epoch in field 1 — use it for span/gap.
        char type = l[0];
        unsigned long epoch = 0;
        if (sscanf(l + 1, ",%lu", &epoch) != 1) {
            return;
        }

        if (!seenAny) {
            s.firstEpoch = static_cast<uint32_t>(epoch);
            seenAny = true;
        } else if (epoch > prevEpoch) {
            uint32_t gap = static_cast<uint32_t>(epoch) - prevEpoch;
            if (gap > s.maxGapSec) {
                s.maxGapSec = gap;
            }
        }
        prevEpoch = static_cast<uint32_t>(epoch);
        s.lastEpoch = prevEpoch;

        switch (type) {
            case 'B': s.boots++; break;
            case 'X': s.stops++; break;
            case 'H': s.hrSamples++; break;
            case 'A': {
                s.aliveCount++;
                int battD = 0;
                unsigned long skip1 = 0, skip2 = 0;
                // A,<epoch>,<uptimeMs>,<battD> — battery is field 3.
                if (sscanf(l, "A,%lu,%lu,%d", &skip1, &skip2, &battD) == 3 && battD >= 0) {
                    if (s.battFirstD < 0) {
                        s.battFirstD = static_cast<int16_t>(battD);
                    }
                    s.battLastD = static_cast<int16_t>(battD);
                }
            } break;
            default: break;
        }
    };

    while (true) {
        if (!file->read(chunk, sizeof(chunk), bytesRead) || bytesRead == 0) {
            break;
        }
        for (size_t i = 0; i < bytesRead; ++i) {
            char c = chunk[i];
            if (c == '\n') {
                line[lineLen] = '\0';
                if (lineLen > 1) {
                    handleLine(line);
                }
                lineLen = 0;
            } else if (lineLen < sizeof(line) - 1) {
                line[lineLen++] = c;
            }
        }
    }
    if (lineLen > 1) {
        line[lineLen] = '\0';
        handleLine(line);
    }

    file->close();
    s.upMin = mKernel.sys.getTimeMs() / 60000;
    return s;
}

uint32_t Service::ParseVersion(const char* str)
{
    if (str == nullptr) {
        return 0;
    }

    typedef union {
        struct {
            uint8_t patch;
            uint8_t minor;
            uint8_t major;
        };
        uint32_t u32;
    } FirmwareVersion_t;

    FirmwareVersion_t v{};

    int major, minor, patch;

    if (sscanf(str, "%d.%d.%d", &major, &minor, &patch) == 3) {
        v.major = static_cast<uint8_t>(major);
        v.minor = static_cast<uint8_t>(minor);
        v.patch = static_cast<uint8_t>(patch);
        return v.u32;
    }

    return 0;
}
