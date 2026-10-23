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
        let (data, response) = try await URLSession.shared.data(from: manifestURL)
        try checkResponse(response)
        let manifest = try JSONDecoder().decode(ReleaseManifest.self, from: data)
        guard manifest.schema == 1 else { throw MetalFlightError.message("Unsupported manifest schema \(manifest.schema)") }
        try validate(manifest.runtime)
        return manifest
    }

    static func installedVersion() -> String? {
        guard let runtime = Paths.resolvedRuntime() else { return nil }
        return runtime.lastPathComponent
    }

    static func install(_ runtime: ReleaseManifest.Runtime, progress: @escaping (String) -> Void,
                        beforeActivation: () async throws -> Void = {}) async throws {
        try validate(runtime)
        try Paths.ensure()
        let destination = Paths.runtimes.appendingPathComponent(runtime.version)
        let receipt = destination.appendingPathComponent(".archive-sha256")
        if FileManager.default.fileExists(atPath: destination.path) {
            guard (try? String(contentsOf: receipt, encoding: .utf8)) == runtime.sha256 else {
                throw MetalFlightError.message("Runtime \(runtime.version) already exists with different or unknown contents. Publish a new version instead of replacing it.")
            }
            try verifyInventory(destination)
            try await beforeActivation()
            try activate(destination)
            return
        }
        let archive = Paths.downloads.appendingPathComponent("metalflight-runtime-\(runtime.version).tar.xz")
        if !(FileManager.default.fileExists(atPath: archive.path) && (try? sha256(of: archive)) == runtime.sha256) {
            progress("Downloading runtime \(runtime.version)…")
            let (temporary, response) = try await URLSession.shared.download(from: runtime.url)
            try checkResponse(response)
            try? FileManager.default.removeItem(at: archive)
            try FileManager.default.moveItem(at: temporary, to: archive)
        }
        progress("Verifying…")
        let attributes = try FileManager.default.attributesOfItem(atPath: archive.path)
        guard (attributes[.size] as? NSNumber)?.int64Value == runtime.size else {
            try? FileManager.default.removeItem(at: archive)
            throw MetalFlightError.message("The runtime download has an unexpected size.")
        }
        guard try sha256(of: archive) == runtime.sha256 else {
            try? FileManager.default.removeItem(at: archive)
            throw MetalFlightError.message("The runtime download does not match its published SHA-256.")
        }
        progress("Extracting…")
        let staging = Paths.runtimes.appendingPathComponent(".staging-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: staging, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: staging) }
        let status = try await Shell.runAsync(URL(fileURLWithPath: "/usr/bin/tar"), ["-xJf", archive.path, "-C", staging.path])
        guard status == 0 else { throw MetalFlightError.message("Extraction failed (\(status)).") }
        let contents = try FileManager.default.contentsOfDirectory(at: staging, includingPropertiesForKeys: nil)
        guard contents.count == 1, FileManager.default.fileExists(atPath: contents[0].appendingPathComponent("bin/wine").path) else {
            throw MetalFlightError.message("Unexpected runtime archive layout.")
        }
        try verifyInventory(contents[0])
        try runtime.sha256.write(to: contents[0].appendingPathComponent(".archive-sha256"), atomically: true, encoding: .utf8)
        try FileManager.default.moveItem(at: contents[0], to: destination)
        try await beforeActivation()
        try activate(destination)
        progress("Runtime \(runtime.version) installed.")
    }

    /// Points runtime-current at `runtime`, keeping the previous target as runtime-previous.
    static func activate(_ runtime: URL) throws {
        guard runtime.deletingLastPathComponent().standardizedFileURL == Paths.runtimes.standardizedFileURL else {
            throw MetalFlightError.message("Runtime must be inside MetalFlight's runtimes folder.")
        }
        try verifyInventory(runtime)
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
        let target = URL(fileURLWithPath: previous, relativeTo: Paths.support).standardizedFileURL
        guard target.deletingLastPathComponent() == Paths.runtimes.standardizedFileURL else {
            throw MetalFlightError.message("Invalid rollback target.")
        }
        try verifyInventory(target)
        let current = try? FileManager.default.destinationOfSymbolicLink(atPath: Paths.currentRuntime.path)
        try replaceSymlink(at: Paths.currentRuntime, to: previous)
        if let current { try replaceSymlink(at: Paths.previousRuntime, to: current) }
    }

    private static func relative(_ runtime: URL) -> String { "runtimes/" + runtime.lastPathComponent }

    static func validate(_ runtime: ReleaseManifest.Runtime) throws {
        guard runtime.version.range(of: #"^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$"#, options: .regularExpression) != nil,
              runtime.sha256.range(of: #"^[a-f0-9]{64}$"#, options: .regularExpression) != nil,
              runtime.size > 0,
              ["https", "http"].contains(runtime.url.scheme?.lowercased() ?? "") else {
            throw MetalFlightError.message("Invalid runtime version, checksum, size or download URL.")
        }
    }

    private static func checkResponse(_ response: URLResponse) throws {
        if let http = response as? HTTPURLResponse, !(200..<300).contains(http.statusCode) {
            throw MetalFlightError.message("Download failed (HTTP \(http.statusCode)).")
        }
    }

    static func verifyInventory(_ runtime: URL) throws {
        struct Inventory: Decodable { let files: [String: String] }
        let root = runtime.resolvingSymlinksInPath().standardizedFileURL
        let inventory = try JSONDecoder().decode(Inventory.self, from: Data(contentsOf:
            runtime.appendingPathComponent("share/metalflight/inventory.json")))
        guard !inventory.files.isEmpty else { throw MetalFlightError.message("Empty runtime inventory.") }
        for (path, checksum) in inventory.files {
            let file = runtime.appendingPathComponent(path).resolvingSymlinksInPath().standardizedFileURL
            guard file.path.hasPrefix(root.path + "/"), try sha256(of: file) == checksum else {
                throw MetalFlightError.message("Runtime verification failed: \(path)")
            }
        }
        guard FileManager.default.isExecutableFile(atPath: runtime.appendingPathComponent("bin/wine").path),
              FileManager.default.isExecutableFile(atPath: runtime.appendingPathComponent("bin/wineserver").path) else {
            throw MetalFlightError.message("The runtime is missing its Wine executables.")
        }
        _ = try Profile.load(runtime: runtime)
    }

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
