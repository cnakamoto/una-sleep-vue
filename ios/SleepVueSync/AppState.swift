//
//  AppState.swift
//  SleepVueSync
//
//  Owns the FTS client and the local store; drives the sync flow:
//  LISTDIR /Apps/SleepVue/ → download each night we don't have yet →
//  parse → DIGEST-verify (protocol v5) → persist → refresh the list.
//

import Combine
import Foundation

@MainActor
final class AppState: ObservableObject {

    let client = FTSClient()

    @Published private(set) var nights: [Night] = []
    @Published private(set) var syncing = false
    @Published private(set) var progress: Double = 0
    @Published private(set) var progressLabel = ""
    @Published var errorMessage: String?

    // Apple Health export (write-only). Enabling prompts for permission and
    // back-exports all synced nights; syncs export new nights automatically.
    @Published private(set) var healthExportEnabled: Bool
    @Published private(set) var healthNote = ""

    // Prune: DELETE archived nights from the watch once they're verified on
    // the phone. Off by default — destructive. On-watch summaries/history
    // are unaffected (the GUI reads slp_last/slp_idx, never the archives).
    @Published private(set) var pruneAfterSync: Bool

    // iCloud backup: true when the night store lives in the iCloud Drive
    // container. Status display only — writes to the container ARE the
    // backup, there is nothing to push manually.
    @Published private(set) var backupAvailable = false

    /// SleepVue stores its nights in its app-private dir, visible over FTS here.
    static let watchDir = "/Apps/SleepVue"

    private let store = NightStore()
    private let tombstoneStore = TombstoneStore()
    private let health = HealthKitExporter()
    private var cancellables: Set<AnyCancellable> = []

    private static let healthEnabledKey = "sleepvue.healthExportEnabled"
    private static let healthExportedKey = "sleepvue.healthExportedDateKeys"
    private static let pruneEnabledKey = "sleepvue.pruneAfterSync"
    /// Legacy UserDefaults tombstone list (≤ v0.7.1) — migration source only.
    private static let deletedKey = "sleepvue.deletedDateKeys"

    /// dateKeys the user deleted — never sync these again, even if the file
    /// is still on the watch (e.g. watch was offline at delete time). Backed
    /// up via iCloud KVS so deletions survive an app reinstall.
    private var deletedDateKeys: Set<String> {
        tombstoneStore.all()
    }

    /// dateKeys (as strings) already pushed to HealthKit. Local marker only —
    /// the sync identifiers make Health-side duplicates impossible anyway.
    private var exportedDateKeys: Set<String> {
        get { Set(UserDefaults.standard.stringArray(forKey: Self.healthExportedKey) ?? []) }
        set { UserDefaults.standard.set(Array(newValue), forKey: Self.healthExportedKey) }
    }

    init() {
        healthExportEnabled = UserDefaults.standard.bool(forKey: Self.healthEnabledKey)
        pruneAfterSync = UserDefaults.standard.bool(forKey: Self.pruneEnabledKey)
        // Surface FTSClient's @Published changes through AppState so views
        // only need to observe one object.
        client.objectWillChange
            .sink { [weak self] in self?.objectWillChange.send() }
            .store(in: &cancellables)

        tombstoneStore.migrate(fromUserDefaultsKey: Self.deletedKey)

        // iCloud arrivals: nights restored onto a fresh install, or files
        // removed elsewhere — reload, then let tombstones win.
        store.onRemoteChange = { [weak self] in
            Task { @MainActor in
                self?.reloadLocal()
                self?.reconcileTombstones()
            }
        }
        // Tombstones synced from iCloud (fresh install, or a delete made on
        // another device) — remove any matching local nights.
        NotificationCenter.default.addObserver(
            forName: NSUbiquitousKeyValueStore.didChangeExternallyNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            Task { @MainActor in self?.reconcileTombstones() }
        }

        reloadLocal()

        // Move the store into iCloud (migrating any existing nights) before
        // the first sync, so downloads land directly in the backup.
        Task {
            await store.activateICloudIfAvailable()
            backupAvailable = store.usingICloud
            reconcileTombstones()
            reloadLocal()
        }
    }

    /// Re-check iCloud on returning to the foreground — picks up an account
    /// sign-in that happened while the app was running.
    func refreshBackup() async {
        await store.activateICloudIfAvailable()
        backupAvailable = store.usingICloud
    }

    /// Tombstones always win, eventually: a night that was re-fetched over
    /// BLE before its tombstone arrived from iCloud (or deleted on another
    /// device) is removed locally as soon as the tombstone shows up.
    private func reconcileTombstones() {
        let tombstoned = deletedDateKeys
        let stale = nights.filter { tombstoned.contains(String($0.header.dateKey)) }
        guard !stale.isEmpty else { return }
        for night in stale { try? store.delete(night) }
        reloadLocal()
    }

    func reloadLocal() {
        nights = store.loadAll()
    }

    func connect() async {
        errorMessage = nil
        do { try await client.connect() }
        catch is CancellationError { /* user cancelled — nothing to report */ }
        catch { errorMessage = describe(error) }
    }

    func cancel() {
        client.cancelConnect()
    }

