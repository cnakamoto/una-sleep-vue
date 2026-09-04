#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include <cstdint>

// Force 4-byte alignment for all message structures
#pragma pack(push, 4)

namespace CustomMessage {

// GUI --> Service
constexpr SDK::MessageType::Type TRACKING_TOGGLE = 0x00000001;
constexpr SDK::MessageType::Type SUMMARY_REQUEST = 0x00000002;
// Service --> GUI
constexpr SDK::MessageType::Type SESSION_STATE   = 0x00000003;
constexpr SDK::MessageType::Type SLEEP_SUMMARY   = 0x00000004;
// GUI --> Service
constexpr SDK::MessageType::Type HISTORY_REQUEST = 0x00000005;
// Service --> GUI
constexpr SDK::MessageType::Type HISTORY_ENTRY   = 0x00000006;

namespace TrackingState {
constexpr uint8_t IDLE     = 0;
constexpr uint8_t TRACKING = 1;
}

// GUI --> Service
//
// R1 pressed: start a night session (IDLE) or end it (TRACKING).
struct TrackingToggle : public SDK::MessageBase {
    TrackingToggle()
        : SDK::MessageBase(TRACKING_TOGGLE)
    {}
};

// GUI --> Service
//
// Ask for the current state + last-night summary (sent on GUI start).
struct SummaryRequest : public SDK::MessageBase {
    SummaryRequest()
        : SDK::MessageBase(SUMMARY_REQUEST)
    {}
};

// GUI --> Service
//
// Ask for the night index (sent when the history page opens). The
// service answers with one HISTORY_ENTRY per stored night, most
// recent first, at most HistoryEntryData::kMaxRows of them.
struct HistoryRequest : public SDK::MessageBase {
    HistoryRequest()
        : SDK::MessageBase(HISTORY_REQUEST)
    {}
};

// POD payloads (MessageBase is non-copyable; the GUI keeps copies of
// these, not of the messages).
struct SessionStateData {
    uint8_t  state;       // TrackingState::*
    uint8_t  aborted;     // Sleep::Flags::* of the last session, 0 if none
    uint16_t elapsedMin;  // while TRACKING
    uint8_t  liveHr;      // latest bpm while TRACKING, 0 = none yet
    uint8_t  hasSummary;  // a completed night is available for SLEEP_SUMMARY
};

struct SleepSummaryData {
    uint32_t dateKey;     // YYYYMMDD of sleep onset
    uint16_t bedMin;      // local minutes since midnight
    uint16_t wakeMin;
    uint16_t totalMin;
    uint16_t awakeMin;
    uint16_t lightMin;
    uint16_t deepMin;
    uint8_t  hrMin;       // 0 = no valid HR
    uint8_t  hrAvg;
    uint8_t  hrMax;
    uint8_t  flags;       // Sleep::Flags::*
};

// Service --> GUI
//
// Current tracking state. Sent on GUI start, on every state change, and
// periodically while TRACKING so the view's elapsed time and live HR
// stay fresh.
struct SessionState : public SDK::MessageBase {
    SessionStateData d;

    SessionState()
        : SDK::MessageBase(SESSION_STATE)
        , d{}
    {}

    explicit SessionState(const SessionStateData& data)
        : SDK::MessageBase(SESSION_STATE)
        , d(data)
    {}
};

// Service --> GUI
//
// Last completed night's summary, decoded from the on-flash session
// header. Sent on GUI start (if available) and right after a session
// closes.
struct SleepSummary : public SDK::MessageBase {
    SleepSummaryData d;

    SleepSummary()
        : SDK::MessageBase(SLEEP_SUMMARY)
        , d{}
    {}

    explicit SleepSummary(const SleepSummaryData& data)
        : SDK::MessageBase(SLEEP_SUMMARY)
        , d(data)
    {}
};

// One history row. dateKey/bedMin/wakeMin mirror the index slot.
struct HistoryEntryData {
    uint32_t dateKey;     // YYYYMMDD of sleep onset
    uint16_t bedMin;
    uint16_t wakeMin;
    uint16_t totalMin;
    uint16_t deepMin;
    uint16_t lightMin;
    uint16_t awakeMin;
    uint8_t  hrAvg;
    uint8_t  flags;
    uint8_t  row;         // 0 = most recent
    uint8_t  rowCount;    // total rows being streamed
    uint8_t  reserved[2];

    static constexpr uint8_t kMaxRows = 7;
};

// Service --> GUI
//
// One night per message, streamed in reply to HISTORY_REQUEST.
struct HistoryEntry : public SDK::MessageBase {
    HistoryEntryData d;

    HistoryEntry()
        : SDK::MessageBase(HISTORY_ENTRY)
        , d{}
    {}

    explicit HistoryEntry(const HistoryEntryData& data)
        : SDK::MessageBase(HISTORY_ENTRY)
        , d(data)
    {}
};

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
