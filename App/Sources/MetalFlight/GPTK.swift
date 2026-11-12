import Foundation

/// Imports libd3dshared.dylib from the user's own Apple Game Porting Toolkit download.
/// MetalFlight never ships Apple's file: the user downloads the toolkit from developer.apple.com
/// (accepting Apple's licence there), mounts it, and MetalFlight copies the one library it needs.
enum GPTK {
    static let requirement = "=identifier \"com.apple.libd3dshared\" and anchor apple"

    static var installed: Bool { FileManager.default.fileExists(atPath: Paths.d3dshared.path) && verify(Paths.d3dshared) }

    static func verify(_ library: URL) -> Bool {
        guard let result = try? Shell.capture("/usr/bin/codesign", ["--verify", "--strict", "-R", requirement, library.path]) else { return false }
        return result.0 == 0
    }

    /// Looks for the evaluation environment in mounted volumes: either its own volume, or the
    /// toolkit volume that contains the evaluation environment disk image.
    static func findSource() -> URL? {
        let volumes = (try? FileManager.default.contentsOfDirectory(at: URL(fileURLWithPath: "/Volumes"), includingPropertiesForKeys: nil)) ?? []
        for volume in volumes {
            let direct = volume.appendingPathComponent("redist/lib/external/libd3dshared.dylib")
            if FileManager.default.fileExists(atPath: direct.path) { return direct }
            let items = (try? FileManager.default.contentsOfDirectory(at: volume, includingPropertiesForKeys: nil)) ?? []
            if let image = items.first(where: { $0.lastPathComponent.hasPrefix("Evaluation environment for Windows games") && $0.pathExtension == "dmg" }) {
                return image
            }
        }
        return nil
    }

    /// `source` is the library itself, or an evaluation-environment disk image.
    /// With `d3dmetal` pins (from the runtime profile) and a disk image, the whole redist is also imported for the
    /// D3DMetal renderer; the result says whether that succeeded. libd3dshared alone is still imported otherwise.
    @discardableResult
    static func importLibrary(from source: URL, d3dmetal: RendererProfile? = nil) async throws -> D3DMetalImport {
        try Paths.ensure()
        if source.pathExtension == "dmg" {
            let mount = FileManager.default.temporaryDirectory.appendingPathComponent("metalflight-gptk-\(UUID().uuidString)")
            try FileManager.default.createDirectory(at: mount, withIntermediateDirectories: true)
            let attach = try await Shell.runAsync(URL(fileURLWithPath: "/usr/bin/hdiutil"),
                                                  ["attach", "-readonly", "-nobrowse", "-noautoopen", "-mountpoint", mount.path, source.path])
            guard attach == 0 else { throw MetalFlightError.message("Could not open the evaluation environment disk image.") }
            defer { _ = try? Shell.capture("/usr/bin/hdiutil", ["detach", mount.path, "-quiet"]) }
            let lib = mount.appendingPathComponent("redist/lib")
            try copy(lib.appendingPathComponent("external/libd3dshared.dylib"))
            guard let d3dmetal, let pins = d3dmetal.gptk else { return .notRequested }
            do {
                try importRedist(from: lib, pins: pins, aliases: d3dmetal.gptkAliases ?? [:], into: Paths.gptkRedist)
                return .imported(pins.name)
            } catch {
                return .unsupported(error.localizedDescription)
            }
        } else {
            try copy(source)
            return .notRequested
        }
    }

    enum D3DMetalImport: Equatable {
        case notRequested, imported(String), unsupported(String)
    }

    // MARK: D3DMetal redist (Apple's files from the user's own toolkit; MetalFlight ships none of them)

