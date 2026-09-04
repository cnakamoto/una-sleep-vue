#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"

#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMotionDetect.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserTouch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp"

#include "Service.hpp"
#include "SleepFitWriter.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "SDK/UnaLogger/Logger.h"

namespace
{

// Poll interval for the message loop; epoch boundaries and the periodic
// GUI state push are driven off this cadence.
constexpr uint32_t kPollMs = 1000;

// Push SESSION_STATE to an open GUI every N polls while TRACKING.
constexpr uint32_t kStatePushEveryPolls = 5;

} // namespace

Service::Service(SDK::Kernel& kernel)
    : mKernel(SDK::KernelProviderService::GetInstance().getKernel())
    , mGUIStarted(false)
    , mState(CustomMessage::TrackingState::IDLE)
    , mSensorHr(SDK::Sensor::Type::HEART_RATE, 10.0f)
    , mSensorMotion(SDK::Sensor::Type::MOTION_DETECT, 0.0f)
    , mSensorTouch(SDK::Sensor::Type::TOUCH_DETECT, 0.0f)
    , mSensorBattery(SDK::Sensor::Type::BATTERY_LEVEL, 300.0f)
    , mBedEpoch(0)
    , mSessionStartMs(0)
    , mNextEpochCloseMs(0)
    , mEpochMovement(0)
    , mEpochHrSum(0)
    , mEpochHrCount(0)
    , mBaselineCount(0)
    , mBaselineBpm(0)
    , mBaselineWindowOpen(true)
    , mMoveWindow{}
    , mMoveWindowIdx(0)
    , mMoveWindowCount(0)
    , mMoveWindowSum(0)
    , mWakeWindow{}
    , mWakeWindowIdx(0)
    , mWakeWindowCount(0)
    , mWakeActiveSum(0)
    , mQuietEpochs(0)
    , mOnsetRing{}
    , mOnsetRingIdx(0)
    , mOnsetRingCount(0)
    , mWornNow(false)
    , mEpochBufCount(0)
    , mFlushedEpochs(0)
    , mAwakeEpochs(0)
    , mLightEpochs(0)
    , mDeepEpochs(0)
    , mHrSum(0)
    , mHrValidCount(0)
    , mHrMin(0)
    , mHrMax(0)
    , mLiveHr(0)
    , mUnwornSinceMs(0)
    , mBatteryPct(0xFF)
    , mCloseFlags(0)
    , mHasSummary(false)
{}

Service::~Service()
{
    if (mSensorHr.isConnected()) {
        mSensorHr.disconnect();
    }
    if (mSensorMotion.isConnected()) {
        mSensorMotion.disconnect();
    }
    if (mSensorTouch.isConnected()) {
        mSensorTouch.disconnect();
    }
    if (mSensorBattery.isConnected()) {
        mSensorBattery.disconnect();
    }
}

void Service::run()
{
    LOG_INFO("thread started\n");

    recoverInterruptedSession();
    mHasSummary = mKernel.fs.exist(Sleep::kLastFile);

    // IDLE arming: motion + touch run in both states (auto-start).
    // HR + battery join only while TRACKING.
    mNextEpochCloseMs = mKernel.sys.getTimeMs() + Sleep::Config::kEpochSec * 1000;
    mSensorMotion.connect();
    mSensorTouch.connect();

    uint32_t statePushCountdown = 0;

    while (true) {
        SDK::MessageBase *msg;
        if (mKernel.comm.getMessage(msg, kPollMs)) {
            switch (msg->getType()) {
                case SDK::MessageType::COMMAND_APP_STOP:
                    LOG_INFO("Force exit from the application\n");
                    if (mState == CustomMessage::TrackingState::TRACKING) {
                        // Power-off / USB mass-storage: bank the night.
                        flushEpochs();
                        mCloseFlags |= Sleep::Flags::kInterrupted;
                        finalizeSessionFile();
                    }
                    // We must release message because this is the last event.
                    mKernel.comm.releaseMessage(msg);
                    return;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                    LOG_INFO("GUI is now running\n");
                    onStartGUI();
                    break;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                    LOG_INFO("GUI has stopped\n");
                    onStopGUI();
                    break;

                case SDK::MessageType::EVENT_SENSOR_LAYER_DATA: {
                    auto* event = static_cast<SDK::Message::Sensor::EventData*>(msg);
                    SDK::Sensor::DataBatch batch(event->data, event->count, event->stride);
                    handleSensorData(event->handle, batch);
                    } break;

                case CustomMessage::TRACKING_TOGGLE:
                    if (mState == CustomMessage::TrackingState::TRACKING) {
                        mCloseFlags = 0;
                        stopTracking();
                    } else {
                        startTracking(0);
                    }
                    sendSessionState();
                    break;

                case CustomMessage::SUMMARY_REQUEST:
                    sendSessionState();
                    sendSummary();
                    break;

                case CustomMessage::HISTORY_REQUEST:
                    sendHistory();
                    break;

                default:
                    break;
            }

            mKernel.comm.releaseMessage(msg);
        }

        closeEpochsIfDue(); // epoch boundaries in both states

        if (mState == CustomMessage::TrackingState::TRACKING
                && mGUIStarted && ++statePushCountdown >= kStatePushEveryPolls) {
            statePushCountdown = 0;
            sendSessionState();
        }
    }
}

