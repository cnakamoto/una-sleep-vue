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
// ... and at most this many motion events in the trailing 20 min
// (rolling window — position shifts must not reset deep, but real
// restlessness must block it). Window length in epochs:
constexpr uint16_t kDeepWindowEpochs    = 40;
constexpr uint8_t  kDeepMaxWindowMove   = 2;
// Baseline = median HR of still epochs during the first kBaselineWindowSec
// of the session (§5: "first-hour median while motionless"), frozen when
// the window closes. Extended if too few samples (restless onset).
constexpr uint32_t kBaselineWindowSec   = 90 * 60;
constexpr uint16_t kBaselineMinEpochs   = 30;
// Auto-wake: close the session when wakefulness is sustained. Rule
// validated offline against real nights (fires ~10 min after getting
// up; never on bathroom trips or restless patches): at least
// kWakeMinActiveEpochs epochs with any motion in the trailing
// kWakeWindowEpochs epochs, after a settle-in grace, and only once
// real stillness has been seen (so "started but never slept" evenings
// can't auto-save a junk night).
constexpr uint16_t kWakeWindowEpochs    = 20;   // trailing 10 min
constexpr uint8_t  kWakeMinActiveEpochs = 16;
constexpr uint32_t kWakeMinSessionMin   = 60;
constexpr uint16_t kWakeMinQuietEpochs  = 60;
// Auto-start: in IDLE, onset = sustained quiet in the night arming
// window while worn. Validated offline against real nights (the 22/24
// rule fires ~10 min into stillness; bed backdates to the last
// significant motion, capped at kBackfillMaxEpochs).
constexpr uint16_t kOnsetWindowEpochs   = 24;   // trailing 12 min
constexpr uint8_t  kOnsetMinQuietEpochs = 22;
constexpr uint16_t kArmStartMin         = 20 * 60;  // local 20:00
constexpr uint16_t kArmEndMin           = 3 * 60;   // local 03:00 (wraps midnight)
constexpr uint16_t kOnsetRingEpochs     = 120;  // 60 min of IDLE motion history
constexpr uint16_t kBackfillMaxEpochs   = 60;   // backdate at most 30 min
// Sessions shorter than this are discarded at close (couch captures,
// bench tests, naps) — one real night per date is the app's model.
constexpr uint16_t kMinSaveMin          = 180;
// ... and sessions whose longest motion-free stretch reaches this many
// epochs (2 h) are not sleep either: the watch lay unworn somewhere
// perfectly still. TOUCH_DETECT is NOT a reliable unworn signal on a
// bedside table — the 2026-09-05 capture "wore" the table for 12.6 h,
// staging 173 min of DEEP out of garbage optical HR. Real nights never
// exceed ~0.7 h without a motion event (2026-09-02..06 data).
constexpr uint16_t kMaxStillEpochs      = 240;  // 2 h
// Safety aborts while TRACKING.
constexpr uint8_t  kBatteryAbortPct     = 8;
constexpr uint32_t kUnwornAbortSec      = 30 * 60;
// Flush buffered epochs to flash every N epochs (10 min).
constexpr uint16_t kFlushEveryEpochs    = 20;
// ---- HR sample cleaning (v0.9.0) -------------------------------------
// Raw 1 Hz HR is accepted into an epoch only if it passes all gates —
// motivation: the 2026-09-13 night had 22% of samples at trust 0/1 and
// 13% of epochs with ONLY low-trust samples; those epochs fed a garbage
// 51 bpm baseline and 32 min of phantom DEEP (the loose-band failure
// mode). Validated offline (tools/hr_filter_study.py replaying
// prb_20260911/0913.csv): the clean 09-11 night is unchanged (baseline
// 81->82, deep 194->196 min) while 09-13 corrects (baseline ->62,
// junk-only epochs become honest hr=0 gaps, 82% coverage).
constexpr uint8_t  kHrMinTrust        = 2;    // platform trustLevel gate
constexpr uint8_t  kHrMinValidBpm     = 30;   // physiological range, sleep
constexpr uint8_t  kHrMaxValidBpm     = 200;  // (0 hits in 68k samples — safety net)
// Spike guard: the platform pre-smooths HR (one >20 bpm 1-s jump in 68k
// probe samples), so a jump this big is accepted only when it repeats
// (next sample within kHrSpikeConfirmBpm of the pending value). After a
// kHrStaleSec measurement gap the next valid sample re-arms the chain.
constexpr uint8_t  kHrSpikeJumpBpm    = 20;
constexpr uint8_t  kHrSpikeConfirmBpm = 10;
constexpr uint32_t kHrStaleSec        = 60;
// Epoch HR = median of the accepted samples (upper median, sorted[n/2])
// — robust to residual spikes, unlike the mean (p99 mean-vs-median skew
// was 4-6 bpm/epoch in the probe nights). Buffer caps the ~30 samples a
// 1 Hz stream delivers per epoch.
constexpr uint8_t  kEpochHrMaxSamples = 32;
// HR probe (ARCHITECTURE.md §9): while TRACKING, log every raw 1 Hz HR
// sample (bpm x10 + trustLevel) plus motion, battery, session start/end
// to prb_YYYYMMDD.csv, one line per event (open/seek-end/write/flush/
// close per line — crash-safe at ~1-2 Hz). Began life as the HEART_BEAT
// RR probe; the sensor proved absent (ADR-0002) and the subscription is
// gone since v0.9.0. It stays on as the raw trail behind the cleaned
// epochs (ADR-0005): hr_filter_study.py --compare replays it against the
// night's .bin, and every follow-up HR study needs it. The knob keeps its
// historical name.
constexpr bool kBeatProbeEnabled        = true;
// RAM cap on still-HR samples used for the baseline median (8 h worth).
constexpr uint16_t kBaselineMaxSamples  = 960;

} // namespace Config

