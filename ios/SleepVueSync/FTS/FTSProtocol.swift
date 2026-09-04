//
//  FTSProtocol.swift
//  SleepVueSync
//
//  UNA Watch BLE File Transfer Service (FTS) wire protocol.
//  Reference: una-sdk/Docs/BLE-File-Transfer-Service.md
//
//  Pure Foundation — no CoreBluetooth — so this file also compiles into the
//  ParserCheck command-line harness for host-side verification.
//

import Foundation

// MARK: - Constants (from the FTS spec)

enum FTS {
    /// Service 0xFEBB; characteristics ADAF0001 (Version) / ADAF0002 (Raw Transfer).
    static let serviceUUIDString = "0000FEBB-0000-1000-8000-00805F9B34FB"
    static let versionUUIDString = "ADAF0001-4669-6C65-5472-616E73666572"
    static let rawTransferUUIDString = "ADAF0002-4669-6C65-5472-616E73666572"

    /// Recommended read window for protocol v5 (spec: "Recommended window: 4096 bytes").
    static let readWindow: UInt32 = 4096

    enum Command: UInt8 {
        case read = 0x10
        case readData = 0x11
        case readPacing = 0x12
        case delete = 0x30
        case deleteStatus = 0x31
        case listDir = 0x50
        case listDirEntry = 0x51
        case digest = 0x70        // UNA extension, protocol v5+
        case digestStatus = 0x71  // UNA extension, protocol v5+
    }

    enum Status: UInt8 {
        case ok = 0x01
        case error = 0x02
        case noFile = 0x03
        case protocolError = 0x04
        case readOnly = 0x05
    }
}

enum FTSError: Error, Equatable {
    case notReady                 // not connected / characteristics missing
    case busy                     // another operation already in flight
    case timeout                  // no traffic for the watchdog interval
    case scanTimeout              // no matching peripheral found in time
    case connectTimeout           // found the watch but connect/handshake stalled
    case disconnected
    case bluetoothUnavailable(String)
    case serviceNotFound
    case watchRejected(FTS.Status) // status byte from the watch
    case malformedResponse
    case digestMismatch
}

extension FTSError: LocalizedError {
    var errorDescription: String? {
        switch self {
        case .notReady: return "Not connected to the watch."
        case .busy: return "Another transfer is already in progress."
        case .timeout: return "The watch stopped responding (no data for 15 s)."
        case .scanTimeout: return "No UNA Watch found. If another app or phone is connected to the watch it stops advertising — disconnect there and try again."
        case .connectTimeout: return "Found the watch but the connection stalled."
        case .disconnected: return "Disconnected from the watch."
        case .bluetoothUnavailable(let why): return why
        case .serviceNotFound: return "The File Transfer Service was not found on this device."
        case .watchRejected(let status): return "The watch rejected the request (status \(status.rawValue))."
        case .malformedResponse: return "Garbled response from the watch."
        case .digestMismatch: return "CRC mismatch after transfer — sync again."
        }
    }
}

// MARK: - Values decoded from responses

struct FTSEntry: Equatable {
    let name: String
    let isDirectory: Bool
    let fileSize: UInt32
    let modificationTime: UInt64 // ns since unix epoch
}

struct FTSReadChunk {
    let status: FTS.Status
    let chunkOffset: UInt32
    let totalLength: UInt32
    let data: Data
}

struct FTSDigest {
    let status: FTS.Status
    let fileSize: UInt32
    let crc32: UInt32
}

enum FTSListItem {
    case entry(FTSEntry)
    case done
}

// MARK: - Packet encoding / decoding

enum FTSPacket {

    // READ 0x10: {cmd, reserved, pathLength(2), chunkOffset(4), chunkSize(4), path}
    static func makeRead(path: String, offset: UInt32, size: UInt32) -> Data {
        var d = Data([FTS.Command.read.rawValue, 0])
        let p = Data(path.utf8)
        d.appendLE16(UInt16(p.count))
        d.appendLE32(offset)
        d.appendLE32(size)
        d.append(p)
        return d
    }

    // READ_PACING 0x12: {cmd, status=OK, reserved(2), chunkOffset(4), chunkSize(4)}
    static func makeReadPacing(offset: UInt32, size: UInt32) -> Data {
        var d = Data([FTS.Command.readPacing.rawValue, 0x01, 0, 0])
        d.appendLE32(offset)
        d.appendLE32(size)
        return d
    }

    // LISTDIR 0x50: {cmd, reserved, pathLength(2), path}
    static func makeListDir(path: String) -> Data {
        var d = Data([FTS.Command.listDir.rawValue, 0])
        let p = Data(path.utf8)
        d.appendLE16(UInt16(p.count))
        d.append(p)
        return d
    }

    // DIGEST 0x70: {cmd, reserved, pathLength(2), path}
    static func makeDigest(path: String) -> Data {
        var d = Data([FTS.Command.digest.rawValue, 0])
        let p = Data(path.utf8)
        d.appendLE16(UInt16(p.count))
        d.append(p)
        return d
    }

