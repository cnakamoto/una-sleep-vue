//
//  HealthKitExporter.swift
//  SleepVueSync
//
//  Writes nights to Apple Health as HKCategoryTypeIdentifierSleepAnalysis:
//  one .inBed sample over bed→wake plus one sample per stage run
//  (AWAKE → .awake, LIGHT → .asleepCore, DEEP → .asleepDeep) — the same
//  shape watchOS itself writes.
//
//  Every sample carries HKMetadataKeySyncIdentifier + SyncVersion, keyed per
//  night and run, so re-exporting the same night is a no-op in HealthKit —
//  export is idempotent by construction. Write-only: the app never reads
//  Health data.
//

import HealthKit

enum HealthExportError: Error, LocalizedError {
    case unavailable
    case notAuthorized

    var errorDescription: String? {
        switch self {
        case .unavailable: return "Apple Health is not available on this device."
        case .notAuthorized: return "No permission to write to Apple Health — enable it in Settings → Health → Data Access & Devices → SleepVue."
        }
    }
}

final class HealthKitExporter {

    static var isAvailable: Bool { HKHealthStore.isHealthDataAvailable() }

    private let store = HKHealthStore()
    private let sleepType = HKCategoryType(.sleepAnalysis)

    /// Ask for write-only access. Returns true if we may write sleep data.
    func requestWriteAuthorization() async throws -> Bool {
        guard Self.isAvailable else { throw HealthExportError.unavailable }
        // Request succeeds even if the user declines — check status after.
        try await store.requestAuthorization(toShare: [sleepType], read: [])
        return store.authorizationStatus(for: sleepType) == .sharingAuthorized
    }

    func canWrite() -> Bool {
        store.authorizationStatus(for: sleepType) == .sharingAuthorized
    }

    /// Write one night. Safe to call repeatedly — sync identifiers make
    /// duplicates impossible.
    func export(_ night: Night) async throws {
        guard !night.epochs.isEmpty else { return }

        let device = HKDevice(name: "UNA Watch",
                              manufacturer: "UNA Watch Ltd",
                              model: "UNA Watch",
                              hardwareVersion: nil,
                              firmwareVersion: nil,
                              softwareVersion: nil,
                              localIdentifier: nil,
                              udiDeviceIdentifier: nil)
        let timeZone = TimeZone.current.identifier
        let keyPrefix = "sleepvue.\(night.header.dateKey)"

        func sample(_ value: HKCategoryValueSleepAnalysis,
                    from start: Date, to end: Date, id: String) -> HKCategorySample {
            HKCategorySample(type: sleepType, value: value.rawValue, start: start, end: end,
                             device: device, metadata: [
                                 HKMetadataKeySyncIdentifier: id,
                                 HKMetadataKeySyncVersion: 1,
                                 HKMetadataKeyTimeZone: timeZone,
                             ])
        }

        var samples: [HKCategorySample] = [
            sample(.inBed, from: night.bed, to: night.wake, id: "\(keyPrefix).inbed")
        ]
        for (index, run) in night.stageRuns.enumerated() {
            let value: HKCategoryValueSleepAnalysis
            switch run.stage {
            case .awake: value = .awake
            case .light: value = .asleepCore
            case .deep: value = .asleepDeep
            }
            samples.append(sample(value, from: run.start, to: run.end,
                                  id: "\(keyPrefix).run.\(index)"))
        }

        try await store.save(samples)
    }
}