void Service::onStartGUI()
{
    mGUIStarted = true;
    sendSessionState();
    sendSummary();
    sendTimeline();
}

void Service::onStopGUI()
{
    mGUIStarted = false;
    // Tracking continues with the GUI closed — that's the point.
}

// ---------------------------------------------------------------- sensors

void Service::handleSensorData(uint16_t handle, SDK::Sensor::DataBatch& data)
{
    // Motion + touch are needed in IDLE too (auto-start arming).
    if (mSensorMotion.matchesDriver(handle)) {
        for (uint16_t i = 0; i < data.size(); ++i) {
            SDK::SensorDataParser::MotionDetect p(data[i]);
            if (p.isDataValid()) {
                auto id = p.getID();
                if (id == SDK::SensorDataParser::MotionDetect::Motion::MOTION
                        || id == SDK::SensorDataParser::MotionDetect::Motion::SIG_MOTION) {
                    mEpochMovement++;
                }
            }
        }
        return;
    }

    if (mSensorTouch.matchesDriver(handle)) {
        SDK::SensorDataParser::Touch p(data[0]);
        if (p.isDataValid()) {
            mWornNow = p.isTouched();
            if (mState == CustomMessage::TrackingState::TRACKING) {
                if (mWornNow) {
                    mUnwornSinceMs = 0;
                } else if (mUnwornSinceMs == 0) {
                    mUnwornSinceMs = mKernel.sys.getTimeMs();
                }
            }
        }
        return;
    }

    if (mState != CustomMessage::TrackingState::TRACKING) {
        return;
    }

    if (mSensorHr.matchesDriver(handle)) {
        for (uint16_t i = 0; i < data.size(); ++i) {
            SDK::SensorDataParser::HeartRate p(data[i]);
            if (p.isDataValid()) {
                uint8_t bpm = static_cast<uint8_t>(p.getBpm() + 0.5f);
                if (bpm == 0) {
                    continue; // sensor ramp-up, not a real sample
                }
                mLiveHr = bpm;
                mEpochHrSum += bpm;
                mEpochHrCount++;
            }
        }
        return;
    }

    if (mSensorMotion.matchesDriver(handle)) {
        for (uint16_t i = 0; i < data.size(); ++i) {
            SDK::SensorDataParser::MotionDetect p(data[i]);
            if (p.isDataValid()) {
                auto id = p.getID();
                if (id == SDK::SensorDataParser::MotionDetect::Motion::MOTION
                        || id == SDK::SensorDataParser::MotionDetect::Motion::SIG_MOTION) {
                    mEpochMovement++;
                }
            }
        }
        return;
    }

    if (mSensorTouch.matchesDriver(handle)) {
        SDK::SensorDataParser::Touch p(data[0]);
        if (p.isDataValid()) {
            if (p.isTouched()) {
                mUnwornSinceMs = 0;
            } else if (mUnwornSinceMs == 0) {
                mUnwornSinceMs = mKernel.sys.getTimeMs();
            }
        }
        return;
    }

    if (mSensorBattery.matchesDriver(handle)) {
        SDK::SensorDataParser::BatteryLevel p(data[0]);
        if (p.isDataValid()) {
            mBatteryPct = static_cast<uint8_t>(p.getCharge() + 0.5f);
        }
    }
}