    // DELETE 0x30: {cmd, reserved, pathLength(2)} + path
    static func makeDelete(path: String) -> Data {
        var d = Data([FTS.Command.delete.rawValue, 0])
        let p = Data(path.utf8)
        d.appendLE16(UInt16(p.count))
        d.append(p)
        return d
    }

    // READ_DATA 0x11: {cmd, status, reserved(2), chunkOffset(4), totalLength(4), chunkLength(4), data}
    static func parseReadData(_ d: Data) -> FTSReadChunk? {
        guard d.count >= 16, d.first == FTS.Command.readData.rawValue,
              let status = FTS.Status(rawValue: d[1]) else { return nil }
        let chunkLength = Int(d.leU32(at: 12))
        guard d.count >= 16 + chunkLength else { return nil }
        return FTSReadChunk(status: status,
                            chunkOffset: d.leU32(at: 4),
                            totalLength: d.leU32(at: 8),
                            data: d.subdata(in: 16..<(16 + chunkLength)))
    }

    // LISTDIR entry 0x51: {cmd, status, pathLength(2), entryNumber(4), totalEntries(4),
    //                      flags(4), modificationTime(8), fileSize(4), name}
    static func parseListDirEntry(_ d: Data) -> FTSListItem? {
        guard d.count >= 28, d.first == FTS.Command.listDirEntry.rawValue,
              FTS.Status(rawValue: d[1]) != nil else { return nil }
        let pathLength = Int(d.leU16(at: 2))
        let entryNumber = d.leU32(at: 4)
        let totalEntries = d.leU32(at: 8)
        // Terminating entry: entryNumber == totalEntries && pathLength == 0
        if pathLength == 0 { return entryNumber == totalEntries ? .done : nil }
        guard d.count >= 28 + pathLength else { return nil }
        let name = String(decoding: d.subdata(in: 28..<(28 + pathLength)), as: UTF8.self)
        return .entry(FTSEntry(name: name,
                               isDirectory: (d.leU32(at: 12) & 0x1) != 0,
                               fileSize: d.leU32(at: 24),
                               modificationTime: d.leU64(at: 16)))
    }

    // DIGEST_STATUS 0x71: {cmd, status, reserved(2), fileSize(4), crc32(4)}
    static func parseDigestStatus(_ d: Data) -> FTSDigest? {
        guard d.count >= 12, d.first == FTS.Command.digestStatus.rawValue,
              let status = FTS.Status(rawValue: d[1]) else { return nil }
        return FTSDigest(status: status, fileSize: d.leU32(at: 4), crc32: d.leU32(at: 8))
    }

    /// Minimal {cmd, status} responses (DELETE 0x31, MKDIR 0x41, MOVE 0x61).
    static func parseSimpleStatus(_ d: Data, expected cmd: FTS.Command) -> FTS.Status? {
        guard d.count >= 2, d.first == cmd.rawValue else { return nil }
        return FTS.Status(rawValue: d[1])
    }
}

// MARK: - CRC-32 (IEEE 802.3 / zlib — matches the watch's DIGEST and Python zlib.crc32)

enum CRC32 {
    private static let table: [UInt32] = (0..<256).map { i -> UInt32 in
        var c = UInt32(i)
        for _ in 0..<8 {
            c = (c & 1) != 0 ? (c >> 1) ^ 0xEDB8_8320 : c >> 1
        }
        return c
    }

    static func compute(_ data: Data) -> UInt32 {
        var crc: UInt32 = 0xFFFF_FFFF
        for byte in data {
            crc = (crc >> 8) ^ table[Int((crc ^ UInt32(byte)) & 0xFF)]
        }
        return crc ^ 0xFFFF_FFFF
    }
}

// MARK: - Little-endian helpers (shared with the SLP1 parser)

extension Data {
    mutating func appendLE16(_ v: UInt16) {
        append(UInt8(v & 0xFF))
        append(UInt8((v >> 8) & 0xFF))
    }

    mutating func appendLE32(_ v: UInt32) {
        append(UInt8(v & 0xFF))
        append(UInt8((v >> 8) & 0xFF))
        append(UInt8((v >> 16) & 0xFF))
        append(UInt8((v >> 24) & 0xFF))
    }

    func leU16(at offset: Int) -> UInt16 {
        let i0 = index(startIndex, offsetBy: offset)
        let i1 = index(after: i0)
        return UInt16(self[i0]) | (UInt16(self[i1]) << 8)
    }

    func leU32(at offset: Int) -> UInt32 {
        let i0 = index(startIndex, offsetBy: offset)
        let i1 = index(after: i0)
        let i2 = index(after: i1)
        let i3 = index(after: i2)
        return UInt32(self[i0])
            | (UInt32(self[i1]) << 8)
            | (UInt32(self[i2]) << 16)
            | (UInt32(self[i3]) << 24)
    }

    func leU64(at offset: Int) -> UInt64 {
        UInt64(leU32(at: offset)) | (UInt64(leU32(at: offset + 4)) << 32)
    }
}
