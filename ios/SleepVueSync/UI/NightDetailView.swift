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
        VStack(alignment: .leading, spacing: 4) {
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

            // Always one badge row tall (blank on unflagged nights) so the
            // charts don't jump when swiping between flagged/unflagged nights.
            NightFlagBadges(flags: night.header.flags, reservesRow: true)
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
            // Fixed 45–95 bpm scale so nights are comparable at a glance.
            // The y-axis stays hidden (shared plot rect for alignment), so
            // the 10 bpm gridlines are RuleMarks labelled inside the plot.
            Chart {
                ForEach(Self.hrGridlines, id: \.self) { bpm in
                    RuleMark(y: .value("bpm", bpm))
                        .lineStyle(StrokeStyle(lineWidth: 0.5))
                        .foregroundStyle(Color.secondary.opacity(0.4))
                        .annotation(position: .top, alignment: .leading, spacing: 1) {
                            Text("\(bpm)")
                                .font(.system(size: 9))
                                .foregroundStyle(.secondary)
                        }
                }
                // Where the curve leaves the band it flattens against the
                // edge, which would otherwise be indistinguishable from a
                // genuinely steady 95 bpm — so mark the stretch.
                ForEach(hrClipRuns, id: \.self) { run in
                    RuleMark(
                        xStart: .value("From", run.start),
                        xEnd: .value("To", run.end),
                        y: .value("bpm", run.high
                                  ? Self.hrDomain.upperBound - Self.hrClipInset
                                  : Self.hrDomain.lowerBound + Self.hrClipInset)
                    )
                    .lineStyle(StrokeStyle(lineWidth: 2))
                    .foregroundStyle(Self.hrColor)
                }
                ForEach(hrPoints, id: \.epoch.date) { point in
                    LineMark(
                        x: .value("Time", point.epoch.date),
                        y: .value("bpm", point.epoch.hr),
                        series: .value("Run", point.run)
                    )
                    .foregroundStyle(Self.hrColor)
                }
            }
            .chartYScale(domain: Self.hrDomain)
            .chartPlotStyle { $0.clipped() }   // epochs outside 45–95 are cut off
            .frame(height: 160)
            .sleepChartXAxis(xDomain)
            .accessibilityLabel(hrClipRuns.isEmpty
                ? "Heart rate"
                : "Heart rate, \(hrClipRuns.count) stretch(es) beyond the 45 to 95 band")
        }
    }

    private static let hrColor = Color(red: 0.88, green: 0.33, blue: 0.44)
    private static let hrDomain = 45...95
    private static let hrGridlines = Array(stride(from: 50, through: 90, by: 10))
    /// Clip markers sit 1 bpm inside the bound: drawn exactly on it, the
    /// plot clip would take half the stroke.
    private static let hrClipInset = 1
    private static let epochSeconds: TimeInterval = 30
    /// Most clips are a single epoch, and 30 s on a 10-hour axis is a third
    /// of a point wide — invisible. Short runs are widened (centred) to
    /// this so the marker is actually legible; it exaggerates duration, but
    /// the marker's job is "the curve left the band here", not "for exactly
    /// this long".
    private static let hrClipMinSpan: TimeInterval = 120

    private struct ClipRun: Hashable {
        let start: Date
        let end: Date
        /// Pinned to the top of the band; otherwise the bottom.
        let high: Bool
    }

    /// Contiguous stretches whose HR falls outside the fixed band, split by
    /// which edge they hit. Built from hrSegments, so gap epochs can't leak
    /// in — hr == 0 means "no sample", not a 0 bpm reading, and treating one
    /// as a low-side clip would mark every dropout.
    private var hrClipRuns: [ClipRun] {
        var runs: [ClipRun] = []
        for segment in night.hrSegments {
            var open: ClipRun?
            for epoch in segment {
                let hr = Int(epoch.hr)
                let high = hr > Self.hrDomain.upperBound
                let outside = high || hr < Self.hrDomain.lowerBound
                let epochEnd = epoch.date.addingTimeInterval(Self.epochSeconds)
                if outside {
                    if let run = open, run.high == high {
                        open = ClipRun(start: run.start, end: epochEnd, high: high)
                    } else {
                        if let run = open { runs.append(run) }   // switched edges
                        open = ClipRun(start: epoch.date, end: epochEnd, high: high)
                    }
                } else if let run = open {
                    runs.append(run)
                    open = nil
                }
            }
            if let run = open { runs.append(run) }
        }
        return runs.map(Self.widened)
    }

    /// Grow a run to hrClipMinSpan, keeping it centred on the real clip.
    private static func widened(_ run: ClipRun) -> ClipRun {
        let span = run.end.timeIntervalSince(run.start)
        guard span < hrClipMinSpan else { return run }
        let grow = (hrClipMinSpan - span) / 2
        return ClipRun(
            start: run.start.addingTimeInterval(-grow),
            end: run.end.addingTimeInterval(grow),
            high: run.high
        )
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
            // Fixed 0–8 so bar heights compare across nights. The field can
            // reach 63, but nights 2026-09-02…15 never exceed 7 (99th pct
            // ≤ 5), so a 0–63 axis would flatten every bar.
            .chartYScale(domain: 0...8)
            .chartPlotStyle { $0.clipped() }   // rare spikes > 8 are cut off
            .foregroundStyle(Color(red: 0.78, green: 0.71, blue: 0.35))
            .frame(height: 100)
            .sleepChartXAxis(xDomain)
        }
    }
}
