//
//  NightStore.swift
//  SleepVueSync
//
//  Local persistence for synced night files. Raw .bin files are kept in
//  Documents/SleepNights/ (so nothing is lost if the on-flash layout ever
//  changes) and parsed into Night values on load — a night is ~4 KB, so
//  re-parsing on launch is free.
//

import Foundation

final class NightStore {

    private var directory: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("SleepNights", isDirectory: true)
    }

    private func ensureDirectory() throws {
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    }

    /// dateKeys (e.g. "20260903") already stored locally.
    func storedDateKeys() -> Set<String> {
        let names = (try? FileManager.default.contentsOfDirectory(atPath: directory.path)) ?? []
        return Set(names.compactMap { Self.dateKey(fromFileName: $0) })
    }

    func save(_ data: Data, fileName: String) throws {
        try ensureDirectory()
        try data.write(to: directory.appendingPathComponent(fileName), options: .atomic)
    }

    func loadAll() -> [Night] {
        let names = (try? FileManager.default.contentsOfDirectory(atPath: directory.path)) ?? []
        return names
            .filter { $0.hasPrefix("slp_") && $0.hasSuffix(".bin") }
            .compactMap { name -> Night? in
                let url = directory.appendingPathComponent(name)
                guard let data = try? Data(contentsOf: url) else { return nil }
                return try? NightFile.parse(data)
            }
            .sorted { $0.header.dateKey > $1.header.dateKey } // newest first
    }

    func delete(_ night: Night) throws {
        let name = "slp_\(night.header.dateKey).bin"
        try FileManager.default.removeItem(at: directory.appendingPathComponent(name))
    }

    /// "slp_20260903.bin" → "20260903"
    static func dateKey(fromFileName name: String) -> String? {
        guard name.hasPrefix("slp_"), name.hasSuffix(".bin") else { return nil }
        let key = name.dropFirst(4).dropLast(4)
        return key.count == 8 && key.allSatisfy(\.isNumber) ? String(key) : nil
    }
}
