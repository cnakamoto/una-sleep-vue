//
//  NightStore.swift
//  SleepVueSync
//
//  Local persistence for synced night files. Raw .bin files are kept as the
//  source of truth — a night is ~4 KB, so re-parsing on launch is free.
//
//  When the user is signed into iCloud the store lives in the app's iCloud
//  Drive container (visible in Files ▸ iCloud Drive ▸ SleepVue): writing a
//  file IS the backup — there is no separate upload step — and after an
//  app reinstall the nights simply download again (see the metadata query
//  below). Signed out of iCloud, it falls back to plain
//  Documents/SleepNights/, exactly as before, and migrates into the
//  container when iCloud becomes available.
//

import Foundation

@MainActor
final class NightStore {

    /// Called on the main queue when the container reports arrivals,
    /// download completions, or removals — the caller should reload.
    var onRemoteChange: (() -> Void)?

    /// True once the store root is the iCloud container.
    private(set) var usingICloud = false

    /// Local fallback; also where pre-iCloud installs keep their nights.
    private var localDirectory: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("SleepNights", isDirectory: true)
    }

    /// Active store root. Starts local; activateICloudIfAvailable() may
    /// switch it into the container (never back within a session).
    private var directory: URL

    private var metadataQuery: NSMetadataQuery?
    private var queryObservers: [NSObjectProtocol] = []

    init() {
        directory = localDirectory
    }

    deinit {
        metadataQuery?.stop()
        for observer in queryObservers {
            NotificationCenter.default.removeObserver(observer)
        }
    }

    /// Switch the store root into the iCloud Drive container when the user
    /// is signed in: migrate any locally-stored nights in, then watch the
    /// container for arrivals (restore after a reinstall) and request
    /// downloads for not-yet-local files. Safe to call repeatedly (e.g. on
    /// returning to foreground) — once active it only re-requests pending
    /// downloads.
    func activateICloudIfAvailable() async {
        guard !usingICloud else {
            requestPendingDownloads()
            return
        }

        // url(forUbiquityContainerIdentifier:) can block briefly — keep it
        // off the main thread.
        let container = await Task.detached(priority: .utility) {
            FileManager.default.url(forUbiquityContainerIdentifier: nil)
        }.value
        guard let container else { return }

        // Only the container's Documents/ tree is user-visible (and is the
        // part covered by the ubiquitous documents scope).
        let cloudDirectory = container
            .appendingPathComponent("Documents", isDirectory: true)
            .appendingPathComponent("SleepNights", isDirectory: true)
        do {
            try FileManager.default.createDirectory(at: cloudDirectory, withIntermediateDirectories: true)
            migrateLocalNights(into: cloudDirectory)
        } catch {
            return // stay local this session; try again next launch
        }

        directory = cloudDirectory
        usingICloud = true
        startMetadataQuery()
    }

    /// Move locally-stored nights (pre-iCloud installs, or nights synced
    /// while signed out) into the container. On a name clash the container
    /// copy wins: both came from the same watch, so they are byte-identical.
    private func migrateLocalNights(into cloudDirectory: URL) {
        let fm = FileManager.default
        let names = (try? fm.contentsOfDirectory(atPath: localDirectory.path)) ?? []
        for name in names where name.hasPrefix("slp_") && name.hasSuffix(".bin") {
            let source = localDirectory.appendingPathComponent(name)
            let destination = cloudDirectory.appendingPathComponent(name)
            if fm.fileExists(atPath: destination.path) {
                try? fm.removeItem(at: source)
            } else {
                try? fm.moveItem(at: source, to: destination)
            }
        }
    }

    /// Watch the container: request downloads for nights that exist only in
    /// iCloud (fresh reinstall, or files evicted under disk pressure) and
    /// report changes so the UI reloads.
    private func startMetadataQuery() {
        let query = NSMetadataQuery()
        query.searchScopes = [NSMetadataQueryUbiquitousDocumentsScope]
        query.predicate = NSPredicate(format: "%K LIKE %@", NSMetadataItemFSNameKey, "slp_*.bin")
        metadataQuery = query

        let center = NotificationCenter.default
        for name in [Notification.Name.NSMetadataQueryDidFinishGathering, .NSMetadataQueryDidUpdate] {
            queryObservers.append(center.addObserver(forName: name, object: query, queue: .main) { [weak self] _ in
                Task { @MainActor in self?.processQueryResults() }
            })
        }
        query.start()
    }

    private func processQueryResults() {
        guard let query = metadataQuery else { return }
        query.disableUpdates()
        defer { query.enableUpdates() }
        requestPendingDownloads()
        onRemoteChange?()
    }

    private func requestPendingDownloads() {
        guard let query = metadataQuery else { return }
        for result in query.results {
            guard let item = result as? NSMetadataItem,
                  let url = item.value(forAttribute: NSMetadataItemURLKey) as? URL,
                  let status = item.value(forAttribute: NSMetadataUbiquitousItemDownloadingStatusKey) as? String,
                  status != NSMetadataUbiquitousItemDownloadingStatusCurrent
            else { continue }
            try? FileManager.default.startDownloadingUbiquitousItem(at: url)
        }
    }

    /// dateKeys (e.g. "20260903") already stored locally. Non-downloaded
    /// iCloud placeholders don't match (they list as ".slp_….bin.icloud"),
    /// so a BLE sync racing a restore may re-fetch the same night — harmless:
    /// same watch, byte-identical content, last write wins.
    func storedDateKeys() -> Set<String> {
        let names = (try? FileManager.default.contentsOfDirectory(atPath: directory.path)) ?? []
        return Set(names.compactMap { Self.dateKey(fromFileName: $0) })
    }

    func save(_ data: Data, fileName: String) throws {
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
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

    /// Removes the file. In the iCloud container the deletion propagates to
    /// the backup automatically — mirror semantics, delete means delete.
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
