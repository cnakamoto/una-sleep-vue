//
//  HypnogramView.swift
//  SleepVueSync
//
//  Classic hypnogram as a Swift Charts plot (one RectangleMark per stage
//  run): AWAKE on top, DEEP at the bottom. Being a Chart lets it share the
//  exact same plot geometry as the HR and movement panels (see
//  SleepChartStyle.swift) — the x domain is applied by the parent.
//

import Charts
import SwiftUI

extension SleepStage {
    var color: Color {
        switch self {
        case .awake: return Color(red: 0.91, green: 0.64, blue: 0.24) // #e8a33d
        case .light: return Color(red: 0.31, green: 0.56, blue: 0.82) // #4f8fd0
        case .deep: return Color(red: 0.21, green: 0.28, blue: 0.78)
        }
    }

    /// Band index 0–2; combined with the y mapping below it places AWAKE at
    /// the top of the hypnogram and DEEP at the bottom.
    var band: Int {
        switch self {
        case .awake: return 0
        case .light: return 1
        case .deep: return 2
        }
    }
}

struct HypnogramView: View {
    let night: Night

    var body: some View {
        Chart(night.stageRuns, id: \.start) { run in
            RectangleMark(
                xStart: .value("Start", run.start),
                xEnd: .value("End", run.end),
                // y domain 0...3; awake (band 0) → 2...3 at the top.
                yStart: .value("Base", 2 - run.stage.band),
                yEnd: .value("Top", 3 - run.stage.band)
            )
            .foregroundStyle(run.stage.color)
        }
        .chartYScale(domain: 0...3)
        .frame(height: 120)
        .accessibilityLabel("Hypnogram")
    }
}