    /// Copies `lib` (the toolkit's redist/lib) to a staging folder, verifies it against `pins`, adds the aliases,
    /// then replaces `destination`. Nothing is installed unless every check passes.
    static func importRedist(from lib: URL, pins: GPTKPins, aliases: [String: String], into destination: URL,
                             signatureValid: (URL) -> Bool = { GPTK.verifyApple($0) }) throws {
        let fm = FileManager.default
        try fm.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
        let staging = destination.deletingLastPathComponent().appendingPathComponent(".redist-import-\(UUID().uuidString)")
        defer { try? fm.removeItem(at: staging) }
        // cp -R keeps the toolkit's symlinks and the framework's bundle layout and signature.
        let status = try Shell.capture("/bin/cp", ["-R", lib.path, staging.path]).0
        guard status == 0 else { throw MetalFlightError.message("Could not copy the Game Porting Toolkit redist.") }
        if let problem = redistProblem(staging, pins: pins, signatureValid: signatureValid) {
            throw MetalFlightError.message("This Game Porting Toolkit is not the qualified \(pins.name): \(problem)")
        }
        for (name, target) in aliases.sorted(by: { $0.key < $1.key }) {
            let alias = staging.appendingPathComponent(name)
            try? fm.removeItem(at: alias)
            if name.hasSuffix(".so") { try fm.createSymbolicLink(atPath: alias.path, withDestinationPath: target) }
            else { try fm.copyItem(at: staging.appendingPathComponent(target), to: alias) }
        }
        try? fm.removeItem(at: destination)
        try fm.moveItem(at: staging, to: destination)
    }

    /// Cheap check for the setup view: every pinned file and alias exists (full verification runs at import and Play).
    static func redistPresent(_ renderer: RendererProfile?, at redist: URL = Paths.gptkRedist) -> Bool {
        guard let renderer, let pins = renderer.gptk else { return false }
        let names = Array(pins.redistFiles.keys) + Array((renderer.gptkAliases ?? [:]).keys)
        return names.allSatisfy { FileManager.default.fileExists(atPath: redist.appendingPathComponent($0).path) }
    }

    /// True when the imported redist still matches the pins (and its aliases exist).
    static func redistInstalled(_ renderer: RendererProfile?, at redist: URL = Paths.gptkRedist,
                                signatureValid: (URL) -> Bool = { GPTK.verifyApple($0) }) -> Bool {
        guard let renderer, let pins = renderer.gptk else { return false }
        let fm = FileManager.default
        for name in (renderer.gptkAliases ?? [:]).keys where !fm.fileExists(atPath: redist.appendingPathComponent(name).path) { return false }
        return redistProblem(redist, pins: pins, ignoring: Set((renderer.gptkAliases ?? [:]).keys), signatureValid: signatureValid) == nil
    }

    /// nil when `root` holds exactly the pinned files and links (plus `ignoring`) and the signed items verify.
    static func redistProblem(_ root: URL, pins: GPTKPins, ignoring: Set<String> = [],
                              signatureValid: (URL) -> Bool) -> String? {
        let fm = FileManager.default
        var files: [String: URL] = [:], links: [String: String] = [:]
        guard let walker = fm.enumerator(at: root, includingPropertiesForKeys: [.isSymbolicLinkKey, .isRegularFileKey]) else {
            return "the redist folder is missing"
        }
        let base = root.standardizedFileURL.path
        for case let url as URL in walker {
            let relative = String(url.standardizedFileURL.path.dropFirst(base.count + 1))
            if ignoring.contains(relative) { continue }
            let values = try? url.resourceValues(forKeys: [.isSymbolicLinkKey, .isRegularFileKey])
            if values?.isSymbolicLink == true {
                // The enumerator does not follow links. skipDescendants() here would skip the rest of the parent
                // directory (it applies to the most recent real subdirectory, e.g. a framework's other entries).
                links[relative] = (try? fm.destinationOfSymbolicLink(atPath: url.path)) ?? ""
            } else if values?.isRegularFile == true {
                files[relative] = url
            }
        }
        if links != pins.redistLinks { return "its symbolic links differ" }
        if Set(files.keys) != Set(pins.redistFiles.keys) { return "its file list differs" }
        for (relative, url) in files where (try? Updater.sha256(of: url)) != pins.redistFiles[relative] { return "\(relative) differs" }
        for relative in pins.signed where !signatureValid(root.appendingPathComponent(relative)) { return "\(relative) is not signed by Apple" }
        return nil
    }

    static func verifyApple(_ item: URL) -> Bool {
        guard let result = try? Shell.capture("/usr/bin/codesign", ["--verify", "--strict", "-R", "=anchor apple", item.path]) else { return false }
        return result.0 == 0
    }

    private static func copy(_ library: URL) throws {
        guard FileManager.default.fileExists(atPath: library.path) else {
            throw MetalFlightError.message("libd3dshared.dylib was not found in the Game Porting Toolkit.")
        }
        guard verify(library) else { throw MetalFlightError.message("libd3dshared.dylib is not signed by Apple.") }
        try? FileManager.default.removeItem(at: Paths.d3dshared)
        try FileManager.default.copyItem(at: library, to: Paths.d3dshared)
    }
}
