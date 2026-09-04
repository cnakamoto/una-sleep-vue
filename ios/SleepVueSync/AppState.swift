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

    /// SleepVue stores its nights in its app-private dir, visible over FTS here.
    static let watchDir = "/Apps/SleepVue"

    private let store = NightStore()
    private var cancellables: Set<AnyCancellable> = []

    init() {
        // Surface FTSClient's @Published changes through AppState so views
        // only need to observe one object.
        client.objectWillChange
            .sink { [weak self] in self?.objectWillChange.send() }
            .store(in: &cancellables)
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

        do {
            if client.phase != .ready {
                progressLabel = "Connecting…"
                try await client.connect()
            }

            progressLabel = "Listing nights on watch…"
            let entries = try await client.listDir(Self.watchDir + "/")
            let nightFiles = entries.filter { NightStore.dateKey(fromFileName: $0.name) != nil }
            let known = store.storedDateKeys()
            let newFiles = nightFiles.filter { entry in
                guard let key = NightStore.dateKey(fromFileName: entry.name) else { return false }
                return !known.contains(key)
            }

            for (index, entry) in newFiles.enumerated() {
                let path = Self.watchDir + "/" + entry.name
                progressLabel = "Night \(index + 1) of \(newFiles.count): \(entry.name)"
                let data = try await client.readFile(path) { [weak self] fraction in
                    Task { @MainActor in self?.progress = fraction }
                }

                // Validate before trusting anything.
                _ = try NightFile.parse(data)

                // Cheap integrity proof without read-back (protocol v5+).
                if client.protocolVersion >= 5 {
                    let digest = try await client.digest(path)
                    guard digest.fileSize == data.count, digest.crc32 == CRC32.compute(data) else {
                        throw FTSError.digestMismatch
                    }
                }

                try store.save(data, fileName: entry.name)
            }

            progressLabel = newFiles.isEmpty ? "Up to date" : "Synced \(newFiles.count) night(s)"
            progress = 1
            reloadLocal()
        } catch {
            errorMessage = describe(error)
            progressLabel = ""
        }
    }

    func delete(_ night: Night) {
        try? store.delete(night)
        reloadLocal()
    }

    private func describe(_ error: Error) -> String {
        if let e = error as? NightParseError {
            return "Unreadable night file (\(e)) — watch app newer than this app?"
        }
        return error.localizedDescription
    }
}
