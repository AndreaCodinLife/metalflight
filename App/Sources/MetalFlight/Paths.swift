import Foundation

/// Everything MetalFlight writes lives under ~/Library/Application Support/MetalFlight.
enum Paths {
    /// METALFLIGHT_SUPPORT_DIR overrides the location (tests, CI).
    static let support: URL = {
        if let custom = ProcessInfo.processInfo.environment["METALFLIGHT_SUPPORT_DIR"], !custom.isEmpty {
            return URL(fileURLWithPath: custom, isDirectory: true)
        }
        return FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("MetalFlight", isDirectory: true)
    }()
    static var runtimes: URL { support.appendingPathComponent("runtimes", isDirectory: true) }
    /// Symlinks to the active and the previous runtime (for rollback).
    static var currentRuntime: URL { support.appendingPathComponent("runtime-current") }
    static var previousRuntime: URL { support.appendingPathComponent("runtime-previous") }
    static var prefix: URL { support.appendingPathComponent("prefix", isDirectory: true) }
    static var gptk: URL { support.appendingPathComponent("gptk", isDirectory: true) }
    static var d3dshared: URL { gptk.appendingPathComponent("libd3dshared.dylib") }
    static var downloads: URL { support.appendingPathComponent("downloads", isDirectory: true) }
    static var logs: URL { support.appendingPathComponent("logs", isDirectory: true) }

    /// Inside the prefix: C:\metalflight holds the CPU helper and its logs.
    static var prefixTools: URL { prefix.appendingPathComponent("drive_c/metalflight", isDirectory: true) }
    static var steam: URL { prefix.appendingPathComponent("drive_c/Program Files (x86)/Steam", isDirectory: true) }
    static var game: URL { steam.appendingPathComponent("steamapps/common/MSFS2024/FlightSimulator2024.exe") }

    static func ensure() throws {
        for dir in [support, runtimes, gptk, downloads, logs] {
            try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true,
                                                    attributes: [.posixPermissions: 0o700])
            try FileManager.default.setAttributes([.posixPermissions: 0o700], ofItemAtPath: dir.path)
        }
    }

    /// The directory the current-runtime symlink points to, if any.
    static func resolvedRuntime() -> URL? {
        guard let target = try? FileManager.default.destinationOfSymbolicLink(atPath: currentRuntime.path) else { return nil }
        let url = URL(fileURLWithPath: target, relativeTo: support).standardizedFileURL
        return FileManager.default.fileExists(atPath: url.appendingPathComponent("bin/wine").path) ? url : nil
    }
}
