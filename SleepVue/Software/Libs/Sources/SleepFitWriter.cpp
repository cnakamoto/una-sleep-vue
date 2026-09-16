#include "SleepFitWriter.hpp"

#include "SDK/Fit/FitWriter.hpp"
#include "SDK/Fit/FitProfile.hpp"
#include "SDK/Interfaces/IFileSystem.hpp"

#include <cstdio>
#include <cstring>

#define LOG_MODULE_PRX      "SleepFit"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace fit = SDK::Fit;

namespace
{

// FIT local message slots
enum : uint8_t {
    L_FILE_ID = 0,
    L_DEV_ID,
    L_FIELD_DESC,
    L_EVENT,
    L_RECORD,
    L_LAP,
    L_SESSION,
    L_ACTIVITY,
};

constexpr uint8_t DF_SLEEP_STAGE = 0;

constexpr std::time_t kFitEpochOffset = 631065600; // 1970 -> 1989-12-31

uint32_t toFitTime(std::time_t t)
{
    return static_cast<uint32_t>(t - kFitEpochOffset);
}

// epoch -> "local epoch" (local wall clock reinterpreted as UTC), the
// convention FIT local_timestamp fields use.
std::time_t tm2epoch(const struct tm* tm)
{
    int y = tm->tm_year + 1900;
    int m = tm->tm_mon + 1;
    if (m <= 2) { y -= 1; m += 12; }
    int64_t  era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = static_cast<uint32_t>(y - era * 400);
    uint32_t doy = (153 * (m - 3) + 2) / 5 + tm->tm_mday - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t  days = era * 146097 + static_cast<int64_t>(doe) - 719468;
    return static_cast<std::time_t>(days * 86400 + tm->tm_hour * 3600
                                    + tm->tm_min * 60 + tm->tm_sec);
}

std::time_t epochToLocal(std::time_t utc)
{
    std::tm lt {};
    localtime_r(&utc, &lt);
    return tm2epoch(&lt);
}

} // namespace

