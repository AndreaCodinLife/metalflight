import CryptoKit
import Foundation

/// Installs runtimes from the GitHub release manifest: download, verify SHA-256, extract,
/// switch the current symlink atomically, keep the previous runtime for rollback.
struct Updater {
    /// Overridable for testing: `defaults write com.andreacodinlife.metalflight manifestURL file:///...`
    static var manifestURL: URL {
        if let custom = ProcessInfo.processInfo.environment["METALFLIGHT_MANIFEST_URL"], let url = URL(string: custom) { return url }
        if let custom = UserDefaults.standard.string(forKey: "manifestURL"), let url = URL(string: custom) { return url }
        return URL(string: "https://github.com/AndreaCodinLife/metalflight/releases/latest/download/metalflight-manifest.json")!
    }

    static func fetchManifest() async throws -> ReleaseManifest {
        let (data, _) = try await URLSession.shared.data(from: manifestURL)
        let manifest = try JSONDecoder().decode(ReleaseManifest.self, from: data)
        guard manifest.schema == 1 else { throw MetalFlightError.message("Unsupported manifest schema \(manifest.schema)") }
        return manifest
    }

    static func installedVersion() -> String? {
        guard let runtime = Paths.resolvedRuntime() else { return nil }
        return runtime.lastPathComponent
    }

    static func install(_ runtime: ReleaseManifest.Runtime, progress: @escaping (String) -> Void) async throws {
        try Paths.ensure()
        let archive = Paths.downloads.appendingPathComponent("metalflight-runtime-\(runtime.version).tar.xz")
        if !(FileManager.default.fileExists(atPath: archive.path) && (try? sha256(of: archive)) == runtime.sha256) {
            progress("Downloading runtime \(runtime.version)…")
            let (temporary, _) = try await URLSession.shared.download(from: runtime.url)
            try? FileManager.default.removeItem(at: archive)
            try FileManager.default.moveItem(at: temporary, to: archive)
        }
        progress("Verifying…")
        guard try sha256(of: archive) == runtime.sha256 else {
            try? FileManager.default.removeItem(at: archive)
            throw MetalFlightError.message("The runtime download does not match its published SHA-256.")
        }
        progress("Extracting…")
        let staging = Paths.runtimes.appendingPathComponent(".staging-\(runtime.version)")
        try? FileManager.default.removeItem(at: staging)
        try FileManager.default.createDirectory(at: staging, withIntermediateDirectories: true)
        let status = try await Shell.runAsync(URL(fileURLWithPath: "/usr/bin/tar"), ["-xJf", archive.path, "-C", staging.path])
        guard status == 0 else { throw MetalFlightError.message("Extraction failed (\(status)).") }
        let contents = try FileManager.default.contentsOfDirectory(at: staging, includingPropertiesForKeys: nil)
        guard contents.count == 1, FileManager.default.fileExists(atPath: contents[0].appendingPathComponent("bin/wine").path) else {
            throw MetalFlightError.message("Unexpected runtime archive layout.")
        }
        let destination = Paths.runtimes.appendingPathComponent(runtime.version)
        try? FileManager.default.removeItem(at: destination)
        try FileManager.default.moveItem(at: contents[0], to: destination)
        try? FileManager.default.removeItem(at: staging)
        try activate(destination)
        progress("Runtime \(runtime.version) installed.")
    }

    /// Points runtime-current at `runtime`, keeping the previous target as runtime-previous.
    static func activate(_ runtime: URL) throws {
        let fm = FileManager.default
        if let old = try? fm.destinationOfSymbolicLink(atPath: Paths.currentRuntime.path), old != relative(runtime) {
            try replaceSymlink(at: Paths.previousRuntime, to: old)
        }
        try replaceSymlink(at: Paths.currentRuntime, to: relative(runtime))
    }

    static func rollback() throws {
        guard let previous = try? FileManager.default.destinationOfSymbolicLink(atPath: Paths.previousRuntime.path) else {
            throw MetalFlightError.message("No previous runtime to roll back to.")
        }
        let current = try? FileManager.default.destinationOfSymbolicLink(atPath: Paths.currentRuntime.path)
        try replaceSymlink(at: Paths.currentRuntime, to: previous)
        if let current { try replaceSymlink(at: Paths.previousRuntime, to: current) }
    }

    private static func relative(_ runtime: URL) -> String { "runtimes/" + runtime.lastPathComponent }

    /// rename(2) over the old link, so readers never see a missing runtime.
    private static func replaceSymlink(at link: URL, to target: String) throws {
        let temporary = link.deletingLastPathComponent().appendingPathComponent(".\(link.lastPathComponent).new")
        try? FileManager.default.removeItem(at: temporary)
        try FileManager.default.createSymbolicLink(atPath: temporary.path, withDestinationPath: target)
        guard rename(temporary.path, link.path) == 0 else { throw MetalFlightError.message("Could not switch \(link.lastPathComponent).") }
    }

    static func sha256(of file: URL) throws -> String {
        let handle = try FileHandle(forReadingFrom: file)
        defer { try? handle.close() }
        var hasher = SHA256()
        while let chunk = try handle.read(upToCount: 4 << 20), !chunk.isEmpty { hasher.update(data: chunk) }
        return hasher.finalize().map { String(format: "%02x", $0) }.joined()
    }
}
