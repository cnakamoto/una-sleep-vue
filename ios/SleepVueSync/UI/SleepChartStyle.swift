//
//  SleepChartStyle.swift
//  SleepVueSync
//
//  Shared x-axis for every chart on the night detail page. Identical time
//  domain + hidden y-axis on all panels ⇒ identical plot rectangles, so
//  events line up vertically across the hypnogram, HR, and movement charts.
//  Hourly gridlines with HH:mm labels.
//

import Charts
import SwiftUI

struct SleepChartXAxis: ViewModifier {
    let domain: ClosedRange<Date>

    func body(content: Content) -> some View {
        content
            .chartXScale(domain: domain)
            .chartYAxis(.hidden)
            .chartXAxis {
                AxisMarks(values: .stride(by: .hour)) { _ in
                    AxisGridLine(stroke: StrokeStyle(lineWidth: 0.5, dash: [3, 3]))
                    // Two-digit 24-hour hours only ("23", "00", "06") — no minutes.
                    AxisValueLabel(
                        format: .dateTime.hour(.twoDigits(amPM: .omitted))
                    )
                }
            }
    }
}

extension View {
    func sleepChartXAxis(_ domain: ClosedRange<Date>) -> some View {
        modifier(SleepChartXAxis(domain: domain))
    }
}
