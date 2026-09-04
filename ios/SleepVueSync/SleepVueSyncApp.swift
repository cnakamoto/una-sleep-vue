//
//  SleepVueSyncApp.swift
//  SleepVueSync
//

import SwiftUI

@main
struct SleepVueSyncApp: App {
    @StateObject private var state = AppState()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(state)
        }
    }
}
