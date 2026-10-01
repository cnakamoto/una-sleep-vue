//
//  TombstoneStore.swift
//  SleepVueSync
//
//  Tombstones — dateKeys of nights the user deleted — live in
//  tombstones.json inside the night-store directory, so the store folder
//  is a complete backup unit: nights and deletions travel together,
//  whether through the iCloud container or a manual export/import
//  (docs/adr/0006 — this replaced an iCloud KVS store in v0.8.0 and
//  outlived the constraint that forced the move). A tombstoned night must
//  never reappear, so imports union tombstones before copying any night
//  files.
//
//  File format: a JSON array of dateKeys, e.g. ["20260903","20260904"].
//

import Foundation

final class TombstoneStore {

    static let fileName = "tombstones.json"

    /// Resolves the current store directory (NightStore may switch it
    /// between Documents and the iCloud container).
    private let directoryProvider: () -> URL

    init(directory: @escaping () -> URL) {
        directoryProvider = directory
    }

    private var fileURL: URL {
        directoryProvider().appendingPathComponent(Self.fileName)
    }

    /// All tombstoned dateKeys.
    func all() -> Set<String> {
        guard let data = try? Data(contentsOf: fileURL),
              let keys = try? JSONDecoder().decode([String].self, from: data)
        else { return [] }
        return Set(keys)
    }

    func insert(_ dateKey: String) {
        write(all().union([dateKey]))
    }

    /// Merge tombstones from a backup. Union only — importing never
    /// un-deletes.
    func union(_ dateKeys: Set<String>) {
        write(all().union(dateKeys))
    }

    /// One-time migration from the UserDefaults list used up to v0.7.1.
    func migrate(fromUserDefaultsKey defaultsKey: String) {
        let defaults = UserDefaults.standard
        guard let legacy = defaults.stringArray(forKey: defaultsKey) else { return }
        union(Set(legacy))
        defaults.removeObject(forKey: defaultsKey)
    }

    private func write(_ keys: Set<String>) {
        guard let data = try? JSONEncoder().encode(keys.sorted()) else { return }
        try? data.write(to: fileURL, options: .atomic)
    }
}
