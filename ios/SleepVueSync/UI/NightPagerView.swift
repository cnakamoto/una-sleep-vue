//
//  NightPagerView.swift
//  SleepVueSync
//
//  Horizontal pager over every synced night, pushed from the home screen.
//  Pages run chronologically (oldest on the left), so a leftward swipe
//  reveals the next, newer night — the Calendar/Health convention. ‹ › in
//  the toolbar step the same way for one-handed use and VoiceOver.
//

import SwiftUI

struct NightPagerView: View {
    @EnvironmentObject private var state: AppState
    @State private var selection: UInt32

    init(initial: Night) {
        _selection = State(initialValue: initial.header.dateKey)
    }

    /// Oldest first — state.nights is newest-first for the list.
    private var nights: [Night] {
        state.nights.sorted { $0.header.dateKey < $1.header.dateKey }
    }

    private var index: Int? {
        nights.firstIndex { $0.header.dateKey == selection }
    }

    private var current: Night? { index.map { nights[$0] } }
    private var hasOlder: Bool { (index ?? 0) > 0 }
    private var hasNewer: Bool { index.map { $0 < nights.count - 1 } ?? false }

    var body: some View {
        TabView(selection: $selection) {
            ForEach(nights, id: \.header.dateKey) { night in
                NightDetailView(night: night)
                    .tag(night.header.dateKey)
            }
        }
        .tabViewStyle(.page(indexDisplayMode: .never))
        .navigationTitle(current?.displayDate ?? "")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItemGroup(placement: .topBarTrailing) {
                Button { step(-1) } label: {
                    Image(systemName: "chevron.left")
                }
                .disabled(!hasOlder)
                .accessibilityLabel("Previous night")

                Button { step(1) } label: {
                    Image(systemName: "chevron.right")
                }
                .disabled(!hasNewer)
                .accessibilityLabel("Next night")
            }
        }
    }

    private func step(_ delta: Int) {
        guard let i = index else { return }
        let j = i + delta
        guard nights.indices.contains(j) else { return }
        withAnimation { selection = nights[j].header.dateKey }
    }
}
