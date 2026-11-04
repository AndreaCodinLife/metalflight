import XCTest
@testable import MetalFlight

final class AppModelTests: XCTestCase {
    override class func setUp() {
        super.setUp()
        setenv("METALFLIGHT_SUPPORT_DIR", NSTemporaryDirectory() + "MetalFlightModelTests-" + UUID().uuidString, 1)
    }

    override class func tearDown() {
        try? FileManager.default.removeItem(at: Paths.support)
        super.tearDown()
    }

    private func manifest(_ version: String = "owned-preview") -> ReleaseManifest {
        ReleaseManifest(schema: 1, appVersion: "0.1.8", runtime: .init(
            version: version, url: URL(string: "https://example.invalid/runtime.tar.xz")!,
            sha256: String(repeating: "a", count: 64), size: 1))
    }

    @MainActor
    private func waitUntil(_ condition: () -> Bool, file: StaticString = #filePath, line: UInt = #line) async throws {
        for _ in 0..<400 {
            if condition() { return }
            try await Task.sleep(nanoseconds: 5_000_000)
        }
        XCTFail("Timed out waiting for model work", file: file, line: line)
    }

    @MainActor
    func testChannelChangeClearsAvailabilityAndError() {
        let model = AppModel()
        model.availableRuntime = "owned-stable"
        model.lastError = "Previous channel error"
        model.updateChannelDidChange(.preview)
        XCTAssertEqual(model.updateChannel, .preview)
        XCTAssertNil(model.availableRuntime)
        XCTAssertNil(model.lastError)
    }

    @MainActor
    func testCurrentChannelResultIsDisplayed() async throws {
        let expected = manifest()
        let model = AppModel(fetchRuntimeManifest: { expected })
        model.updateChannelDidChange(.preview)
        model.checkForUpdates()
        try await waitUntil { model.busy == nil }
        XCTAssertEqual(model.availableRuntime, expected.runtime.version)
        XCTAssertNil(model.lastError)
        XCTAssertTrue(model.lines.contains("Latest preview runtime: owned-preview"))
    }

    @MainActor
    func testFailedCheckClearsPreviousAvailability() async throws {
        let model = AppModel(fetchRuntimeManifest: { throw MetalFlightError.message("Owned feed failure") })
        model.availableRuntime = "owned-previous"
        model.checkForUpdates()
        try await waitUntil { model.busy == nil }
        XCTAssertNil(model.availableRuntime)
        XCTAssertEqual(model.lastError, "Owned feed failure")
    }

    @MainActor
    func testOldChannelSuccessIsIgnored() async throws {
        let pending = PendingManifest()
        let model = AppModel(fetchRuntimeManifest: { try await pending.fetch() })
        model.updateChannelDidChange(.stable)
        model.checkForUpdates()
        try await waitUntil { pending.continuation != nil }
        model.updateChannelDidChange(.preview)
        pending.continuation?.resume(returning: manifest("owned-stable"))
        try await waitUntil { model.busy == nil }
        XCTAssertNil(model.availableRuntime)
        XCTAssertNil(model.lastError)
        XCTAssertFalse(model.lines.contains { $0.contains("owned-stable") })
    }

    @MainActor
    func testOldChannelErrorIsIgnored() async throws {
        let pending = PendingManifest()
        let model = AppModel(fetchRuntimeManifest: { try await pending.fetch() })
        model.updateChannelDidChange(.stable)
        model.checkForUpdates()
        try await waitUntil { pending.continuation != nil }
        model.updateChannelDidChange(.preview)
        pending.continuation?.resume(throwing: MetalFlightError.message("Obsolete failure"))
        try await waitUntil { model.busy == nil }
        XCTAssertNil(model.availableRuntime)
        XCTAssertNil(model.lastError)
        XCTAssertFalse(model.lines.contains { $0.contains("Obsolete failure") })
    }

    @MainActor
    func testSwitchingAwayAndBackStillIgnoresOldResult() async throws {
        let pending = PendingManifest()
        let model = AppModel(fetchRuntimeManifest: { try await pending.fetch() })
        model.updateChannelDidChange(.stable)
        model.checkForUpdates()
        try await waitUntil { pending.continuation != nil }
        model.updateChannelDidChange(.preview)
        model.updateChannelDidChange(.stable)
        pending.continuation?.resume(returning: manifest("owned-stale"))
        try await waitUntil { model.busy == nil }
        XCTAssertNil(model.availableRuntime)
        XCTAssertNil(model.lastError)
    }

    @MainActor
    func testInstallDiscardsManifestFromPreviousChannel() async throws {
        let pending = PendingManifest()
        let model = AppModel(fetchRuntimeManifest: { try await pending.fetch() })
        model.updateChannelDidChange(.stable)
        model.installRuntime()
        try await waitUntil { pending.continuation != nil }
        model.updateChannelDidChange(.preview)
        // If incorrectly used, this manifest fails validation before any download.
        pending.continuation?.resume(returning: manifest("../obsolete"))
        try await waitUntil { model.busy == nil }
        XCTAssertNil(model.availableRuntime)
        XCTAssertNil(model.installedRuntime)
        XCTAssertNil(model.lastError)
    }
}

@MainActor
private final class PendingManifest {
    var continuation: CheckedContinuation<ReleaseManifest, Error>?

    func fetch() async throws -> ReleaseManifest {
        try await withCheckedThrowingContinuation { continuation = $0 }
    }
}