void Service::idleEpochClosed()
{
    // Motion history ring (60 min) — the onset rule's only input
    // besides worn-state and the wall clock.
    mOnsetRing[mOnsetRingIdx % Sleep::Config::kOnsetRingEpochs] = mEpochMovement;
    ++mOnsetRingIdx;
    if (mOnsetRingCount < Sleep::Config::kOnsetRingEpochs) {
        ++mOnsetRingCount;
    }
    mEpochMovement = 0;
    mEpochHrSum = 0;
    mEpochHrCount = 0;

    if (mOnsetRingCount < Sleep::Config::kOnsetWindowEpochs || !mWornNow) {
        return;
    }

    // Night arming window (wraps midnight, e.g. 20:00 -> 03:00).
    uint16_t tod = localMinutes(time(nullptr));
    bool armed = (Sleep::Config::kArmStartMin <= Sleep::Config::kArmEndMin)
        ? (tod >= Sleep::Config::kArmStartMin && tod < Sleep::Config::kArmEndMin)
        : (tod >= Sleep::Config::kArmStartMin || tod < Sleep::Config::kArmEndMin);
    if (!armed) {
        return;
    }

    uint16_t quiet = 0;
    for (uint16_t k = 0; k < Sleep::Config::kOnsetWindowEpochs; ++k) {
        uint16_t idx = (mOnsetRingIdx - 1 - k + Sleep::Config::kOnsetRingEpochs)
                       % Sleep::Config::kOnsetRingEpochs;
        if (mOnsetRing[idx] == 0) {
            ++quiet;
        }
    }
    if (quiet < Sleep::Config::kOnsetMinQuietEpochs) {
        return;
    }

    // Onset! Bed = just after the last significant motion (mv >= 2),
    // backdated at most kBackfillMaxEpochs.
    uint16_t back = 0;
    while (back < mOnsetRingCount && back < Sleep::Config::kBackfillMaxEpochs) {
        uint16_t idx = (mOnsetRingIdx - 1 - back + Sleep::Config::kOnsetRingEpochs)
                       % Sleep::Config::kOnsetRingEpochs;
        if (mOnsetRing[idx] >= 2) {
            break;
        }
        ++back;
    }
    LOG_INFO("auto-start: onset (quiet %u), backdating %u epochs\n", quiet, back);

    startTracking(back * Sleep::Config::kEpochSec);

    // Backfill the quiet stretch as LIGHT epochs through the normal
    // path, so the night file spans bed -> wake coherently.
    for (uint16_t e = 0; e < back; ++e) {
        mEpochMovement = 0;
        mEpochHrSum = 0;
        mEpochHrCount = 0;
        closeCurrentEpoch();
    }
}

// ------------------------------------------------------------- state flow

void Service::startTracking(uint32_t backdateSec)
{
    LOG_INFO("start tracking (backdate %lus)\n",
             static_cast<unsigned long>(backdateSec));

    mState = CustomMessage::TrackingState::TRACKING;
    mBedEpoch = time(nullptr) - backdateSec;
    mSessionStartMs = mKernel.sys.getTimeMs() - backdateSec * 1000;
    mNextEpochCloseMs = mKernel.sys.getTimeMs() + Sleep::Config::kEpochSec * 1000;

    mEpochMovement = 0;
    mEpochHrSum = 0;
    mEpochHrCount = 0;
    mBaselineCount = 0;
    mBaselineBpm = 0;
    mBaselineWindowOpen = true;
    memset(mMoveWindow, 0, sizeof(mMoveWindow));
    mMoveWindowIdx = 0;
    mMoveWindowCount = 0;
    mMoveWindowSum = 0;
    memset(mWakeWindow, 0, sizeof(mWakeWindow));
    mWakeWindowIdx = 0;
    mWakeWindowCount = 0;
    mWakeActiveSum = 0;
    mQuietEpochs = 0;
    mEpochBufCount = 0;
    mFlushedEpochs = 0;
    mAwakeEpochs = mLightEpochs = mDeepEpochs = 0;
    mHrSum = 0;
    mHrValidCount = 0;
    mHrMin = 0;
    mHrMax = 0;
    mLiveHr = 0;
    mUnwornSinceMs = 0;
    mCloseFlags = 0;

    // Placeholder header: real bedEpoch now, stats rewritten at close.
    // This is what makes a crashed/interrupted night recoverable.
    Sleep::SessionHeader hdr{};
    memcpy(hdr.magic, "SLP1", 4);
    hdr.dateKey  = localDateKey(mBedEpoch);
    hdr.bedEpoch = static_cast<uint32_t>(mBedEpoch);

    auto file = mKernel.fs.file(Sleep::kCurrentFile);
    if (file && file->open(true, true)) {
        size_t bw = 0;
        file->write(reinterpret_cast<const char*>(&hdr), sizeof(hdr), bw);
        file->flush();
        file->close();
    } else {
        LOG_INFO("failed to create %s\n", Sleep::kCurrentFile);
    }

    // Motion + touch stay connected across states (auto-start arming);
    // HR + battery are TRACKING-only.
    mSensorHr.connect();
    mSensorBattery.connect();
}

