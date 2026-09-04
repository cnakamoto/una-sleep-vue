//
//  Formatters.swift
//  SleepVueSync
//

import Foundation

/// 452 → "7h32"
func minutesText(_ minutes: Int) -> String {
    let h = minutes / 60, m = minutes % 60
    return h > 0 ? "\(h)h\(String(format: "%02d", m))" : "\(m)m"
}

/// Date → "23:14"
func timeText(_ date: Date) -> String {
    let f = DateFormatter()
    f.dateFormat = "HH:mm"
    return f.string(from: date)
}
