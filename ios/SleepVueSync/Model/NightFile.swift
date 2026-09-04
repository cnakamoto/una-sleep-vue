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
    let hr: UInt8           // bpm, 0 = none
    let spo2: UInt8         // %, 0 = none
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
    let hrMin: UInt8        // 0 = no valid HR all session
    let hrAvg: UInt8
    let hrMax: UInt8
    let flags: NightFlags
}

struct Night: Equatable, Hashable {
    let header: NightHeader
    let epochs: [SleepEpoch]

    var bed: Date { header.bed }

    /// Fall back to the end of the last epoch when the header has no wake yet.
    var wake: Date {
        header.wake ?? (epochs.last?.date.addingTimeInterval(30) ?? header.bed)
    }

    // MARK: Epoch-derived stats (single source of truth — mirrors plot_night.py)

    var totalMinutes: Int { epochs.count / 2 } // 30 s epochs

    func minutes(of stage: SleepStage) -> Int {
        epochs.reduce(0) { $0 + ($1.stage == stage ? 1 : 0) } / 2
    }

    var validHR: [Int] { epochs.compactMap { $0.hr > 0 ? Int($0.hr) : nil } }
    var hrMin: Int { validHR.min() ?? 0 }
    var hrMax: Int { validHR.max() ?? 0 }
    var hrAvg: Int { validHR.isEmpty ? 0 : validHR.reduce(0, +) / validHR.count }

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
            flags: NightFlags(rawValue: UInt8(data[29]))
        )

        // EpochRecord: stage:2 | movement:6 | hr:8 | spo2:8 | flags:8 (LE u32)
        var epochs: [SleepEpoch] = []
        epochs.reserveCapacity(n)
        for i in 0..<n {
            let bits = data.leU32(at: headerSize + i * epochSize)
            epochs.append(SleepEpoch(
                date: Date(timeIntervalSince1970: TimeInterval(bedEpoch + UInt32(i) * epochSeconds)),
                stage: SleepStage(rawValue: UInt8(bits & 0x3)) ?? .light,
                movement: UInt8((bits >> 2) & 0x3F),
                hr: UInt8((bits >> 8) & 0xFF),
                spo2: UInt8((bits >> 16) & 0xFF)
            ))
        }

        return Night(header: header, epochs: epochs)
    }
}
