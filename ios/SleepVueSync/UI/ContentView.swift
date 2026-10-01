//
//  ContentView.swift
//  SleepVueSync
//
//  Home screen: connection + sync, the Latest night card, and the list of
//  synced nights. Configuration and diagnostics live in SettingsView
//  (gear, top right); nights open in NightPagerView.
//

import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var state: AppState
    @State private var nightPendingDelete: Night?
    @State private var showingSettings = false

    var body: some View {
        NavigationStack {
            List {
                connectionSection

                if state.syncing || !state.progressLabel.isEmpty {
                    Section {
                        VStack(alignment: .leading, spacing: 6) {
                            Text(state.progressLabel).font(.callout)
                            if state.syncing {
                                ProgressView(value: state.progress)
                            }
                        }
                    }
                }

                // Hidden when there are no nights — the list below already
                // carries the single empty-state message.
                if let latest = state.nights.first {
                    Section("Latest night") {
                        NavigationLink(value: latest) {
                            LatestNightCard(night: latest)
                        }
                    }
                }

                Section("Synced nights") {
                    if state.nights.isEmpty {
                        Text("No nights yet. Connect and sync to pull sleep data from your watch.")
                            .foregroundStyle(.secondary)
                    } else {
                        ForEach(state.nights, id: \.header.dateKey) { night in
                            NavigationLink(value: night) {
                                NightRow(night: night)
                            }
                        }
                        .onDelete { offsets in
                            if let first = offsets.first {
                                nightPendingDelete = state.nights[first]
                            }
                        }
                    }
                }
            }
            .navigationTitle("SleepVue")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button {
                        showingSettings = true
                    } label: {
                        Image(systemName: "gearshape")
                    }
                    .accessibilityLabel("Settings")
                }
            }
            .navigationDestination(for: Night.self) { night in
                NightPagerView(initial: night)
            }
            .sheet(isPresented: $showingSettings) {
                SettingsView()
            }
            .alert("Sync problem", isPresented: .constant(state.errorMessage != nil)) {
                Button("OK") { state.errorMessage = nil }
            } message: {
                Text(state.errorMessage ?? "")
            }
            .confirmationDialog(
                "Delete \(nightPendingDelete?.displayDate ?? "this night")?",
                isPresented: Binding(
                    get: { nightPendingDelete != nil },
                    set: { if !$0 { nightPendingDelete = nil } }
                ),
                titleVisibility: .visible
            ) {
                Button("Delete night", role: .destructive) {
                    if let night = nightPendingDelete { state.delete(night) }
                    nightPendingDelete = nil
                }
                Button("Cancel", role: .cancel) { nightPendingDelete = nil }
            } message: {
                Text("Removes it from this phone and stops it from syncing again — restoring a backup won't bring it back. "
                    + "If the watch is connected, its copy is deleted too. "
                    + "Anything already exported stays in Apple Health (SleepVue has write-only access).")
            }
        }
    }

    private var connectionSection: some View {
        Section {
            HStack {
                Circle()
                    .fill(statusColor)
                    .frame(width: 10, height: 10)
                Text(statusText)
                Spacer()
                if isConnecting {
                    Button("Cancel", role: .cancel) {
                        state.cancel()
                    }
                } else if state.client.phase == .ready {
                    Button(state.syncing ? "Syncing…" : "Sync now") {
                        Task { await state.sync() }
                    }
                    .disabled(state.syncing)
                } else {
                    Button("Connect") {
                        Task { await state.connect() }
                    }
                }
            }
        } footer: {
            if case .ready = state.client.phase {
                Text("FTS protocol v\(state.client.protocolVersion)")
            }
        }
    }

    private var isConnecting: Bool {
        switch state.client.phase {
        case .starting, .scanning, .connecting, .handshaking: return true
        default: return false
        }
    }

    private var statusColor: Color {
        switch state.client.phase {
        case .ready: return .green
        case .starting, .scanning, .connecting, .handshaking: return .orange
        case .failed: return .red
        case .idle: return .gray
        }
    }

    private var statusText: String {
        switch state.client.phase {
        case .idle: return "Not connected"
        case .starting: return "Starting Bluetooth…"
        case .scanning: return "Looking for UNA Watch…"
        case .connecting: return "Connecting…"
        case .handshaking: return "Pairing…"
        case .ready: return state.client.deviceName.map { "Connected: \($0)" } ?? "Connected"
        case .failed(let why): return why
        }
    }
}

private struct NightRow: View {
    let night: Night

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(night.displayDate).font(.headline)
                Spacer()
                Text(minutesText(night.totalMinutes))
                    .font(.subheadline.weight(.medium))
            }
            HStack(spacing: 12) {
                Text("\(timeText(night.bed)) → \(timeText(night.wake))")
                Spacer()
                Text("Deep \(minutesText(night.minutes(of: .deep)))")
            }
            .font(.caption)
            .foregroundStyle(.secondary)
        }
        .padding(.vertical, 2)
    }
}
