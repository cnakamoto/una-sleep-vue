#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include <cstdint>

// Force 4-byte alignment for all message structures
#pragma pack(push, 4)

namespace CustomMessage {

// Service --> GUI
constexpr SDK::MessageType::Type PROBE_STATS = 0x00000001;

// Battery deci-percent when no sample has arrived yet.
constexpr int16_t kBatteryUnknown = -1;

// POD summary of the overnight probe log (probe.csv), computed by the
// service from the on-flash file and handed to the GUI as a whole.
struct ProbeStatsData {
    uint32_t firstEpoch;   // Epoch of first log line; 0 = no data at all
    uint32_t lastEpoch;    // Epoch of last log line
    uint32_t maxGapSec;    // Longest silence between consecutive log lines
    uint32_t upMin;        // Current service uptime (minutes)
    uint32_t hrSamples;    // 'H' lines
    uint16_t boots;        // 'B' lines — >1 means the service restarted
    uint16_t stops;        // 'X' lines — COMMAND_APP_STOP received
    uint16_t aliveCount;   // 'A' lines (one per minute of service life)
    int16_t  battFirstD;   // Battery deci-percent, first alive marker
    int16_t  battLastD;    // Battery deci-percent, most recent sample
};

// Service --> GUI
//
// Sent when the GUI starts; the view renders it as plain text. Pure data;
// allocated from a kernel pool.
struct ProbeStats : public SDK::MessageBase {
    ProbeStatsData d;

    ProbeStats()
        : SDK::MessageBase(PROBE_STATS)
        , d{}
    {}

    explicit ProbeStats(const ProbeStatsData& stats)
        : SDK::MessageBase(PROBE_STATS)
        , d(stats)
    {}
};

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
