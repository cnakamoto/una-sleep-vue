//
//  SleepVueSyncApp.swift
//  SleepVueSync
//

import SwiftUI

@main
struct SleepVueSyncApp: App {
    @StateObject private var state: AppState
    @Environment(\.scenePhase) private var scenePhase

    init() {
        let appState = AppState()
        _state = StateObject(wrappedValue: appState)
        // Registration is only valid during app launch.
        BackgroundSync.register(appState: appState)
        BackgroundSync.schedule()
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(state)
        }
        .onChange(of: scenePhase) {
            if scenePhase == .background {
                BackgroundSync.schedule()
            }
        }
    }
}
