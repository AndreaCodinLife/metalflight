import AppKit
import Foundation

/// Keeps MSFS's client area and rendered UI at the same size on macOS displays.
/// Wine reports the client area in logical display points; a smaller UserCfg
/// render size is stretched to that area, which leaves the game's mouse hit
/// testing in the old coordinate space.
final class GameResolutionOverride: @unchecked Sendable {
    private struct Journal: Codable {
        let configPath: String
        let oldResolution: Size
        let oldFullscreenResolution: Size
        let applied: Size
    }

    private struct Size: Codable, Equatable {
        let width: Int
        let height: Int
    }

    private let journalURL: URL
    private let journal: Journal
    private let lock = NSLock()
    private var restored = false

    private init(journalURL: URL, journal: Journal) {
        self.journalURL = journalURL
        self.journal = journal
    }

    /// Restores an interrupted previous launch, then temporarily matches both
    /// UserCfg resolutions to the display MetalFlight is running on.
    static func prepare(prefix: URL, support: URL, screen: NSScreen?) throws -> GameResolutionOverride? {
        let journalURL = support.appendingPathComponent("game-resolution-override.json")
        try restoreInterruptedOverride(journalURL: journalURL, prefix: prefix)

        guard let screen else { return nil }
        let target = Size(width: Int(screen.frame.width.rounded()), height: Int(screen.frame.height.rounded()))
        guard target.width > 0, target.height > 0,
              let configURL = userConfig(in: prefix),
              var contents = try? String(contentsOf: configURL, encoding: .utf8),
              isWindowed(in: contents),
              let oldResolution = value(for: "Resolution", in: contents),
              let oldFullscreen = value(for: "FullScreenResolution", in: contents) else { return nil }

        guard oldResolution != target || oldFullscreen != target else { return nil }
        contents = try replacing("Resolution", in: contents, with: target)
        contents = try replacing("FullScreenResolution", in: contents, with: target)

        let journal = Journal(configPath: configURL.path, oldResolution: oldResolution,
                              oldFullscreenResolution: oldFullscreen, applied: target)
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(journal).write(to: journalURL, options: .atomic)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: journalURL.path)
        do {
            try write(contents, to: configURL)
            return GameResolutionOverride(journalURL: journalURL, journal: journal)
        } catch {
            try? FileManager.default.removeItem(at: journalURL)
            throw error
        }
    }

    /// Restores only if the game left our temporary dimensions in place. A
    /// resolution the user selected while playing is therefore preserved.
    func restore() throws {
        lock.lock()
        defer { lock.unlock() }
        guard !restored else { return }

        let configURL = URL(fileURLWithPath: journal.configPath)
        guard var contents = try? String(contentsOf: configURL, encoding: .utf8),
              Self.value(for: "Resolution", in: contents) == journal.applied,
              Self.value(for: "FullScreenResolution", in: contents) == journal.applied else {
            try? FileManager.default.removeItem(at: journalURL)
            restored = true
            return
        }
        contents = try Self.replacing("Resolution", in: contents, with: journal.oldResolution)
        contents = try Self.replacing("FullScreenResolution", in: contents, with: journal.oldFullscreenResolution)
        try Self.write(contents, to: configURL)
        try FileManager.default.removeItem(at: journalURL)
        restored = true
    }

    private static func restoreInterruptedOverride(journalURL: URL, prefix: URL) throws {
        guard let data = try? Data(contentsOf: journalURL),
              let journal = try? JSONDecoder().decode(Journal.self, from: data) else { return }
        let configURL = URL(fileURLWithPath: journal.configPath).standardizedFileURL
        let prefixURL = prefix.standardizedFileURL
        guard configURL.path.hasPrefix(prefixURL.path + "/"),
              var contents = try? String(contentsOf: configURL, encoding: .utf8),
              value(for: "Resolution", in: contents) == journal.applied,
              value(for: "FullScreenResolution", in: contents) == journal.applied else {
            try? FileManager.default.removeItem(at: journalURL)
            return
        }
        contents = try replacing("Resolution", in: contents, with: journal.oldResolution)
        contents = try replacing("FullScreenResolution", in: contents, with: journal.oldFullscreenResolution)
        try write(contents, to: configURL)
        try FileManager.default.removeItem(at: journalURL)
    }

    private static func userConfig(in prefix: URL) -> URL? {
        let users = prefix.appendingPathComponent("drive_c/users", isDirectory: true)
        guard let entries = try? FileManager.default.contentsOfDirectory(at: users,
                                                                         includingPropertiesForKeys: [.isDirectoryKey]) else { return nil }
        let matches = entries.map {
            $0.appendingPathComponent("AppData/Roaming/Microsoft Flight Simulator 2024/UserCfg.opt")
        }.filter { FileManager.default.fileExists(atPath: $0.path) }
        return matches.count == 1 ? matches[0] : nil
    }

    private static func value(for key: String, in contents: String) -> Size? {
        for line in contents.split(separator: "\n", omittingEmptySubsequences: false) {
            let fields = line.split(whereSeparator: \.isWhitespace)
            guard fields.count >= 3, fields[0] == key,
                  let width = Int(fields[1]), let height = Int(fields[2]) else { continue }
            return Size(width: width, height: height)
        }
        return nil
    }

    private static func isWindowed(in contents: String) -> Bool {
        contents.split(separator: "\n", omittingEmptySubsequences: false).contains { line in
            let fields = line.split(whereSeparator: \.isWhitespace)
            return fields.count >= 2 && fields[0] == "Windowed" && fields[1] == "1"
        }
    }

    private static func write(_ contents: String, to url: URL) throws {
        let permissions = (try? FileManager.default.attributesOfItem(atPath: url.path)[.posixPermissions]) as? NSNumber
        try contents.write(to: url, atomically: true, encoding: .utf8)
        if let permissions {
            try FileManager.default.setAttributes([.posixPermissions: permissions], ofItemAtPath: url.path)
        }
    }

    private static func replacing(_ key: String, in contents: String, with size: Size) throws -> String {
        let pattern = "(?m)^([ \\t]*" + NSRegularExpression.escapedPattern(for: key) +
            "[ \\t]+)\\d+[ \\t]+\\d+([ \\t]*\\r?)$"
        let regex = try NSRegularExpression(pattern: pattern)
        let range = NSRange(contents.startIndex..<contents.endIndex, in: contents)
        guard let match = regex.firstMatch(in: contents, range: range),
              let prefixRange = Range(match.range(at: 1), in: contents),
              let suffixRange = Range(match.range(at: 2), in: contents) else {
            throw CocoaError(.fileReadCorruptFile)
        }
        return String(contents[..<prefixRange.upperBound]) + "\(size.width) \(size.height)" +
            String(contents[suffixRange.lowerBound...])
    }
}
