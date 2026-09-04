//
//  ContentView.swift
//  SleepVueSync
//

import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var state: AppState

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

                Section("Apple Health") {
                    Toggle("Write nights to Apple Health", isOn: Binding(
                        get: { state.healthExportEnabled },
                        set: { state.setHealthExport($0) }
                    ))
                    if !state.healthNote.isEmpty {
                        Text(state.healthNote)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }

                Section {
                    Toggle("Delete from watch after sync", isOn: Binding(
                        get: { state.pruneAfterSync },
                        set: { state.setPrune($0) }
                    ))
                } header: {
                    Text("Watch")
                } footer: {
                    Text("Archived nights are removed from the watch only after a verified transfer. The watch keeps its own on-device summaries and history.")
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
                            for i in offsets { state.delete(state.nights[i]) }
                        }
                    }
                }

                Section {
                    DisclosureGroup("BLE debug log") {
                        if state.client.debugLog.isEmpty {
                            Text("No events yet.")
                                .font(.caption2.monospaced())
                                .foregroundStyle(.secondary)
                        } else {
                            ForEach(Array(state.client.debugLog.enumerated()), id: \.offset) { _, line in
                                Text(line)
                                    .font(.caption2.monospaced())
                                    .frame(maxWidth: .infinity, alignment: .leading)
                                    .textSelection(.enabled)
                            }
                            Button("Clear log", role: .destructive) {
                                state.client.clearDebugLog()
                            }
                            .font(.caption)
                        }
                    }
                } footer: {
                    Text("Persists across launches, so background syncs leave a trail. Background refresh runs a few times a day at iOS's discretion.")
                }
            }
            .navigationTitle("SleepVue")
            .navigationDestination(for: Night.self) { night in
                NightDetailView(night: night)
            }
            .alert("Sync problem", isPresented: .constant(state.errorMessage != nil)) {
                Button("OK") { state.errorMessage = nil }
            } message: {
                Text(state.errorMessage ?? "")
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
