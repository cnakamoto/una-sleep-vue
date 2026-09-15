//
//  BackupTransfer.swift
//  SleepVueSync
//
//  Manual backup export/import via the document picker — the one way an
//  app without the iCloud entitlement (free/personal developer teams) can
//  reach user-picked locations like iCloud Drive. See docs/adr/0004.
//
//  A backup is a plain folder ("SleepVue Backup") holding the raw
//  slp_YYYYMMDD.bin night files plus tombstones.json — the same files the
//  store keeps, so a backup is inspectable (and plottable on a Mac) as-is.
//  Import unions tombstones BEFORE copying nights and never imports a
//  tombstoned night, so deletions recorded in the backup are honored.
//

import Foundation

struct BackupImport {
    /// Night files to save: (fileName, data), already parsed and validated.
    var nights: [(fileName: String, data: Data)]
    /// Tombstones recorded in the backup (union these into TombstoneStore).
    var tombstones: Set<String>
}

enum BackupTransfer {

    static let backupFolderName = "SleepVue Backup"

    enum BackupError: LocalizedError {
        case notABackup

        var errorDescription: String? {
            switch self {
            case .notABackup:
                return "That folder doesn't look like a SleepVue backup (no slp_*.bin files, no tombstones.json)."
            }
        }
    }

    /// Copy nights + tombstones.json from the store directory into
    /// "<folder>/SleepVue Backup/". Returns the number of files written.
    /// Existing files are replaced — the store is the source of truth.
    @discardableResult
    static func writeBackup(of storeDirectory: URL, into folder: URL) throws -> Int {
        let fm = FileManager.default
        let backupDir = folder.appendingPathComponent(backupFolderName, isDirectory: true)
        try fm.createDirectory(at: backupDir, withIntermediateDirectories: true)

        let names = ((try? fm.contentsOfDirectory(atPath: storeDirectory.path)) ?? [])
            .filter { NightStore.dateKey(fromFileName: $0) != nil || $0 == TombstoneStore.fileName }
        var written = 0
        for name in names {
            let destination = backupDir.appendingPathComponent(name)
            if fm.fileExists(atPath: destination.path) {
                try fm.removeItem(at: destination)
            }
            try fm.copyItem(at: storeDirectory.appendingPathComponent(name), to: destination)
            written += 1
        }
        return written
    }

    /// Read a backup folder. Nights already stored locally are skipped, and
    /// nights tombstoned either locally or in the backup are never returned
    /// — a deletion always beats a copy.
    static func readBackup(at folder: URL, existing: Set<String>, tombstoned: Set<String>) throws -> BackupImport {
        let fm = FileManager.default
        let names = try fm.contentsOfDirectory(atPath: folder.path)

        var backupTombstones: Set<String> = []
        if names.contains(TombstoneStore.fileName),
           let data = fm.contents(atPath: folder.appendingPathComponent(TombstoneStore.fileName).path),
           let keys = try? JSONDecoder().decode([String].self, from: data) {
            backupTombstones = Set(keys)
        }

        let skip = existing.union(tombstoned).union(backupTombstones)
        var nights: [(String, Data)] = []
        for name in names.sorted() {
            guard let key = NightStore.dateKey(fromFileName: name), !skip.contains(key),
                  let data = fm.contents(atPath: folder.appendingPathComponent(name).path),
                  (try? NightFile.parse(data)) != nil // validate before trusting
            else { continue }
            nights.append((name, data))
        }

        if nights.isEmpty && backupTombstones.isEmpty {
            throw BackupError.notABackup
        }
        return BackupImport(nights: nights, tombstones: backupTombstones)
    }
}
