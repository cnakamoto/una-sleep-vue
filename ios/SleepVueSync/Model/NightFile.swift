//
//  NightFile.swift
//  SleepVueSync
//
//  Parser for SleepVue night files (slp_YYYYMMDD.bin) as written by the
//  watch app. Layout source of truth: SleepVue/Software/Libs/Header/SleepTypes.hpp
//  (SessionHeader: 32 bytes LE, then epochCount 4-byte EpochRecords).
//  Stats conventions mirror tools/plot_night.py: totals are derived from the
//  epoch records, not trusted from the header.
//
//  Pure Foundation so ParserCheck can verify it against real night files.
//

import Foundation

enum SleepStage: UInt8, CaseIterable {
    case awake = 0
    case light = 1
    case deep = 2

    var displayName: String {
        switch self {
        case .awake: return "Awake"
        case .light: return "Light"
        case .deep: return "Deep"
        }
    }
}

/// One 30-second epoch. `hr == 0` and `spo2 == 0` mean "no sample".
struct SleepEpoch: Equatable, Hashable {
    let date: Date          // start of the 30 s window
    let stage: SleepStage
    let movement: UInt8     // motion event count (0–63)
    let hr: UInt8           // bpm (median of accepted 1 Hz samples), 0 = gap epoch
    let spo2: UInt8         // %, 0 = none
    // Quality byte (watch v0.9.0; both 0/false in older files = unknown):
    let hrSamples: UInt8    // 1 Hz samples that passed the cleaning gates (cap 63)
    let hrDropped: Bool     // the watch rejected >= 1 sample this epoch
}

struct NightFlags: OptionSet, Equatable, Hashable {
    let rawValue: UInt8
    static let unwornAbort   = NightFlags(rawValue: 0x01)
    static let batteryAbort  = NightFlags(rawValue: 0x02)
    static let interrupted   = NightFlags(rawValue: 0x04) // power-off / USB mid-session
    static let autoWake      = NightFlags(rawValue: 0x08) // closed by auto-wake rule
}

/// The 32-byte SessionHeader. Timestamps: bed/wake are unix epoch seconds;
/// wakeEpoch is 0 while the session is still recording.
struct NightHeader: Equatable, Hashable {
    let dateKey: UInt32     // YYYYMMDD of sleep onset, local time
    let bed: Date
    let wake: Date?         // nil = still recording on the watch
    let epochCount: UInt16
    let totalMin: UInt16
    let awakeMin: UInt16
    let lightMin: UInt16
    let deepMin: UInt16
    let hrMin: UInt8        // P5 of valid epoch HRs (raw min before v0.9.0); 0 = no valid HR
    let hrAvg: UInt8
    let hrMax: UInt8        // P95 (raw max before v0.9.0)
    let flags: NightFlags
    let hrCoverage: UInt8   // % of epochs with valid HR; 0 = unknown (pre-v0.9.0 file)
}

/// A maximal stretch of one stage — same computation as plot_night.py `runs()`.
struct StageRun: Equatable, Hashable {
    let start: Date
    let end: Date
    let stage: SleepStage
}

struct Night: Equatable, Hashable {
    let header: NightHeader
    let epochs: [SleepEpoch]

    var bed: Date { header.bed }

    /// Fall back to the end of the last epoch when the header has no wake yet.
    var wake: Date {
        header.wake ?? (epochs.last?.date.addingTimeInterval(30) ?? header.bed)
    }

    /// Maximal same-stage runs over the epochs (hypnogram, HealthKit export).
    var stageRuns: [StageRun] {
        guard !epochs.isEmpty else { return [] }
        var runs: [StageRun] = []
        var start = 0
        for i in 1...epochs.count {
            if i == epochs.count || epochs[i].stage != epochs[start].stage {
                runs.append(StageRun(start: epochs[start].date,
                                     end: epochs[i - 1].date.addingTimeInterval(30),
                                     stage: epochs[start].stage))
                start = i
            }
        }
        return runs
    }

    // MARK: Epoch-derived stats (single source of truth — mirrors plot_night.py)

    var totalMinutes: Int { epochs.count / 2 } // 30 s epochs

    func minutes(of stage: SleepStage) -> Int {
        epochs.reduce(0) { $0 + ($1.stage == stage ? 1 : 0) } / 2
    }

