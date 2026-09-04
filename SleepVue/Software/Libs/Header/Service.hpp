#ifndef __SERVICE_HPP__
#define __SERVICE_HPP__

#include "SDK/Kernel/KernelProviderService.hpp"
#include "SDK/SensorLayer/SensorConnection.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"

#include "Commands.hpp"
#include "SleepTypes.hpp"

#include <cstdint>
#include <ctime>

// Sleep tracker service (ARCHITECTURE.md §3).
//
// IDLE: no sensor subscriptions; answers GUI state/summary requests.
// TRACKING: owns HR + motion + touch + battery subscriptions, aggregates
// 30 s epochs, classifies them (AWAKE/LIGHT/DEEP per SleepTypes.hpp
// thresholds), flushes to slp_cur.bin every 20 epochs. Session close
// (manual toggle, unworn/battery abort, or COMMAND_APP_STOP mid-night)
// finalizes the header, archives the night, and refreshes slp_last.bin.
//
// Crash safety: slp_cur.bin holds a placeholder header from session
// start; a boot that finds it closes the night out from the epoch
// records already on flash (losing nothing but the tail).
class Service
{
public:
    Service(SDK::Kernel& kernel);

    virtual ~Service();

    void run();

private:
    SDK::Kernel&             mKernel;
    bool                     mGUIStarted;

    uint8_t                  mState;      // CustomMessage::TrackingState::*

    SDK::Sensor::Connection  mSensorHr;
    SDK::Sensor::Connection  mSensorMotion;
    SDK::Sensor::Connection  mSensorTouch;
    SDK::Sensor::Connection  mSensorBattery;

    // --- session in progress (TRACKING) ---
    std::time_t              mBedEpoch;      // wall clock at start
    uint32_t                 mSessionStartMs;
    uint32_t                 mNextEpochCloseMs;

    // Current epoch accumulators
    uint8_t                  mEpochMovement;
    uint16_t                 mEpochHrSum;
    uint8_t                  mEpochHrCount;

    // Staging state
    uint16_t                 mBaselineCount;
    uint8_t                  mBaselineBpm;   // 0 = not valid yet
    bool                     mBaselineWindowOpen;
    uint8_t                  mStillHr[Sleep::Config::kBaselineMaxSamples];

    // Rolling movement window for the DEEP gate (ring of per-epoch
    // movement counts; sum covers the last kDeepWindowEpochs epochs).
    uint8_t                  mMoveWindow[Sleep::Config::kDeepWindowEpochs];
    uint16_t                 mMoveWindowIdx;
    uint16_t                 mMoveWindowCount;
    uint16_t                 mMoveWindowSum;

    // Auto-wake: ring of per-epoch 0/1 "any motion" flags over the
    // trailing kWakeWindowEpochs epochs, plus a total-quiet counter
    // proving real stillness was seen during this session.
    uint8_t                  mWakeWindow[Sleep::Config::kWakeWindowEpochs];
    uint16_t                 mWakeWindowIdx;
    uint16_t                 mWakeWindowCount;
    uint16_t                 mWakeActiveSum;
    uint16_t                 mQuietEpochs;

    // Auto-start (IDLE): ring of per-epoch motion counts over the last
    // kOnsetRingEpochs epochs, plus worn state from TOUCH_DETECT.
    uint8_t                  mOnsetRing[Sleep::Config::kOnsetRingEpochs];
    uint16_t                 mOnsetRingIdx;
    uint16_t                 mOnsetRingCount;
    bool                     mWornNow;

    // Buffered, not-yet-flushed epoch records
    Sleep::EpochRecord       mEpochBuf[Sleep::Config::kFlushEveryEpochs];
    uint16_t                 mEpochBufCount;
    uint16_t                 mFlushedEpochs;

    // Session running totals
    uint16_t                 mAwakeEpochs;
    uint16_t                 mLightEpochs;
    uint16_t                 mDeepEpochs;
    uint32_t                 mHrSum;
    uint32_t                 mHrValidCount;
    uint8_t                  mHrMin;
    uint8_t                  mHrMax;
    uint8_t                  mLiveHr;

    // Abort tracking
    uint32_t                 mUnwornSinceMs;   // 0 = worn / unknown
    uint8_t                  mBatteryPct;      // 0xFF = unknown
    uint8_t                  mCloseFlags;

    bool                     mHasSummary;      // slp_last.bin exists

    void onStartGUI();
    void onStopGUI();

    void handleSensorData(uint16_t handle, SDK::Sensor::DataBatch& data);

    void startTracking(uint32_t backdateSec);
    void stopTracking();      // manual stop or abort; uses mCloseFlags
    void closeEpochsIfDue();  // epoch boundaries in both states
    void closeCurrentEpoch();
    void idleEpochClosed();   // IDLE: onset ring + auto-start rule
    Sleep::Stage classifyEpoch(uint8_t movement, uint8_t hrMean) const;
    void flushEpochs();

    // Finalizes slp_cur.bin from the epoch records on flash (used by both
    // normal close and boot recovery), then archives it.
    void finalizeSessionFile();
    void recoverInterruptedSession();

    void sendSessionState();
    void sendSummary();
    bool loadLastHeader(Sleep::SessionHeader& hdr);

    // Night index ring (slp_idx.bin), updated on every session close.
    void updateIndex(const Sleep::SessionHeader& hdr);
    void sendHistory();

    // Local wall-clock helpers
    static void localTime(std::tm& out);
    static uint16_t localMinutes(std::time_t t);
    static uint32_t localDateKey(std::time_t t);

    static uint32_t ParseVersion(const char* str);
};

#endif
