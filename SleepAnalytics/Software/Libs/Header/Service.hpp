#ifndef __SERVICE_HPP__
#define __SERVICE_HPP__

#include "SDK/Kernel/KernelProviderService.hpp"
#include "SDK/SensorLayer/SensorConnection.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"

#include "Commands.hpp"

#include <cstdint>

// Overnight residency + battery probe (see ARCHITECTURE.md §10).
//
// The service starts at watch boot (APP_AUTOSTART On), subscribes to
// HEART_RATE @ 0.1 Hz and BATTERY_LEVEL, and appends one CSV line per event
// to probe.csv on flash:
//
//   B,<epoch>,<uptimeMs>          service boot
//   G,<epoch>,<uptimeMs>          GUI started (user opened the app)
//   X,<epoch>,<uptimeMs>          COMMAND_APP_STOP received
//   A,<epoch>,<uptimeMs>,<battD>  alive marker (1/min, battery deci-%)
//   H,<epoch>,<bpmD>,<trustD>     heart-rate sample (decis, integers only)
//
// Residency verdict: exactly one B, no X, and A/H lines spanning the whole
// night. Battery verdict: first vs last battD of the night.
class Service
{
public:
    Service(SDK::Kernel& kernel);

    virtual ~Service();

    void run();

private:
    SDK::Kernel&             mKernel;
    bool                     mGUIStarted;

    SDK::Sensor::Connection  mSensorHr;
    SDK::Sensor::Connection  mSensorBattery;
    int16_t                  mBattD;        // Last battery deci-percent
    uint32_t                 mLastAliveMs;  // getTimeMs() of last alive marker

    void onStartGUI();
    void onStopGUI();

    void handleSensorData(uint16_t handle, SDK::Sensor::DataBatch& data);

    // Appends one line (incl. '\n') to probe.csv: open, seek to end, write,
    // flush, close — per line, so a crash loses at most the last line.
    void appendLine(const char* line);
    void writeBootMarker();
    void writeAliveMarkerIfDue();

    // Parses probe.csv into stats for the GUI. Runs on GUI start; the file
    // is small (one night ~80 KB), so a linear pass is acceptable here.
    CustomMessage::ProbeStatsData analyzeLog() const;

    static uint32_t ParseVersion(const char* str);
};

#endif