// ---- Epoch record: 4 bytes, little-endian ---------------------------
// stage:2 | movement:6 | hr:8 | spo2:8 (0 = none) | quality:8
// quality (v0.9.0): hrSamples:6 | hrDropped:1 | reserved:1 — the
// lightweight "reason code" trail: how many 1 Hz samples survived the
// cleaning gates (63 = cap), and whether any were rejected. The byte is
// 0 in pre-0.9.0 files (= unknown); readers that mask only bits 0-23
// are unaffected.
struct EpochRecord {
    uint32_t bits;

    static constexpr uint32_t kStageMask = 0x3;
    static constexpr uint32_t kMoveShift = 2,  kMoveMask = 0x3F  << kMoveShift;
    static constexpr uint32_t kHrShift   = 8,  kHrMask   = 0xFF  << kHrShift;
    static constexpr uint32_t kSpo2Shift = 16, kSpo2Mask = 0xFF  << kSpo2Shift;
    static constexpr uint32_t kQShift    = 24, kQSamplesMask = 0x3F << kQShift;
    static constexpr uint32_t kQDroppedBit = 1u << 30;

    static uint32_t pack(Stage s, uint8_t movement, uint8_t hrBpm, uint8_t spo2,
                         uint8_t hrSamples = 0, bool hrDropped = false)
    {
        uint8_t mv = movement > 63 ? 63 : movement;
        uint8_t qs = hrSamples > 63 ? 63 : hrSamples;
        return (static_cast<uint32_t>(s) & kStageMask)
             | (static_cast<uint32_t>(mv) << kMoveShift)
             | (static_cast<uint32_t>(hrBpm) << kHrShift)
             | (static_cast<uint32_t>(spo2) << kSpo2Shift)
             | (static_cast<uint32_t>(qs) << kQShift)
             | (hrDropped ? kQDroppedBit : 0);
    }

    Stage   stage()    const { return static_cast<Stage>(bits & kStageMask); }
    uint8_t movement() const { return (bits & kMoveMask) >> kMoveShift; }
    uint8_t hrBpm()    const { return (bits & kHrMask)   >> kHrShift; }
    uint8_t spo2()     const { return (bits & kSpo2Mask) >> kSpo2Shift; }
    uint8_t hrSamples() const { return (bits & kQSamplesMask) >> kQShift; }
    bool    hrDropped() const { return (bits & kQDroppedBit) != 0; }
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
    // v0.9.0: hrMin/hrMax are ROBUST percentiles of the valid epoch HRs —
    // P5 / P95 with the shared integer rank rule sorted[(p*(n-1))/100]
    // (mirrored by NightFile.swift and tools/plot_night.py; degrades to
    // raw extremes on small n). Pre-0.9.0 files hold raw min/max here.
    // A single garbage epoch must not set the night's "resting" HR.
    uint8_t  hrMin;          // P5 of valid epoch HRs; 0 = no valid HR all session
    uint8_t  hrAvg;          // mean of valid epoch HRs
    uint8_t  hrMax;          // P95 of valid epoch HRs
    uint8_t  flags;          // bit0 unworn abort, bit1 battery abort, bit2 interrupted (power-off/USB)
    uint8_t  hrCoverage;     // % of epochs with valid HR (v0.9.0; 0 in older files = unknown)
    uint8_t  reserved[1];
};
static_assert(sizeof(SessionHeader) == 32, "session header must stay 32 bytes");

namespace Flags {
constexpr uint8_t kAbortedUnworn  = 0x01;
constexpr uint8_t kAbortedBattery = 0x02;
constexpr uint8_t kInterrupted    = 0x04;
constexpr uint8_t kAutoWake       = 0x08;
}

// ---- Night index: slp_idx.bin ---------------------------------------
// Ring of the last kMaxNights sessions, most recent first. One slot
// carries everything a history row needs, so the GUI never opens the
// per-night files to build the list.
constexpr uint8_t kMaxNights = 14;

struct IndexSlot {
    uint32_t dateKey;     // YYYYMMDD of sleep onset
    uint16_t bedMin;      // local minutes since midnight
    uint16_t wakeMin;
    uint16_t totalMin;
    uint16_t deepMin;
    uint16_t lightMin;
    uint16_t awakeMin;
    uint8_t  hrAvg;
    uint8_t  flags;       // Sleep::Flags::*
};
static_assert(sizeof(IndexSlot) == 20, "index slot layout changed");

struct IndexHeader {
    char     magic[4];    // "SIDX"
    uint32_t version;     // 1
    uint32_t count;       // valid slots (<= kMaxNights)
};
static_assert(sizeof(IndexHeader) == 12, "index header must stay 12 bytes");

// File names (app-private dir; see deploy layout /Apps/SleepVue/).
constexpr const char* kCurrentFile = "slp_cur.bin";   // session in progress
constexpr const char* kLastFile    = "slp_last.bin";  // most recent closed night
constexpr const char* kIndexFile   = "slp_idx.bin";   // night index ring

} // namespace Sleep

#endif // SLEEP_TYPES_HPP
