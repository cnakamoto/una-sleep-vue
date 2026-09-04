//
//  BackgroundSync.swift
//  SleepVueSync
//
//  Periodic background sync via BGAppRefreshTask. iOS picks the actual run
//  times (usage-based); we request every ~3 h. On fire we run the normal
//  sync() path — the saved-identifier / retrieveConnectedPeripherals
//  discovery means no advertisement is needed, so a headless connect works
//  whenever the watch is in range (iOS holds its ANCS link anyway).
//
//  Info.plist requirements (already wired): UIBackgroundModes fetch +
//  bluetooth-central, BGTaskSchedulerPermittedIdentifiers with `identifier`.
//

import BackgroundTasks
import Foundation

enum BackgroundSync {

    static let identifier = "com.sleepvue.sync.refresh"
    private static let refreshInterval: TimeInterval = 3 * 3600

    /// Must be called during app launch (registration is only valid then).
    static func register(appState: AppState) {
        BGTaskScheduler.shared.register(forTaskWithIdentifier: identifier, using: nil) { task in
            DebugLog.write("BG refresh fired")
            schedule() // chain the next one
            task.expirationHandler = {
                DebugLog.write("BG refresh expired — bailing")
                Task { @MainActor in appState.client.disconnect() }
            }
            Task { @MainActor in
                await appState.sync()
                DebugLog.write("BG refresh done")
                task.setTaskCompleted(success: true)
            }
        }
    }

    static func schedule() {
        let request = BGAppRefreshTaskRequest(identifier: identifier)
        request.earliestBeginDate = Date(timeIntervalSinceNow: refreshInterval)
        do {
            try BGTaskScheduler.shared.submit(request)
        } catch {
            DebugLog.write("BG refresh schedule failed: \(error.localizedDescription)")
        }
    }
}
