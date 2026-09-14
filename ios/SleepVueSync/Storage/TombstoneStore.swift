//
//  TombstoneStore.swift
//  SleepVueSync
//
//  Tombstones — dateKeys of nights the user deleted — live in iCloud's
//  ubiquitous key-value store, one key per night ("tombstone.20260903").
//  They survive app deletion/reinstall with the user's Apple account, so a
//  deleted night stays deleted; per-key storage union-merges across
//  devices, so a delete anywhere becomes a delete everywhere, eventually.
//  (1 MB KVS limit ÷ ~15 bytes per tombstone = decades of headroom.)
//

import Foundation

final class TombstoneStore {

    private static let keyPrefix = "tombstone."

    private let store = NSUbiquitousKeyValueStore.default

    /// All tombstoned dateKeys currently known (local + synced from iCloud).
    func all() -> Set<String> {
        Set(store.dictionaryRepresentation.keys.compactMap { key in
            guard key.hasPrefix(Self.keyPrefix) else { return nil }
            return String(key.dropFirst(Self.keyPrefix.count))
        })
    }

    func insert(_ dateKey: String) {
        store.set(true, forKey: Self.keyPrefix + dateKey)
    }

    /// One-time migration from the UserDefaults list used up to v0.7.1.
    func migrate(fromUserDefaultsKey defaultsKey: String) {
        let defaults = UserDefaults.standard
        guard let legacy = defaults.stringArray(forKey: defaultsKey) else { return }
        for dateKey in legacy { insert(dateKey) }
        defaults.removeObject(forKey: defaultsKey)
    }
}
