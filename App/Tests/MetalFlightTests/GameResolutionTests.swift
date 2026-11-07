import AppKit
import XCTest
@testable import MetalFlight

final class GameResolutionTests: XCTestCase {
    private var directory: URL!
    private var prefix: URL!
    private var support: URL!
    private var config: URL!

    override func setUpWithError() throws {
        directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        prefix = directory.appendingPathComponent("prefix")
        support = directory.appendingPathComponent("support")
        config = prefix.appendingPathComponent(
            "drive_c/users/test-user/AppData/Roaming/Microsoft Flight Simulator 2024/UserCfg.opt")
        try FileManager.default.createDirectory(at: config.deletingLastPathComponent(), withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: support, withIntermediateDirectories: true)
        try userConfig(windowed: true).write(to: config, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: config.path)
    }

    override func tearDownWithError() throws {
        try? FileManager.default.removeItem(at: directory)
    }

    func testWindowedResolutionMatchesDisplayAndRestoresOriginalValues() throws {
        guard let screen = NSScreen.screens.first else { throw XCTSkip("No active display is available.") }
        let originalPermissions = try FileManager.default.attributesOfItem(atPath: config.path)[.posixPermissions] as? NSNumber

        let override = try XCTUnwrap(GameResolutionOverride.prepare(prefix: prefix, support: support, screen: screen))
        let expected = "\(Int(screen.frame.width.rounded())) \(Int(screen.frame.height.rounded()))"
        XCTAssertEqual(value("Resolution"), expected)
        XCTAssertEqual(value("FullScreenResolution"), expected)
        XCTAssertTrue(FileManager.default.fileExists(atPath: support.appendingPathComponent("game-resolution-override.json").path))

        try override.restore()
        XCTAssertEqual(value("Resolution"), "1280 720")
        XCTAssertEqual(value("FullScreenResolution"), "1920 1080")
        XCTAssertEqual(try FileManager.default.attributesOfItem(atPath: config.path)[.posixPermissions] as? NSNumber,
                       originalPermissions)
        XCTAssertFalse(FileManager.default.fileExists(atPath: support.appendingPathComponent("game-resolution-override.json").path))
    }

    func testDoesNotOverrideFullscreenModeOrAlreadyMatchedSettings() throws {
        try userConfig(windowed: false).write(to: config, atomically: true, encoding: .utf8)
        XCTAssertNil(try GameResolutionOverride.prepare(prefix: prefix, support: support, screen: NSScreen.screens.first))
        XCTAssertEqual(value("Resolution"), "1280 720")

        guard let screen = NSScreen.screens.first else { throw XCTSkip("No active display is available.") }
        let matched = userConfig(windowed: true)
            .replacingOccurrences(of: "Resolution 1280 720", with: "Resolution \(Int(screen.frame.width.rounded())) \(Int(screen.frame.height.rounded()))")
            .replacingOccurrences(of: "FullScreenResolution 1920 1080", with: "FullScreenResolution \(Int(screen.frame.width.rounded())) \(Int(screen.frame.height.rounded()))")
        try matched.write(to: config, atomically: true, encoding: .utf8)
        XCTAssertNil(try GameResolutionOverride.prepare(prefix: prefix, support: support, screen: screen))
        XCTAssertFalse(FileManager.default.fileExists(atPath: support.appendingPathComponent("game-resolution-override.json").path))
    }

    func testPreservesResolutionChangedWhileGameWasRunning() throws {
        guard let screen = NSScreen.screens.first else { throw XCTSkip("No active display is available.") }
        let override = try XCTUnwrap(GameResolutionOverride.prepare(prefix: prefix, support: support, screen: screen))
        var current = try String(contentsOf: config, encoding: .utf8)
        current = current.replacingOccurrences(of: value("Resolution"), with: "1024 768")
            .replacingOccurrences(of: value("FullScreenResolution"), with: "1024 768")
        try current.write(to: config, atomically: true, encoding: .utf8)

        try override.restore()
        XCTAssertEqual(value("Resolution"), "1024 768")
        XCTAssertEqual(value("FullScreenResolution"), "1024 768")
    }

    private func value(_ key: String) -> String {
        let text = (try? String(contentsOf: config, encoding: .utf8)) ?? ""
        return text.split(separator: "\n").compactMap { line in
            let fields = line.split(whereSeparator: \.isWhitespace)
            return fields.count >= 3 && fields[0] == key ? "\(fields[1]) \(fields[2])" : nil
        }.first ?? ""
    }

    private func userConfig(windowed: Bool) -> String {
        """
        Version 1
        Windowed \(windowed ? 1 : 0)
        Resolution 1280 720
        FullScreenResolution 1920 1080
        """
    }
}