void Service::stopTracking()
{
    LOG_INFO("stop tracking (flags 0x%02x)\n", mCloseFlags);

    mSensorHr.disconnect();
    mSensorBattery.disconnect();

    flushEpochs();
    finalizeSessionFile();

    mState = CustomMessage::TrackingState::IDLE;
}

// ------------------------------------------------------- epoch processing

void Service::closeEpochsIfDue()
{
    uint32_t now = mKernel.sys.getTimeMs();

    while (static_cast<int32_t>(now - mNextEpochCloseMs) >= 0) {
        if (mState == CustomMessage::TrackingState::TRACKING) {
            closeCurrentEpoch();
        } else {
            idleEpochClosed();
        }
        mNextEpochCloseMs += Sleep::Config::kEpochSec * 1000;
    }

    if (mState != CustomMessage::TrackingState::TRACKING) {
        return;
    }

    // Safety aborts
    if (mUnwornSinceMs != 0 && now - mUnwornSinceMs >= Sleep::Config::kUnwornAbortSec * 1000) {
        LOG_INFO("aborting: unworn\n");
        mCloseFlags |= Sleep::Flags::kAbortedUnworn;
        stopTracking();
        return;
    }
    if (mBatteryPct > 0 && mBatteryPct <= Sleep::Config::kBatteryAbortPct) {
        LOG_INFO("aborting: battery %u%%\n", mBatteryPct);
        mCloseFlags |= Sleep::Flags::kAbortedBattery;
        stopTracking();
        return;
    }

    // Auto-wake: sustained motion in the trailing window, after the
    // settle-in grace, and only if real stillness was seen this session.
    if (mWakeWindowCount >= Sleep::Config::kWakeWindowEpochs
            && mQuietEpochs >= Sleep::Config::kWakeMinQuietEpochs
            && now - mSessionStartMs >= Sleep::Config::kWakeMinSessionMin * 60000
            && mWakeActiveSum >= Sleep::Config::kWakeMinActiveEpochs) {
        LOG_INFO("auto-wake: closing session\n");
        mCloseFlags |= Sleep::Flags::kAutoWake;
        stopTracking();
    }
}

void Service::closeCurrentEpoch()
{
    uint8_t hrMean = 0;
    if (mEpochHrCount > 0) {
        hrMean = static_cast<uint8_t>(mEpochHrSum / mEpochHrCount);
    }

    // Rolling movement window first, so the current epoch is inside its
    // own trailing-20-min window when classified.
    {
        uint8_t mv = mEpochMovement > 10 ? 10 : mEpochMovement;
        uint16_t idx = mMoveWindowIdx % Sleep::Config::kDeepWindowEpochs;
        if (mMoveWindowCount < Sleep::Config::kDeepWindowEpochs) {
            mMoveWindow[idx] = mv;
            mMoveWindowSum += mv;
            ++mMoveWindowCount;
        } else {
            mMoveWindowSum -= mMoveWindow[idx];
            mMoveWindow[idx] = mv;
            mMoveWindowSum += mv;
        }
        ++mMoveWindowIdx;
    }

    // Auto-wake window: per-epoch 0/1 "any motion", plus quiet tally.
    {
        uint8_t active = mEpochMovement > 0 ? 1 : 0;
        uint16_t idx = mWakeWindowIdx % Sleep::Config::kWakeWindowEpochs;
        if (mWakeWindowCount < Sleep::Config::kWakeWindowEpochs) {
            mWakeWindow[idx] = active;
            mWakeActiveSum += active;
            ++mWakeWindowCount;
        } else {
            mWakeActiveSum -= mWakeWindow[idx];
            mWakeWindow[idx] = active;
            mWakeActiveSum += active;
        }
        ++mWakeWindowIdx;
        if (mEpochMovement == 0) {
            ++mQuietEpochs;
        }
    }

    Sleep::Stage stage = classifyEpoch(mEpochMovement, hrMean);

    // Baseline: median of still-epoch HR means from the first
    // kBaselineWindowSec of the session, then frozen. A restless onset
    // (too few samples when the window would close) extends it.
    if (mBaselineWindowOpen) {
        uint32_t now = mKernel.sys.getTimeMs();
        if (now - mSessionStartMs >= Sleep::Config::kBaselineWindowSec * 1000
                && mBaselineCount >= Sleep::Config::kBaselineMinEpochs) {
            mBaselineWindowOpen = false; // freeze with what we have
        } else if (mEpochMovement == 0 && hrMean > 0
                   && mBaselineCount < Sleep::Config::kBaselineMaxSamples) {
            uint16_t i = mBaselineCount;
            while (i > 0 && mStillHr[i - 1] > hrMean) {
                mStillHr[i] = mStillHr[i - 1];
                --i;
            }
            mStillHr[i] = hrMean;
            ++mBaselineCount;
            if (mBaselineCount >= Sleep::Config::kBaselineMinEpochs) {
                mBaselineBpm = mStillHr[mBaselineCount / 2];
            }
        }
    }

    switch (stage) {
        case Sleep::Stage::AWAKE: ++mAwakeEpochs; break;
        case Sleep::Stage::LIGHT: ++mLightEpochs; break;
        case Sleep::Stage::DEEP:  ++mDeepEpochs;  break;
    }
    if (hrMean > 0) {
        mHrSum += hrMean;
        ++mHrValidCount;
        if (mHrMin == 0 || hrMean < mHrMin) mHrMin = hrMean;
        if (hrMean > mHrMax) mHrMax = hrMean;
    }

    if (mEpochBufCount < Sleep::Config::kFlushEveryEpochs) {
        mEpochBuf[mEpochBufCount].bits =
            Sleep::EpochRecord::pack(stage, mEpochMovement, hrMean, 0);
        ++mEpochBufCount;
    }
    if (mEpochBufCount >= Sleep::Config::kFlushEveryEpochs) {
        flushEpochs();
    }

    mEpochMovement = 0;
    mEpochHrSum = 0;
    mEpochHrCount = 0;
}