    func sync() async {
        guard !syncing else { return }
        errorMessage = nil
        syncing = true
        progress = 0
        defer { syncing = false }
        DebugLog.write("sync started")

        do {
            if client.phase != .ready {
                progressLabel = "Connecting…"
                try await client.connect()
            }

            progressLabel = "Listing nights on watch…"
            let entries = try await client.listDir(Self.watchDir + "/")
            let nightFiles = entries.filter { NightStore.dateKey(fromFileName: $0.name) != nil }
            let known = store.storedDateKeys()
            let tombstoned = deletedDateKeys
            let newFiles = nightFiles.filter { entry in
                guard let key = NightStore.dateKey(fromFileName: entry.name) else { return false }
                return !known.contains(key) && !tombstoned.contains(key)
            }

            var downloaded: [Night] = []
            for (index, entry) in newFiles.enumerated() {
                let path = Self.watchDir + "/" + entry.name
                progressLabel = "Night \(index + 1) of \(newFiles.count): \(entry.name)"
                let data = try await client.readFile(path) { [weak self] fraction in
                    Task { @MainActor in self?.progress = fraction }
                }

                // Validate before trusting anything.
                let night = try NightFile.parse(data)

                // Cheap integrity proof without read-back (protocol v5+).
                if client.protocolVersion >= 5 {
                    let digest = try await client.digest(path)
                    guard digest.fileSize == data.count, digest.crc32 == CRC32.compute(data) else {
                        throw FTSError.digestMismatch
                    }
                }

                try store.save(data, fileName: entry.name)
                downloaded.append(night)
            }

            var summary = newFiles.isEmpty ? "Up to date" : "Synced \(newFiles.count) night(s)"
            progressLabel = summary
            progress = 1
            reloadLocal()

            if healthExportEnabled, !downloaded.isEmpty {
                progressLabel = "Writing to Apple Health…"
                await exportToHealth(downloaded)
            }

            // Prune AFTER the data is verified locally and pushed to Health.
            var pruned = 0
            if pruneAfterSync {
                for night in downloaded {
                    let path = Self.watchDir + "/slp_\(night.header.dateKey).bin"
                    do {
                        try await client.deleteFile(path)
                        pruned += 1
                    } catch {
                        // Per-file failure is logged in the BLE debug log;
                        // the file simply stays on the watch.
                    }
                }
                if pruned > 0 { summary += " · deleted \(pruned) from watch" }
            }
            progressLabel = summary
            DebugLog.write("sync ok: \(summary)")
        } catch {
            errorMessage = describe(error)
            progressLabel = ""
            DebugLog.write("sync failed: \(error.localizedDescription)")
        }
    }

    func setPrune(_ enabled: Bool) {
        pruneAfterSync = enabled
        UserDefaults.standard.set(enabled, forKey: Self.pruneEnabledKey)
    }

    /// Toggle from the UI. Enabling asks for HealthKit write access and
    /// back-exports any synced nights not yet written.
    func setHealthExport(_ enabled: Bool) {
        guard enabled else {
            healthExportEnabled = false
            UserDefaults.standard.set(false, forKey: Self.healthEnabledKey)
            healthNote = ""
            return
        }
        guard HealthKitExporter.isAvailable else {
            healthNote = HealthExportError.unavailable.localizedDescription
            return
        }
        Task {
            do {
                if try await health.requestWriteAuthorization() {
                    healthExportEnabled = true
                    UserDefaults.standard.set(true, forKey: Self.healthEnabledKey)
                    healthNote = ""
                    await exportToHealth(nights)
                } else {
                    healthNote = HealthExportError.notAuthorized.localizedDescription
                }
            } catch {
                healthNote = error.localizedDescription
            }
        }
    }

    /// Export nights not yet marked exported; marks on success. Health
    /// failures are reported in healthNote, never thrown into the sync flow.
    private func exportToHealth(_ candidates: [Night]) async {
        guard health.canWrite() else {
            healthNote = HealthExportError.notAuthorized.localizedDescription
            return
        }
        var done = exportedDateKeys
        var exportedCount = 0
        for night in candidates {
            let key = String(night.header.dateKey)
            guard !done.contains(key) else { continue }
            do {
                try await health.export(night)
                done.insert(key)
                exportedCount += 1
            } catch {
                healthNote = "Health export failed for \(night.displayDate): \(error.localizedDescription)"
            }
        }
        exportedDateKeys = done
        if exportedCount > 0 {
            healthNote = "\(exportedCount) night(s) written to Apple Health"
        }
    }

    /// Delete a night everywhere we can: local copy (which also removes the
    /// iCloud backup copy — delete means delete), watch copy (if the watch
    /// is connected right now), and tombstone the date so it never re-syncs.
    /// Health data already exported stays (write-only access).
    func delete(_ night: Night) {
        let key = String(night.header.dateKey)
        try? store.delete(night)

        tombstoneStore.insert(key)

        var exported = exportedDateKeys
        exported.remove(key)
        exportedDateKeys = exported

        reloadLocal()

        if client.phase == .ready {
            let path = Self.watchDir + "/slp_\(night.header.dateKey).bin"
            Task { try? await client.deleteFile(path) }
        }
    }

    private func describe(_ error: Error) -> String {
        if let e = error as? NightParseError {
            return "Unreadable night file (\(e)) — watch app newer than this app?"
        }
        return error.localizedDescription
    }
}
