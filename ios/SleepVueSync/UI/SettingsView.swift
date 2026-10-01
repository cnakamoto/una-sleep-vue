//
//  SettingsView.swift
//  SleepVueSync
//
//  Modal Settings sheet (gear on the home screen): Apple Health export,
//  watch prune, manual backup, and the BLE debug log. Everything here is
//  configuration or diagnostics — the sync action itself stays on the home
//  screen.
//

import SwiftUI

struct SettingsView: View {
    @EnvironmentObject private var state: AppState
    @Environment(\.dismiss) private var dismiss
    @State private var pickingBackupFolder: ((URL?) -> Void)?

    var body: some View {
        NavigationStack {
            Form {
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

                Section {
                    HStack {
                        Image(systemName: state.backupAvailable ? "checkmark.icloud" : "icloud.slash")
                            .foregroundStyle(state.backupAvailable ? Color.accentColor : .secondary)
                            .accessibilityHidden(true)
                        Text(state.backupAvailable ? "Backed up to iCloud Drive" : "Not backed up to iCloud")
                        Spacer()
                    }
                    Button("Export backup…") {
                        pickingBackupFolder = { url in
                            pickingBackupFolder = nil
                            if let url { state.exportBackup(to: url) }
                        }
                    }
                    Button("Import backup…") {
                        pickingBackupFolder = { url in
                            pickingBackupFolder = nil
                            if let url { state.importBackup(from: url) }
                        }
                    }
                    if !state.backupNote.isEmpty {
                        Text(state.backupNote)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                } header: {
                    Text("Backup")
                } footer: {
                    Text(state.backupAvailable
                         ? "Nights are saved straight into iCloud Drive, so they restore by themselves after a reinstall — there is nothing to press. Deleting a night deletes the backup copy too. Export writes a separate copy to a folder you choose, for keeping nights outside iCloud."
                         : "Sign in to iCloud in Settings to back nights up automatically. Until then they live only on this phone — export writes a copy to a folder you choose, and import restores it.")
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
                } header: {
                    Text("Diagnostics")
                } footer: {
                    Text("Persists across launches, so background syncs leave a trail. Background refresh runs a few times a day at iOS's discretion.")
                }
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
            .sheet(isPresented: Binding(
                get: { pickingBackupFolder != nil },
                set: { if !$0 { pickingBackupFolder = nil } }
            )) {
                if let onPick = pickingBackupFolder {
                    FolderPicker(onPick: onPick)
                        .ignoresSafeArea()
                }
            }
        }
    }
}
