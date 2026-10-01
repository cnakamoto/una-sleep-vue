//
//  LatestNightCard.swift
//  SleepVueSync
//
//  Compact summary of the Latest night (CONTEXT.md: the synced session
//  with the most recent date — not necessarily last night's, which is why
//  the date is always printed). Hero total, mini hypnogram, key stats.
//

import Charts
import SwiftUI

struct LatestNightCard: View {
    let night: Night

    var body: some View {
        let deep = night.minutes(of: .deep)
        let light = night.minutes(of: .light)
        let deepPct = night.totalMinutes > 0 ? deep * 100 / night.totalMinutes : 0

        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .firstTextBaseline) {
                Text(minutesText(night.totalMinutes))
                    .font(.system(.largeTitle, design: .rounded).weight(.semibold))
                Spacer()
                VStack(alignment: .trailing, spacing: 2) {
                    Text(night.displayDate)
                        .font(.subheadline.weight(.medium))
                    Text("\(timeText(night.bed)) → \(timeText(night.wake))")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            // Mini hypnogram: same colours/bands as the detail page, but no
            // axes — the card is a glance, the detail page has the labels.
            HypnogramView(night: night, height: 56)
                .chartXScale(domain: night.bed...night.wake)
                .chartXAxis(.hidden)
                .chartYAxis(.hidden)
                .clipShape(RoundedRectangle(cornerRadius: 6))

            HStack(spacing: 16) {
                stat("Deep", "\(minutesText(deep)) · \(deepPct)%", color: SleepStage.deep.color)
                stat("Light", minutesText(light), color: SleepStage.light.color)
                stat("HR", "\(night.hrMin)–\(night.hrMax)", color: .secondary)
                Spacer()
            }
            .font(.caption)

            NightFlagBadges(flags: night.header.flags)
        }
        .padding(.vertical, 6)
        .accessibilityElement(children: .combine)
    }

    private func stat(_ label: String, _ value: String, color: Color) -> some View {
        HStack(spacing: 4) {
            Circle().fill(color).frame(width: 8, height: 8)
            Text(label).foregroundStyle(.secondary)
            Text(value).fontWeight(.medium)
        }
    }
}

/// Capsule badges for a session's flags; renders nothing when there are none.
/// Shared by the Latest night card and the detail header.
struct NightFlagBadges: View {
    let flags: NightFlags

    var body: some View {
        if !flags.isEmpty {
            HStack(spacing: 8) {
                if flags.contains(.autoWake) { badge("Auto-wake") }
                if flags.contains(.interrupted) { badge("Interrupted") }
                if flags.contains(.unwornAbort) { badge("Watch removed") }
                if flags.contains(.batteryAbort) { badge("Low battery") }
            }
        }
    }

    private func badge(_ text: String) -> some View {
        Text(text)
            .font(.caption2.weight(.medium))
            .padding(.horizontal, 8)
            .padding(.vertical, 3)
            .background(Color.secondary.opacity(0.2))
            .clipShape(Capsule())
    }
}
