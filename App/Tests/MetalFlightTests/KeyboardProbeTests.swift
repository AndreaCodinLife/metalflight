import XCTest
@testable import MetalFlight

final class KeyboardProbeTests: XCTestCase {
    func testReportsOnlyKnownEventCountsAfterArming() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: false)
        defer { try? FileManager.default.removeItem(at: root) }
        let probe = KeyboardProbe(marker: root.appendingPathComponent("test.arm"),
                                  log: root.appendingPathComponent("game.log"))
        try Data("[MF KEY PROBE] queue-char\n".utf8).write(to: probe.log)
        XCTAssertNil(try probe.report())
        try probe.arm()
        XCTAssertTrue(FileManager.default.fileExists(atPath: probe.marker.path))

        try Data("""
            ordinary game text
            [MF KEY PROBE] queue-char
            [MF KEY PROBE] started
            [MF KEY PROBE] driver-down
            [MF KEY PROBE] queue-char
            [MF KEY PROBE] queue-char
            [MF KEY PROBE] unexpected value
            """.utf8).write(to: probe.log)
        let report = try XCTUnwrap(probe.report())
        XCTAssertTrue(report.contains("driver-down=1\n"))
        XCTAssertTrue(report.contains("queue-char=2\n"))
        XCTAssertTrue(report.contains("raw-read-down=0\n"))
        XCTAssertFalse(report.contains("ordinary game text"))
        XCTAssertFalse(report.contains("unexpected value"))
        probe.disarm()
        XCTAssertFalse(FileManager.default.fileExists(atPath: probe.marker.path))
    }
}
