import XCTest
@testable import MetalFlight

final class GameBuildTests: XCTestCase {
    func testOnlyExplicitlySupportedExecutablePasses() throws {
        let file = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: file) }
        try Data("owned simulator fixture".utf8).write(to: file)
        let supported = [try Updater.sha256(of: file): "owned-test-build"]
        XCTAssertEqual(try GameBuild.verify(file, supported: supported), "owned-test-build")
        try Data("updated simulator fixture".utf8).write(to: file)
        XCTAssertThrowsError(try GameBuild.verify(file, supported: supported))
        XCTAssertThrowsError(try GameBuild.verify(file, supported: nil))
        XCTAssertThrowsError(try GameBuild.verify(file, supported: [:]))
    }
}