    var validHR: [Int] { epochs.compactMap { $0.hr > 0 ? Int($0.hr) : nil } }
    /// Night HR range = P5–P95 of the valid epoch HRs (ADR-0005), never the
    /// raw extremes. Shared integer rank rule sorted[(p*(n-1))/100] — same
    /// as the watch header and plot_night.py; recomputed here so old files
    /// display the trimmed range too.
    var hrMin: Int { Night.percentile(validHR, 5) }
    var hrMax: Int { Night.percentile(validHR, 95) }
    var hrAvg: Int { validHR.isEmpty ? 0 : validHR.reduce(0, +) / validHR.count }
    /// % of epochs with a valid HR — what the range and average rest on.
    var hrCoverage: Int { epochs.isEmpty ? 0 : validHR.count * 100 / epochs.count }

    static func percentile(_ values: [Int], _ p: Int) -> Int {
        guard !values.isEmpty else { return 0 }
        let sorted = values.sorted()
        return sorted[(p * (sorted.count - 1)) / 100]
    }

    /// A run of this many consecutive gap epochs breaks the HR trace; a
    /// single missing epoch is bridged. Same rule as plot_night.py.
    static let hrGapBreakEpochs = 2

    /// Valid-HR epochs split into runs separated by >= `hrGapBreakEpochs`
    /// gaps, so a chart never draws a line across a real dropout.
    var hrSegments: [[SleepEpoch]] {
        var segments: [[SleepEpoch]] = []
        var current: [SleepEpoch] = []
        var gap = 0
        for e in epochs {
            if e.hr > 0 {
                if gap >= Night.hrGapBreakEpochs, !current.isEmpty {
                    segments.append(current)
                    current = []
                }
                current.append(e)
                gap = 0
            } else {
                gap += 1
            }
        }
        if !current.isEmpty { segments.append(current) }
        return segments
    }

    /// dateKey like 20260903 → a display string like "Wed 3 Sep 2026".
    var displayDate: String {
        let s = String(header.dateKey)
        let parser = DateFormatter()
        parser.locale = Locale(identifier: "en_US_POSIX")
        parser.dateFormat = "yyyyMMdd"
        guard let d = parser.date(from: s) else { return s }
        let out = DateFormatter()
        out.dateFormat = "EEE d MMM yyyy"
        return out.string(from: d)
    }
}

enum NightParseError: Error, Equatable {
    case tooShort
    case badMagic
    case truncated
}

enum NightFile {

    static let headerSize = 32
    static let epochSize = 4
    static let epochSeconds: UInt32 = 30

    static func parse(_ data: Data) throws -> Night {
        guard data.count >= headerSize else { throw NightParseError.tooShort }
        guard data.prefix(4) == Data("SLP1".utf8) else { throw NightParseError.badMagic }

        let n = Int(data.leU16(at: 16))
        guard data.count >= headerSize + n * epochSize else { throw NightParseError.truncated }

        let bedEpoch = data.leU32(at: 8)
        let wakeEpoch = data.leU32(at: 12)

        let header = NightHeader(
            dateKey: data.leU32(at: 4),
            bed: Date(timeIntervalSince1970: TimeInterval(bedEpoch)),
            wake: wakeEpoch == 0 ? nil : Date(timeIntervalSince1970: TimeInterval(wakeEpoch)),
            epochCount: UInt16(n),
            totalMin: data.leU16(at: 18),
            awakeMin: data.leU16(at: 20),
            lightMin: data.leU16(at: 22),
            deepMin: data.leU16(at: 24),
            hrMin: UInt8(data[26]),
            hrAvg: UInt8(data[27]),
            hrMax: UInt8(data[28]),
            flags: NightFlags(rawValue: UInt8(data[29])),
            hrCoverage: UInt8(data[30])
        )

        // EpochRecord: stage:2 | movement:6 | hr:8 | spo2:8 | quality:8 (LE u32)
        // quality: hrSamples:6 | hrDropped:1 | reserved:1
        var epochs: [SleepEpoch] = []
        epochs.reserveCapacity(n)
        for i in 0..<n {
            let bits = data.leU32(at: headerSize + i * epochSize)
            epochs.append(SleepEpoch(
                date: Date(timeIntervalSince1970: TimeInterval(bedEpoch + UInt32(i) * epochSeconds)),
                stage: SleepStage(rawValue: UInt8(bits & 0x3)) ?? .light,
                movement: UInt8((bits >> 2) & 0x3F),
                hr: UInt8((bits >> 8) & 0xFF),
                spo2: UInt8((bits >> 16) & 0xFF),
                hrSamples: UInt8((bits >> 24) & 0x3F),
                hrDropped: (bits >> 30) & 0x1 == 1
            ))
        }

        return Night(header: header, epochs: epochs)
    }
}