Sleep::Stage Service::classifyEpoch(uint8_t movement, uint8_t hrMean) const
{
    if (movement >= Sleep::Config::kAwakeMovementCount) {
        return Sleep::Stage::AWAKE;
    }
    if (mBaselineBpm > 0 && hrMean > 0
            && mMoveWindowCount >= Sleep::Config::kDeepWindowEpochs
            && mMoveWindowSum <= Sleep::Config::kDeepMaxWindowMove
            && static_cast<uint16_t>(hrMean) * 100
               <= static_cast<uint16_t>(mBaselineBpm) * (100 - Sleep::Config::kDeepHrDropPct)) {
        return Sleep::Stage::DEEP;
    }
    return Sleep::Stage::LIGHT;
}

// ---------------------------------------------------------------- storage

void Service::flushEpochs()
{
    if (mEpochBufCount == 0) {
        return;
    }

    auto file = mKernel.fs.file(Sleep::kCurrentFile);
    if (!file || !file->open(true, false)) {
        LOG_INFO("flush: open failed\n");
        return;
    }
    file->seek(sizeof(Sleep::SessionHeader)
               + mFlushedEpochs * sizeof(Sleep::EpochRecord));
    size_t bw = 0;
    file->write(reinterpret_cast<const char*>(mEpochBuf),
                mEpochBufCount * sizeof(Sleep::EpochRecord), bw);
    file->flush();
    file->close();

    mFlushedEpochs += mEpochBufCount;
    mEpochBufCount = 0;
}