bool SleepFitWriter::exportNight(SDK::Kernel& kernel, const Sleep::SessionHeader& hdr)
{
    if (hdr.epochCount == 0 || hdr.wakeEpoch <= hdr.bedEpoch) {
        return false;
    }

    // Open the finalized night file (records start after the header).
    auto src = kernel.fs.file(Sleep::kCurrentFile);
    if (!src || !src->open(false)) {
        LOG_INFO("export: night file open failed\n");
        return false;
    }
    src->seek(sizeof(Sleep::SessionHeader));

    // Activity/YYYYMM/ from local bed time — the phone's sync convention.
    std::tm bedTm {};
    std::time_t bed = static_cast<std::time_t>(hdr.bedEpoch);
    localtime_r(&bed, &bedTm);

    char dir[32];
    snprintf(dir, sizeof(dir), "Activity/%04u%02u",
             bedTm.tm_year + 1900, bedTm.tm_mon + 1);
    kernel.fs.mkdir("Activity");
    kernel.fs.mkdir(dir);

    char path[64];
    snprintf(path, sizeof(path), "%s/activity_%04u%02u%02uT%02u%02u%02u.fit",
             dir, bedTm.tm_year + 1900, bedTm.tm_mon + 1, bedTm.tm_mday,
             bedTm.tm_hour, bedTm.tm_min, bedTm.tm_sec);

    auto file = kernel.fs.file(path);
    if (!file || !file->open(true, true)) {
        LOG_INFO("export: create failed [%s]\n", path);
        src->close();
        return false;
    }

    fit::FitWriter w(*file);
    bool ok = w.begin(/*profileVersion=*/0);

    // file_id
    w.defineMessage(L_FILE_ID, fit::mesgNum(fit::MesgNum::FileId),
        {fit::field::FileId::Type, fit::field::FileId::Manufacturer,
         fit::field::FileId::Product, fit::field::FileId::SerialNumber,
         fit::field::FileId::TimeCreated});
    w.data(L_FILE_ID)
        .u8(static_cast<uint8_t>(fit::File::Activity))
        .u16(static_cast<uint16_t>(fit::Manufacturer::Development))
        .u16(0)
        .u32(0)
        .u32(toFitTime(hdr.wakeEpoch))
        .write();

    // developer_data_id + sleep_stage field description
    w.defineMessage(L_DEV_ID, fit::mesgNum(fit::MesgNum::DeveloperDataId),
        {fit::field::DeveloperDataId::ApplicationId,
         fit::field::DeveloperDataId::DeveloperDataIndex});
    {
        uint8_t appId[16] = {};
        std::strncpy(reinterpret_cast<char*>(appId), "SleepVue", sizeof(appId));
        w.data(L_DEV_ID).bytes(appId, sizeof(appId)).u8(0).write();
    }
    {
        const char* name = "sleep_stage";
        const char* units = "";
        w.defineMessage(L_FIELD_DESC, fit::mesgNum(fit::MesgNum::FieldDescription),
            {fit::field::FieldDescription::DeveloperDataIndex,
             fit::field::FieldDescription::FieldDefinitionNumber,
             fit::field::FieldDescription::FitBaseTypeId,
             {fit::field::FieldDescription::kFieldNameNum, fit::BaseType::String,
              static_cast<uint8_t>(std::strlen(name) + 1)},
             {fit::field::FieldDescription::kUnitsNum, fit::BaseType::String, 1}});
        w.data(L_FIELD_DESC)
            .u8(0)
            .u8(DF_SLEEP_STAGE)
            .u8(fit::baseTypeId(fit::BaseType::UInt8))
            .str(name, static_cast<uint8_t>(std::strlen(name) + 1))
            .str(units, 1)
            .write();
    }

    // event (start/stop)
    w.defineMessage(L_EVENT, fit::mesgNum(fit::MesgNum::Event),
        {fit::field::Event::Timestamp, fit::field::Event::EventField,
         fit::field::Event::EventType});

    // record: timestamp + HR + sleep_stage (developer field)
    {
        const fit::FitWriter::DevField stage[] = {{DF_SLEEP_STAGE, 1, 0}};
        w.defineMessage(L_RECORD, fit::mesgNum(fit::MesgNum::Record),
            {fit::field::Record::Timestamp, fit::field::Record::HeartRate},
            {stage[0]});
    }

    // lap / session / activity
    w.defineMessage(L_LAP, fit::mesgNum(fit::MesgNum::Lap),
        {fit::field::Lap::Timestamp, fit::field::Lap::StartTime,
         fit::field::Lap::TotalElapsedTime, fit::field::Lap::TotalTimerTime,
         fit::field::Lap::MessageIndex, fit::field::Lap::AvgHeartRate,
         fit::field::Lap::MaxHeartRate});
    w.defineMessage(L_SESSION, fit::mesgNum(fit::MesgNum::Session),
        {fit::field::Session::Timestamp, fit::field::Session::StartTime,
         fit::field::Session::TotalElapsedTime, fit::field::Session::TotalTimerTime,
         fit::field::Session::MessageIndex, fit::field::Session::NumLaps,
         fit::field::Session::Sport, fit::field::Session::SubSport,
         fit::field::Session::AvgHeartRate, fit::field::Session::MaxHeartRate});
    w.defineMessage(L_ACTIVITY, fit::mesgNum(fit::MesgNum::Activity),
        {fit::field::Activity::Timestamp, fit::field::Activity::TotalTimerTime,
         fit::field::Activity::LocalTimestamp, fit::field::Activity::NumSessions});

    // START
    w.data(L_EVENT)
        .u32(toFitTime(hdr.bedEpoch))
        .u8(static_cast<uint8_t>(fit::Event::Timer))
        .u8(static_cast<uint8_t>(fit::EventType::Start))
        .write();

    // One record per epoch (0xFF = invalid HR sentinel, stage always valid)
    Sleep::EpochRecord rec {};
    for (uint16_t i = 0; i < hdr.epochCount; ++i) {
        size_t br = 0;
        if (!src->read(reinterpret_cast<char*>(&rec), sizeof(rec), br)
                || br != sizeof(rec)) {
            ok = false;
            break;
        }
        uint8_t hr = rec.hrBpm() > 0 ? rec.hrBpm() : 0xFF;
        w.data(L_RECORD)
            .u32(toFitTime(hdr.bedEpoch + i * Sleep::Config::kEpochSec))
            .u8(hr)
            .u8(static_cast<uint8_t>(rec.stage()))
            .write();
    }
    src->close();

    // STOP
    w.data(L_EVENT)
        .u32(toFitTime(hdr.wakeEpoch))
        .u8(static_cast<uint8_t>(fit::Event::Timer))
        .u8(static_cast<uint8_t>(fit::EventType::Stop))
        .write();

    uint32_t durMs = (hdr.wakeEpoch - hdr.bedEpoch) * 1000;

    // Single lap for the whole night
    w.data(L_LAP)
        .u32(toFitTime(hdr.wakeEpoch))
        .u32(toFitTime(hdr.bedEpoch))
        .u32(durMs)
        .u32(durMs)
        .u16(0)
        .u8(hdr.hrAvg)
        .u8(hdr.hrMax)  // P95, deliberately (ADR-0005): not the literal max
        .write();

    // Session (sport: generic — the spike question is whether the phone
    // ingests app-written FIT at all; proper sleep typing comes later)
    w.data(L_SESSION)
        .u32(toFitTime(hdr.wakeEpoch))
        .u32(toFitTime(hdr.bedEpoch))
        .u32(durMs)
        .u32(durMs)
        .u16(0)
        .u16(1)
        .u8(static_cast<uint8_t>(fit::Sport::Generic))
        .u8(static_cast<uint8_t>(fit::SubSport::Generic))
        .u8(hdr.hrAvg)
        .u8(hdr.hrMax)  // P95 (ADR-0005)
        .write();

    w.data(L_ACTIVITY)
        .u32(toFitTime(hdr.wakeEpoch))
        .u32(durMs)
        .u32(toFitTime(epochToLocal(static_cast<std::time_t>(hdr.wakeEpoch))))
        .u16(1)
        .write();

    ok = w.finish() && ok && w.ok();
    ok = file->flush() && ok;
    ok = file->close() && ok;

    if (!ok) {
        LOG_INFO("export: failed, removing partial file\n");
        kernel.fs.remove(path);
    } else {
        LOG_INFO("export: wrote %s\n", path);
    }
    return ok;
}
