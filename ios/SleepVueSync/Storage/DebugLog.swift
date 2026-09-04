//
//  DebugLog.swift
//  SleepVueSync
//
//  Persistent debug log (Documents/ble-debug.log, rolling 128 KB). The BLE
//  client mirrors lines into its @Published array for the live UI; the file
//  is what makes *background* runs observable after the fact.
//

import Foundation

enum DebugLog {

    private static var fileURL: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("ble-debug.log")
    }
    private static let maxBytes = 128 * 1024

    /// Timestamp, append to the log file, and return the formatted line.
    @discardableResult
    static func write(_ message: String) -> String {
        let stamp = DateFormatter.localizedString(from: Date(), dateStyle: .short, timeStyle: .medium)
        let line = "\(stamp)  \(message)"
        append(line + "\n")
        return line
    }

    static func recent(_ count: Int) -> [String] {
        guard let text = try? String(contentsOf: fileURL, encoding: .utf8) else { return [] }
        return Array(text.split(separator: "\n").map(String.init).suffix(count))
    }

    static func clear() {
        try? FileManager.default.removeItem(at: fileURL)
    }

    private static func append(_ line: String) {
        guard let data = line.data(using: .utf8) else { return }
        if FileManager.default.fileExists(atPath: fileURL.path) {
            if let handle = try? FileHandle(forWritingTo: fileURL) {
                defer { handle.closeFile() }
                if handle.seekToEndOfFile() > maxBytes {
                    trim()
                    handle.seekToEndOfFile()
                }
                handle.write(data)
            }
        } else {
            try? data.write(to: fileURL)
        }
    }

    /// Keep the second half of the file, cut at a line boundary.
    private static func trim() {
        guard let data = try? Data(contentsOf: fileURL) else { return }
        let tail = data.suffix(maxBytes / 2)
        guard let newline = tail.firstIndex(of: 0x0A) else { return }
        try? tail.suffix(from: newline + 1).write(to: fileURL, options: .atomic)
    }
}