void Service::finalizeSessionFile()
{
    uint16_t epochCount = mFlushedEpochs;

    // Recompute header totals from the records on flash — this same path
    // serves a normal close and boot recovery after interruption.
    Sleep::SessionHeader hdr{};
    {
        auto file = mKernel.fs.file(Sleep::kCurrentFile);
        if (!file || !file->open(false)) {
            LOG_INFO("finalize: open failed\n");
            return;
        }
        size_t br = 0;
        file->read(reinterpret_cast<char*>(&hdr), sizeof(hdr), br);

        uint16_t awake = 0, light = 0, deep = 0;
        uint32_t hrSum = 0, hrN = 0;
        uint8_t hrMin = 0, hrMax = 0;

        Sleep::EpochRecord rec{};
        for (uint16_t i = 0; i < epochCount; ++i) {
            size_t r = 0;
            if (!file->read(reinterpret_cast<char*>(&rec), sizeof(rec), r) || r != sizeof(rec)) {
                epochCount = i; // truncated file: close out what we have
                break;
            }
            switch (rec.stage()) {
                case Sleep::Stage::AWAKE: ++awake; break;
                case Sleep::Stage::LIGHT: ++light; break;
                case Sleep::Stage::DEEP:  ++deep;  break;
            }
            uint8_t bpm = rec.hrBpm();
            if (bpm > 0) {
                hrSum += bpm;
                ++hrN;
                if (hrMin == 0 || bpm < hrMin) hrMin = bpm;
                if (bpm > hrMax) hrMax = bpm;
            }
        }
        file->close();

        hdr.epochCount = epochCount;
        hdr.wakeEpoch  = hdr.bedEpoch + epochCount * Sleep::Config::kEpochSec;
        hdr.totalMin   = epochCount * Sleep::Config::kEpochSec / 60;
        hdr.awakeMin   = awake * Sleep::Config::kEpochSec / 60;
        hdr.lightMin   = light * Sleep::Config::kEpochSec / 60;
        hdr.deepMin    = deep * Sleep::Config::kEpochSec / 60;
        hdr.hrMin = hrMin;
        hdr.hrAvg = hrN > 0 ? static_cast<uint8_t>(hrSum / hrN) : 0;
        hdr.hrMax = hrMax;
        hdr.flags |= mCloseFlags;
    }

    if (epochCount == 0 || hdr.totalMin < Sleep::Config::kMinSaveMin) {
        // Too short to be a night: couch capture, bench test, or nap.
        // One real night per date is the app's model — discard quietly.
        LOG_INFO("discarding short session (%u min)\n", hdr.totalMin);
        mKernel.fs.remove(Sleep::kCurrentFile);
        return;
    }

    // Rewrite the finalized header at offset 0.
    {
        auto file = mKernel.fs.file(Sleep::kCurrentFile);
        if (file && file->open(true, false)) {
            file->seek(0);
            size_t bw = 0;
            file->write(reinterpret_cast<const char*>(&hdr), sizeof(hdr), bw);
            file->flush();
            file->close();
        }
    }

    // Phone-sync spike: also emit a workout-style FIT activity into
    // Activity/YYYYMM/ (the folder the phone pulls over BLE FTS).
    SleepFitWriter::exportNight(mKernel, hdr);

    // Archive the night and refresh the "last night" copy the GUI reads.
    char archive[24];
    snprintf(archive, sizeof(archive), "slp_%08lu.bin",
             static_cast<unsigned long>(hdr.dateKey));
    mKernel.fs.remove(archive); // keep the newest attempt at this date
    mKernel.fs.copy(Sleep::kCurrentFile, archive);
    mKernel.fs.copy(Sleep::kCurrentFile, Sleep::kLastFile);
    mKernel.fs.remove(Sleep::kCurrentFile);

    mHasSummary = true;
    LOG_INFO("session closed: %u epochs, deep %umin\n", epochCount, hdr.deepMin);

    updateIndex(hdr);
    sendSummary();
    sendTimeline();
}

void Service::updateIndex(const Sleep::SessionHeader& hdr)
{
    Sleep::IndexHeader idx{};
    Sleep::IndexSlot slots[Sleep::kMaxNights];
    uint32_t count = 0;

    // Load existing ring (tolerate missing/corrupt: start fresh).
    {
        auto file = mKernel.fs.file(Sleep::kIndexFile);
        if (file && file->open(false)) {
            size_t br = 0;
            if (file->read(reinterpret_cast<char*>(&idx), sizeof(idx), br)
                    && br == sizeof(idx)
                    && memcmp(idx.magic, "SIDX", 4) == 0
                    && idx.version == 1) {
                count = idx.count <= Sleep::kMaxNights ? idx.count : Sleep::kMaxNights;
                size_t br2 = 0;
                file->read(reinterpret_cast<char*>(slots),
                           count * sizeof(Sleep::IndexSlot), br2);
                count = br2 / sizeof(Sleep::IndexSlot);
            }
            file->close();
        }
    }

    // Re-tracked night: newest attempt replaces the old date entry.
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (slots[i].dateKey != hdr.dateKey) {
            slots[kept++] = slots[i];
        }
    }
    if (kept > Sleep::kMaxNights - 1) {
        kept = Sleep::kMaxNights - 1;
    }

    // Newest first.
    for (uint32_t i = kept; i > 0; --i) {
        slots[i] = slots[i - 1];
    }
    Sleep::IndexSlot& s = slots[0];
    s.dateKey  = hdr.dateKey;
    s.bedMin   = localMinutes(hdr.bedEpoch);
    s.wakeMin  = localMinutes(hdr.wakeEpoch);
    s.totalMin = hdr.totalMin;
    s.deepMin  = hdr.deepMin;
    s.lightMin = hdr.lightMin;
    s.awakeMin = hdr.awakeMin;
    s.hrAvg    = hdr.hrAvg;
    s.flags    = hdr.flags;
    ++kept;

    memcpy(idx.magic, "SIDX", 4);
    idx.version = 1;
    idx.count   = kept;

    auto file = mKernel.fs.file(Sleep::kIndexFile);
    if (file && file->open(true, true)) {
        size_t bw = 0;
        file->write(reinterpret_cast<const char*>(&idx), sizeof(idx), bw);
        file->write(reinterpret_cast<const char*>(slots),
                    kept * sizeof(Sleep::IndexSlot), bw);
        file->flush();
        file->close();
    }
}

