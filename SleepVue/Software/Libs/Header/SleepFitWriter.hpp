#ifndef SLEEP_FIT_WRITER_HPP
#define SLEEP_FIT_WRITER_HPP

#include "SDK/Kernel/Kernel.hpp"
#include "SleepTypes.hpp"

#include <ctime>

// One-shot FIT export of a closed night (phone-sync spike).
//
// Writes a workout-style FIT activity to Activity/YYYYMM/ — the folder
// the phone pulls over BLE FTS — carrying the night's HR curve as
// timestamped records plus a sleep_stage developer field (0=awake,
// 1=light, 2=deep). Purpose: learn whether the UNA phone app ingests
// and displays app-written FIT files before investing in proper
// sleep-typed FIT (which the SDK's FitProfile doesn't model).
class SleepFitWriter
{
public:
    // hdr must be the FINALIZED header of the night file currently at
    // Sleep::kCurrentFile (records are streamed from that file).
    static bool exportNight(SDK::Kernel& kernel, const Sleep::SessionHeader& hdr);
};

#endif // SLEEP_FIT_WRITER_HPP
