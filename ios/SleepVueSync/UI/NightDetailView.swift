//
//  NightDetailView.swift
//  SleepVueSync
//
//  One night: summary header, stage split, hypnogram, HR curve, movement.
//  Same panels as tools/plot_night.py.
//

import Charts
import SwiftUI

struct NightDetailView: View {
    let night: Night

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                header
                stageSplit
                hypnogramSection
                hrSection
                movementSection
            }
            .padding()
        }
        .navigationTitle(night.displayDate)
        .navigationBarTitleDisplayMode(.inline)
    }

    // MARK: - Header

    private var header: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .firstTextBaseline) {
                Text(minutesText(night.totalMinutes))
                    .font(.system(size: 44, weight: .semibold, design: .rounded))
                Spacer()
                VStack(alignment: .trailing) {
                    Text("\(timeText(night.bed)) → \(timeText(night.wake))")
                    Text("HR \(night.hrMin)–\(night.hrMax) avg \(night.hrAvg)")
                }
                .font(.subheadline)
                .foregroundStyle(.secondary)
            }

            if !night.header.flags.isEmpty {
                HStack(spacing: 8) {
                    if night.header.flags.contains(.autoWake) { badge("Auto-wake") }
                    if night.header.flags.contains(.interrupted) { badge("Interrupted") }
                    if night.header.flags.contains(.unwornAbort) { badge("Watch removed") }
                    if night.header.flags.contains(.batteryAbort) { badge("Low battery") }
                }
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

    // MARK: - Stage split

    private var stageSplit: some View {
        let awake = night.minutes(of: .awake)
        let light = night.minutes(of: .light)
        let deep = night.minutes(of: .deep)
        let total = max(1, awake + light + deep)

        return VStack(alignment: .leading, spacing: 8) {
            GeometryReader { geo in
                HStack(spacing: 2) {
                    splitPart(.awake, minutes: awake, total: total, width: geo.size.width)
                    splitPart(.light, minutes: light, total: total, width: geo.size.width)
                    splitPart(.deep, minutes: deep, total: total, width: geo.size.width)
                }
            }
            .frame(height: 14)
            .clipShape(RoundedRectangle(cornerRadius: 7))

            HStack(spacing: 16) {
                legend(.awake, minutes: awake)
                legend(.light, minutes: light)
                legend(.deep, minutes: deep)
            }
            .font(.caption)
        }
    }

    private func splitPart(_ stage: SleepStage, minutes: Int, total: Int, width: CGFloat) -> some View {
        stage.color
            .frame(width: minutes > 0 ? max(4, width * CGFloat(minutes) / CGFloat(total)) : 0)
    }

    private func legend(_ stage: SleepStage, minutes: Int) -> some View {
        HStack(spacing: 4) {
            Circle().fill(stage.color).frame(width: 8, height: 8)
            Text("\(stage.displayName) \(minutesText(minutes))")
                .foregroundStyle(.secondary)
        }
    }

    // MARK: - Charts

    private var hypnogramSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Sleep stages").font(.headline)
            HypnogramView(night: night)
        }
    }

    private var hrSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Heart rate").font(.headline)
            Chart(night.epochs.filter { $0.hr > 0 }, id: \.date) { epoch in
                LineMark(
                    x: .value("Time", epoch.date),
                    y: .value("bpm", epoch.hr)
                )
            }
            .chartYScale(domain: .automatic(includesZero: false))
            .foregroundStyle(Color(red: 0.88, green: 0.33, blue: 0.44))
            .frame(height: 160)
        }
    }

    private var movementSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Movement").font(.headline)
            Chart(night.epochs, id: \.date) { epoch in
                BarMark(
                    x: .value("Time", epoch.date),
                    y: .value("Motion", epoch.movement)
                )
            }
            .foregroundStyle(Color(red: 0.78, green: 0.71, blue: 0.35))
            .frame(height: 100)
        }
    }
}
