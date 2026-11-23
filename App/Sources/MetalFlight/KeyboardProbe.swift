import Foundation

/// One sample per game launch. The clipboard receives only allowlisted counters.
struct KeyboardProbe {
    let marker: URL
    let log: URL

    private static let events = [
        "driver-down", "driver-up", "injected-down", "injected-up", "queue-down", "queue-up",
        "translate-down", "queue-char", "queue-ime-key", "queue-ime-char", "raw-read-down", "raw-read-up",
    ]

    func arm() throws {
        try Data().write(to: marker, options: [.atomic])
    }

    func disarm() {
        try? FileManager.default.removeItem(at: marker)
    }

    func report() throws -> String? {
        let handle = try FileHandle(forReadingFrom: log)
        defer { try? handle.close() }
        let size = try handle.seekToEnd()
        let window: UInt64 = 256 * 1024
        try handle.seek(toOffset: size > window ? size - window : 0)
        let text = String(decoding: try handle.read(upToCount: Int(window)) ?? Data(), as: UTF8.self)
        let prefix = "[MF KEY PROBE] "
        var counts: [String: Int] = [:]
        var started = false
        var records = 0
        for row in text.split(whereSeparator: \.isNewline) where row.hasPrefix(prefix) {
            let event = String(row.dropFirst(prefix.count))
            if event == "started" {
                started = true
            } else if started, records < 31, Self.events.contains(event) {
                counts[event, default: 0] += 1
                records += 1
            }
        }
        guard started, records > 0 else { return nil }
        let summary = Self.events.map { "\($0)=\(counts[$0, default: 0])" }.joined(separator: "\n")
        return "MetalFlight keyboard test #13\n" + summary
    }
}
