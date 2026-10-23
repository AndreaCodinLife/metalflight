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
    static func importLibrary(from source: URL) async throws {
        try Paths.ensure()
        if source.pathExtension == "dmg" {
            let mount = FileManager.default.temporaryDirectory.appendingPathComponent("metalflight-gptk-\(UUID().uuidString)")
            try FileManager.default.createDirectory(at: mount, withIntermediateDirectories: true)
            let attach = try await Shell.runAsync(URL(fileURLWithPath: "/usr/bin/hdiutil"),
                                                  ["attach", "-readonly", "-nobrowse", "-noautoopen", "-mountpoint", mount.path, source.path])
            guard attach == 0 else { throw MetalFlightError.message("Could not open the evaluation environment disk image.") }
            defer { _ = try? Shell.capture("/usr/bin/hdiutil", ["detach", mount.path, "-quiet"]) }
            try copy(mount.appendingPathComponent("redist/lib/external/libd3dshared.dylib"))
        } else {
            try copy(source)
        }
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
