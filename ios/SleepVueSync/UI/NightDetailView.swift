//
//  NightDetailView.swift
//  SleepVueSync
//
//  One night: summary header, stage split, hypnogram, HR curve, movement.
//  Same panels as tools/plot_night.py. Hosted as a page inside
//  NightPagerView, which owns the navigation title and ‹ › controls.
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

            NightFlagBadges(flags: night.header.flags)
        }
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

    /// One time domain for all panels — with hidden y-axes everywhere this
    /// makes the plot areas identical, so the charts align vertically.
    private var xDomain: ClosedRange<Date> { night.bed...night.wake }

    private var hypnogramSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Sleep stages").font(.headline)
            HypnogramView(night: night)
                .sleepChartXAxis(xDomain)
        }
    }

    private var hrSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Heart rate · \(night.hrMin)–\(night.hrMax) bpm · \(night.hrCoverage) % coverage")
                .font(.headline)
            // One series per contiguous run so the line breaks at dropouts
            // (>= 2 gap epochs) instead of bridging them.
            Chart(hrPoints, id: \.epoch.date) { point in
                LineMark(
                    x: .value("Time", point.epoch.date),
                    y: .value("bpm", point.epoch.hr),
                    series: .value("Run", point.run)
                )
            }
            .chartYScale(domain: .automatic(includesZero: false))
            .foregroundStyle(Color(red: 0.88, green: 0.33, blue: 0.44))
            .frame(height: 160)
            .sleepChartXAxis(xDomain)
        }
    }

    private struct HRPoint {
        let epoch: SleepEpoch
        let run: Int
    }

    private var hrPoints: [HRPoint] {
        night.hrSegments.enumerated().flatMap { run, segment in
            segment.map { HRPoint(epoch: $0, run: run) }
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
            .sleepChartXAxis(xDomain)
        }
    }
}