void Service::sendHistory()
{
    if (!mGUIStarted) {
        return;
    }

    Sleep::IndexHeader idx{};
    Sleep::IndexSlot slots[CustomMessage::HistoryEntryData::kMaxRows];
    uint32_t count = 0;

    {
        auto file = mKernel.fs.file(Sleep::kIndexFile);
        if (!file || !file->open(false)) {
            return;
        }
        size_t br = 0;
        if (file->read(reinterpret_cast<char*>(&idx), sizeof(idx), br)
                && br == sizeof(idx)
                && memcmp(idx.magic, "SIDX", 4) == 0
                && idx.version == 1) {
            uint32_t want = idx.count;
            if (want > CustomMessage::HistoryEntryData::kMaxRows) {
                want = CustomMessage::HistoryEntryData::kMaxRows;
            }
            size_t br2 = 0;
            file->read(reinterpret_cast<char*>(slots),
                       want * sizeof(Sleep::IndexSlot), br2);
            count = br2 / sizeof(Sleep::IndexSlot);
        }
        file->close();
    }

    for (uint32_t i = 0; i < count; ++i) {
        CustomMessage::HistoryEntryData d{};
        d.dateKey  = slots[i].dateKey;
        d.bedMin   = slots[i].bedMin;
        d.wakeMin  = slots[i].wakeMin;
        d.totalMin = slots[i].totalMin;
        d.deepMin  = slots[i].deepMin;
        d.lightMin = slots[i].lightMin;
        d.awakeMin = slots[i].awakeMin;
        d.hrAvg    = slots[i].hrAvg;
        d.flags    = slots[i].flags;
        d.row      = static_cast<uint8_t>(i);
        d.rowCount = static_cast<uint8_t>(count);
        SDK::send_msg<CustomMessage::HistoryEntry>(mKernel, d);
    }
}

void Service::recoverInterruptedSession()
{
    if (!mKernel.fs.exist(Sleep::kCurrentFile)) {
        return;
    }
    LOG_INFO("recovering interrupted session\n");

    size_t size = 0;
    {
        auto file = mKernel.fs.file(Sleep::kCurrentFile);
        if (file && file->open(false)) {
            size = file->size();
            file->close();
        }
    }
    if (size < sizeof(Sleep::SessionHeader)) {
        mKernel.fs.remove(Sleep::kCurrentFile);
        return;
    }

    mFlushedEpochs = (size - sizeof(Sleep::SessionHeader)) / sizeof(Sleep::EpochRecord);
    mCloseFlags = Sleep::Flags::kInterrupted;
    finalizeSessionFile();
    mFlushedEpochs = 0;
    mCloseFlags = 0;
}

// --------------------------------------------------------------- GUI push

void Service::sendSessionState()
{
    if (!mGUIStarted) {
        return;
    }
    CustomMessage::SessionStateData d{};
    d.state = mState;
    d.aborted = mCloseFlags;
    d.liveHr = mLiveHr;
    d.hasSummary = mHasSummary ? 1 : 0;
    if (mState == CustomMessage::TrackingState::TRACKING) {
        d.elapsedMin = static_cast<uint16_t>(
            (mKernel.sys.getTimeMs() - mSessionStartMs) / 60000);
    }
    SDK::send_msg<CustomMessage::SessionState>(mKernel, d);
}

void Service::sendSummary()
{
    if (!mGUIStarted) {
        return;
    }
    Sleep::SessionHeader hdr;
    if (!mHasSummary || !loadLastHeader(hdr)) {
        return;
    }
    CustomMessage::SleepSummaryData d{};
    d.dateKey  = hdr.dateKey;
    d.bedMin   = localMinutes(hdr.bedEpoch);
    d.wakeMin  = localMinutes(hdr.wakeEpoch);
    d.totalMin = hdr.totalMin;
    d.awakeMin = hdr.awakeMin;
    d.lightMin = hdr.lightMin;
    d.deepMin  = hdr.deepMin;
    d.hrMin = hdr.hrMin;
    d.hrAvg = hdr.hrAvg;
    d.hrMax = hdr.hrMax;
    d.flags = hdr.flags;
    SDK::send_msg<CustomMessage::SleepSummary>(mKernel, d);
}

