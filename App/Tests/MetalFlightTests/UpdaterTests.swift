import XCTest
@testable import MetalFlight

final class UpdaterTests: XCTestCase {
    override class func setUp() {
        super.setUp()
        setenv("METALFLIGHT_SUPPORT_DIR", NSTemporaryDirectory() + "MetalFlightTests-" + UUID().uuidString, 1)
    }

    override func setUpWithError() throws {
        try? FileManager.default.removeItem(at: Paths.support)
        try Paths.ensure()
    }

    override class func tearDown() {
        try? FileManager.default.removeItem(at: Paths.support)
        super.tearDown()
    }

    private func fixture(_ version: String, extraRootEntry: String? = nil) throws -> ReleaseManifest.Runtime {
        let work = Paths.support.appendingPathComponent("fixture-" + UUID().uuidString)
        let root = work.appendingPathComponent("runtime")
        defer { try? FileManager.default.removeItem(at: work) }
        let share = root.appendingPathComponent("share/metalflight")
        try FileManager.default.createDirectory(at: share, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: root.appendingPathComponent("bin"), withIntermediateDirectories: true)
        for name in ["wine", "wineserver"] {
            let file = root.appendingPathComponent("bin/" + name)
            try "#!/bin/sh\nexit 0\n".write(to: file, atomically: true, encoding: .utf8)
            try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: file.path)
        }
        let profile = Profile(steamAppID: "2537590", dllOverrides: "", gameDebug: "-all", gameArguments: [],
                              gameSettings: [:], steamArguments: [], steamDebug: "-all", steamSettings: [:],
                              system32: [], helper: "helper.dll", readinessProbe: "probe.exe")
        try JSONEncoder().encode(profile).write(to: share.appendingPathComponent("profile.json"))
        let paths = ["bin/wine", "bin/wineserver", "share/metalflight/profile.json"]
        let inventory = try Dictionary(uniqueKeysWithValues: paths.map { ($0, try Updater.sha256(of: root.appendingPathComponent($0))) })
        try JSONSerialization.data(withJSONObject: ["files": inventory]).write(to: share.appendingPathComponent("inventory.json"))
        let archive = Paths.downloads.appendingPathComponent("metalflight-runtime-\(version).tar.xz")
        var entries = ["runtime"]
        if let extraRootEntry {
            try "owned extra entry".write(to: work.appendingPathComponent(extraRootEntry), atomically: true, encoding: .utf8)
            entries.append(extraRootEntry)
        }
        let process = try Shell.run(URL(fileURLWithPath: "/usr/bin/tar"), ["-cJf", archive.path, "-C", work.path] + entries)
        XCTAssertEqual(process.terminationStatus, 0)
        return ReleaseManifest.Runtime(version: version, url: URL(string: "https://example.invalid/runtime.tar.xz")!,
                                       sha256: try Updater.sha256(of: archive), size: Int64(try Data(contentsOf: archive).count))
    }

    func testRejectsUnsafeManifestBeforeWriting() async throws {
        for version in ["../prefix", "/tmp/runtime", ".", "", "v1/../../prefix"] {
            let runtime = ReleaseManifest.Runtime(version: version, url: URL(string: "https://example.invalid/runtime")!,
                                                  sha256: String(repeating: "a", count: 64), size: 1)
            do { try await Updater.install(runtime, progress: { _ in }); XCTFail("accepted \(version)") }
            catch { }
        }
        XCTAssertNil(Paths.resolvedRuntime())
    }

    func testInstallReinstallAndRollback() async throws {
        let first = try fixture("1.0")
        try await Updater.install(first, progress: { _ in })
        XCTAssertEqual(Updater.installedVersion(), "1.0")
        try await Updater.install(first, progress: { _ in })
        let second = try fixture("2.0")
        try await Updater.install(second, progress: { _ in })
        XCTAssertEqual(Updater.installedVersion(), "2.0")
        try Updater.rollback()
        XCTAssertEqual(Updater.installedVersion(), "1.0")
        try Updater.rollback()
        XCTAssertEqual(Updater.installedVersion(), "2.0")
    }

    func testFinderMetadataDoesNotPreventInstallation() async throws {
        try await Updater.install(fixture("metadata", extraRootEntry: ".DS_Store"), progress: { _ in })
        XCTAssertEqual(Updater.installedVersion(), "metadata")
    }

    func testUnexpectedHiddenArchiveEntryIsStillRejected() async throws {
        try await Updater.install(fixture("original"), progress: { _ in })
        let malformed = try fixture("unexpected", extraRootEntry: ".unexpected")
        do { try await Updater.install(malformed, progress: { _ in }); XCTFail("accepted unexpected archive entry") }
        catch { XCTAssertEqual(error.localizedDescription, "Unexpected runtime archive layout.") }
        XCTAssertEqual(Updater.installedVersion(), "original")
    }

    func testFailedUpdatePreservesInstalledRuntime() async throws {
        try await Updater.install(fixture("1.0"), progress: { _ in })
        let second = try fixture("2.0")
        let wrongSize = ReleaseManifest.Runtime(version: second.version, url: second.url, sha256: second.sha256, size: second.size + 1)
        do { try await Updater.install(wrongSize, progress: { _ in }); XCTFail("accepted bad size") } catch { }
        XCTAssertEqual(Updater.installedVersion(), "1.0")
        let collision = ReleaseManifest.Runtime(version: "1.0", url: second.url, sha256: second.sha256, size: second.size)
        do { try await Updater.install(collision, progress: { _ in }); XCTFail("overwrote immutable version") } catch { }
        XCTAssertEqual(Updater.installedVersion(), "1.0")
    }

    func testCorruptRollbackIsRefused() async throws {
        try await Updater.install(fixture("1.0"), progress: { _ in })
        try await Updater.install(fixture("2.0"), progress: { _ in })
        try "corrupted".write(to: Paths.runtimes.appendingPathComponent("1.0/bin/wine"), atomically: true, encoding: .utf8)
        XCTAssertThrowsError(try Updater.rollback())
        XCTAssertEqual(Updater.installedVersion(), "2.0")
    }

    func testLocalPreviewFeedImportsAndDoesNotRestartActiveRuntime() async throws {
        let fixture = try fixture("preview1")
        let archive = Paths.downloads.appendingPathComponent("metalflight-runtime-preview1.tar.xz")
        let local = Paths.support.appendingPathComponent("source.tar.xz")
        try FileManager.default.moveItem(at: archive, to: local)
        let runtime = ReleaseManifest.Runtime(version: fixture.version, url: local, sha256: fixture.sha256, size: fixture.size)
        let feed = Paths.support.appendingPathComponent("preview-manifest.json")
        try JSONEncoder().encode(ReleaseManifest(schema: 1, appVersion: "0.1.0", runtime: runtime)).write(to: feed)
        XCTAssertEqual(Updater.manifestURL, feed)
        let loaded = try await Updater.fetchManifest(from: feed)
        var activations = 0
        try await Updater.install(loaded.runtime, progress: { _ in }, beforeActivation: { activations += 1 })
        XCTAssertEqual(Updater.installedVersion(), "preview1")
        XCTAssertEqual(try Updater.sha256(of: archive), fixture.sha256)
        try await Updater.install(loaded.runtime, progress: { _ in }, beforeActivation: { activations += 1 })
        XCTAssertEqual(activations, 1, "Checking an already active version must not stop Steam")
    }

    func testLocalArchiveStillRequiresPublishedChecksum() async throws {
        let fixture = try fixture("invalid")
        let archive = Paths.downloads.appendingPathComponent("metalflight-runtime-invalid.tar.xz")
        let local = Paths.support.appendingPathComponent("source.tar.xz")
        try FileManager.default.moveItem(at: archive, to: local)
        let runtime = ReleaseManifest.Runtime(version: fixture.version, url: local,
                                              sha256: String(repeating: "a", count: 64), size: fixture.size)
        do { try await Updater.install(runtime, progress: { _ in }); XCTFail("accepted incorrect hash") } catch { }
        XCTAssertNil(Paths.resolvedRuntime())
        XCTAssertFalse(FileManager.default.fileExists(atPath: Paths.runtimes.appendingPathComponent("invalid").path))
    }

    /// Set METALFLIGHT_TEST_REAL_RUNTIME_ARCHIVE to exercise the updater against
    /// a packaged runtime in its normal isolated test support directory.
    func testPackagedRuntimeInstallReinstallAndRollback() async throws {
        guard let archivePath = ProcessInfo.processInfo.environment["METALFLIGHT_TEST_REAL_RUNTIME_ARCHIVE"] else {
            throw XCTSkip("Set METALFLIGHT_TEST_REAL_RUNTIME_ARCHIVE to qualify a release archive")
        }
        let archive = URL(fileURLWithPath: archivePath)
        let manifestPath = archive.deletingLastPathComponent().appendingPathComponent("metalflight-manifest.json")
        let manifest = try JSONDecoder().decode(ReleaseManifest.self, from: Data(contentsOf: manifestPath))
        let published = manifest.runtime
        let runtime = ReleaseManifest.Runtime(version: published.version, url: archive,
                                              sha256: published.sha256, size: published.size)
        let previous = try fixture("previous")

        try await Updater.install(previous, progress: { _ in })
        try await Updater.install(runtime, progress: { _ in })
        XCTAssertEqual(Updater.installedVersion(), runtime.version)
        try await Updater.install(runtime, progress: { _ in })
        try Updater.rollback()
        XCTAssertEqual(Updater.installedVersion(), "previous")
        try Updater.rollback()
        XCTAssertEqual(Updater.installedVersion(), runtime.version)
    }
}
