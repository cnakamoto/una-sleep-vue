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

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