void Service::sendTimeline()
{
    if (!mGUIStarted) {
        return;
    }

    using Tl = CustomMessage::SleepTimelineData;

    Sleep::SessionHeader hdr;
    if (mHasSummary && loadLastHeader(hdr) && hdr.epochCount > 0) {
        // Bucket epochs into screen columns: column c covers epochs
        // [c*N/C, (c+1)*N/C); majority stage wins, ties go lighter
        // (conservative: a mixed slice reads as the lighter stage).
        memset(mColCounts, 0, sizeof(mColCounts));

        auto file = mKernel.fs.file(Sleep::kLastFile);
        uint16_t done = 0;
        if (file && file->open(false)) {
            file->seek(sizeof(Sleep::SessionHeader));
            Sleep::EpochRecord recs[64];
            while (done < hdr.epochCount) {
                uint16_t want = hdr.epochCount - done;
                if (want > 64) {
                    want = 64;
                }
                size_t br = 0;
                if (!file->read(reinterpret_cast<char*>(recs),
                                want * sizeof(Sleep::EpochRecord), br)
                        || br != want * sizeof(Sleep::EpochRecord)) {
                    break; // truncated file: treat as no timeline
                }
                for (uint16_t i = 0; i < want; ++i) {
                    uint32_t col = static_cast<uint32_t>(done + i)
                                   * Tl::kMaxColumns / hdr.epochCount;
                    uint8_t st = static_cast<uint8_t>(recs[i].stage());
                    if (st <= 2 && mColCounts[col][st] < 255) {
                        ++mColCounts[col][st];
                    }
                }
                done += want;
            }
            file->close();
        }

        if (done == hdr.epochCount) {
            for (uint8_t ch = 0; ch < Tl::kChunks; ++ch) {
                CustomMessage::SleepTimelineData d{};
                d.dateKey = hdr.dateKey;
                d.epochCount = hdr.epochCount;
                d.chunk = ch;
                d.chunkCount = Tl::kChunks;
                for (uint8_t c = 0; c < Tl::kColumnsPerChunk; ++c) {
                    uint16_t col = ch * Tl::kColumnsPerChunk + c;
                    uint8_t a = mColCounts[col][0];
                    uint8_t l = mColCounts[col][1];
                    uint8_t dp = mColCounts[col][2];
                    uint8_t st = (a >= l && a >= dp) ? 0 : (l >= dp ? 1 : 2);
                    d.columns[c / 4] |= static_cast<uint8_t>(st << ((c % 4) * 2));
                }
                SDK::send_msg<CustomMessage::SleepTimeline>(mKernel, d);
            }
            return;
        }
    }

    // No timeline: single marker so the GUI hides the bar.
    CustomMessage::SleepTimelineData none{};
    none.chunkCount = 0;
    SDK::send_msg<CustomMessage::SleepTimeline>(mKernel, none);
}

bool Service::loadLastHeader(Sleep::SessionHeader& hdr)
{
    auto file = mKernel.fs.file(Sleep::kLastFile);
    if (!file || !file->open(false)) {
        return false;
    }
    size_t br = 0;
    bool ok = file->read(reinterpret_cast<char*>(&hdr), sizeof(hdr), br)
              && br == sizeof(hdr)
              && memcmp(hdr.magic, "SLP1", 4) == 0;
    file->close();
    return ok;
}

// ------------------------------------------------------------------- time

void Service::localTime(std::tm& out)
{
    std::time_t utc = time(nullptr);
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&out, &utc);
#else
    localtime_r(&utc, &out);
#endif
}

uint16_t Service::localMinutes(std::time_t t)
{
    std::tm tm {};
    localtime_r(&t, &tm);
    return static_cast<uint16_t>(tm.tm_hour * 60 + tm.tm_min);
}

uint32_t Service::localDateKey(std::time_t t)
{
    std::tm tm {};
    localtime_r(&t, &tm);
    return static_cast<uint32_t>((tm.tm_year + 1900) * 10000
                                 + (tm.tm_mon + 1) * 100 + tm.tm_mday);
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
