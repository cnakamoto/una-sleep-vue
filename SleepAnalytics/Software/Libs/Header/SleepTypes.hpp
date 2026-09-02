#ifndef SLEEP_TYPES_HPP
#define SLEEP_TYPES_HPP

#include <cstdint>

// Sleep staging configuration and on-flash record layouts (ARCHITECTURE.md
// §5-§6). All staging thresholds live here for field tuning; the probe's
// overnight HR data (run 3) validated the ballpark values.
namespace Sleep {

enum class Stage : uint8_t {
    AWAKE = 0,
    LIGHT = 1,
    DEEP  = 2,
};

namespace Config {

constexpr uint32_t kEpochSec            = 30;
// Motion events (MOTION or SIG_MOTION) in one epoch that mark it AWAKE.
constexpr uint8_t  kAwakeMovementCount  = 3;
// DEEP needs HR at least this far below baseline (run 3 showed a 23% dip).
constexpr uint8_t  kDeepHrDropPct       = 10;
// ... sustained for this many consecutive still epochs (20 min).
constexpr uint16_t kDeepMinStillEpochs  = 40;
// Baseline becomes valid after this many still epochs with HR (15 min).
constexpr uint16_t kBaselineMinEpochs   = 30;
// Safety aborts while TRACKING.
constexpr uint8_t  kBatteryAbortPct     = 8;
constexpr uint32_t kUnwornAbortSec      = 30 * 60;
// Flush buffered epochs to flash every N epochs (10 min).
constexpr uint16_t kFlushEveryEpochs    = 20;
// RAM cap on still-HR samples used for the baseline median (8 h worth).
constexpr uint16_t kBaselineMaxSamples  = 960;

} // namespace Config

// ---- Epoch record: 4 bytes, little-endian ---------------------------
// stage:2 | movement:6 | hr:8 | spo2:8 (0 = none) | flags:8
struct EpochRecord {
    uint32_t bits;

    static constexpr uint32_t kStageMask = 0x3;
    static constexpr uint32_t kMoveShift = 2,  kMoveMask = 0x3F  << kMoveShift;
    static constexpr uint32_t kHrShift   = 8,  kHrMask   = 0xFF  << kHrShift;
    static constexpr uint32_t kSpo2Shift = 16, kSpo2Mask = 0xFF  << kSpo2Shift;

    static uint32_t pack(Stage s, uint8_t movement, uint8_t hrBpm, uint8_t spo2)
    {
        uint8_t mv = movement > 63 ? 63 : movement;
        return (static_cast<uint32_t>(s) & kStageMask)
             | (static_cast<uint32_t>(mv) << kMoveShift)
             | (static_cast<uint32_t>(hrBpm) << kHrShift)
             | (static_cast<uint32_t>(spo2) << kSpo2Shift);
    }

    Stage   stage()    const { return static_cast<Stage>(bits & kStageMask); }
    uint8_t movement() const { return (bits & kMoveMask) >> kMoveShift; }
    uint8_t hrBpm()    const { return (bits & kHrMask)   >> kHrShift; }
    uint8_t spo2()     const { return (bits & kSpo2Mask) >> kSpo2Shift; }
};
static_assert(sizeof(EpochRecord) == 4, "epoch record must stay 4 bytes");

// ---- Session header: 32 bytes at offset 0 of every night file --------
// Followed by epochCount EpochRecords. Timestamps are derived:
// epoch i covers [bedEpoch + i*30s, +30s).
struct SessionHeader {
    char     magic[4];       // "SLP1"
    uint32_t dateKey;        // YYYYMMDD of sleep onset (local), e.g. 20260901
    uint32_t bedEpoch;       // unix time, session start
    uint32_t wakeEpoch;      // unix time, session end (0 while recording)
    uint16_t epochCount;
    uint16_t totalMin;
    uint16_t awakeMin;
    uint16_t lightMin;
    uint16_t deepMin;
    uint8_t  hrMin;          // 0 = no valid HR all session
    uint8_t  hrAvg;
    uint8_t  hrMax;
    uint8_t  flags;          // bit0 unworn abort, bit1 battery abort, bit2 interrupted (power-off/USB)
    uint8_t  reserved[2];
};
static_assert(sizeof(SessionHeader) == 32, "session header must stay 32 bytes");

namespace Flags {
constexpr uint8_t kAbortedUnworn  = 0x01;
constexpr uint8_t kAbortedBattery = 0x02;
constexpr uint8_t kInterrupted    = 0x04;
}

// File names (app-private dir; see deploy layout /Apps/SleepAnalytics/).
constexpr const char* kCurrentFile = "slp_cur.bin";   // session in progress
constexpr const char* kLastFile    = "slp_last.bin";  // most recent closed night

} // namespace Sleep

#endif // SLEEP_TYPES_HPP
