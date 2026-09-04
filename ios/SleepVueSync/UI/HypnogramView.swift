//
//  HypnogramView.swift
//  SleepVueSync
//
//  Classic hypnogram: AWAKE on top, DEEP at the bottom, one colored run per
//  maximal stretch of the same stage — same rendering as tools/plot_night.py.
//

import SwiftUI

extension SleepStage {
    var color: Color {
        switch self {
        case .awake: return Color(red: 0.91, green: 0.64, blue: 0.24) // #e8a33d
        case .light: return Color(red: 0.31, green: 0.56, blue: 0.82) // #4f8fd0
        case .deep: return Color(red: 0.21, green: 0.28, blue: 0.78)
        }
    }

    /// Vertical band index: 0 = top (AWAKE) … 2 = bottom (DEEP).
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
        Canvas { context, size in
            let t0 = night.bed.timeIntervalSince1970
            let t1 = night.wake.timeIntervalSince1970
            guard t1 > t0, !night.epochs.isEmpty else { return }

            let bandHeight = size.height / 3
            for run in stageRuns {
                let x0 = (run.start.timeIntervalSince1970 - t0) / (t1 - t0) * size.width
                let x1 = (run.end.timeIntervalSince1970 - t0) / (t1 - t0) * size.width
                let rect = CGRect(x: x0,
                                  y: CGFloat(run.stage.band) * bandHeight + bandHeight * 0.05,
                                  width: max(1, x1 - x0),
                                  height: bandHeight * 0.9)
                context.fill(Path(rect), with: .color(run.stage.color))
            }
        }
        .frame(height: 120)
        .background(Color(white: 0.1))
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .accessibilityLabel("Hypnogram")
    }

    /// Maximal (start, end, stage) runs — same algorithm as plot_night.py `runs()`.
    private var stageRuns: [(start: Date, end: Date, stage: SleepStage)] {
        let epochs = night.epochs
        guard !epochs.isEmpty else { return [] }
        var result: [(Date, Date, SleepStage)] = []
        var start = 0
        for i in 1...epochs.count {
            if i == epochs.count || epochs[i].stage != epochs[start].stage {
                result.append((epochs[start].date,
                               epochs[i - 1].date.addingTimeInterval(30),
                               epochs[start].stage))
                start = i
            }
        }
        return result
    }
}
