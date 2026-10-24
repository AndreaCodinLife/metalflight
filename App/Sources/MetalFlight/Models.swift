import Foundation

/// Published as a release asset (metalflight-manifest.json); points at the runtime archive.
struct ReleaseManifest: Codable {
    struct Runtime: Codable {
        let version: String
        let url: URL
        let sha256: String
        let size: Int64
    }
    let schema: Int
    let appVersion: String
    let runtime: Runtime
}

/// Shipped inside each runtime (share/metalflight/profile.json): how to launch Steam and the game.
struct Profile: Codable {
    let steamAppID: String
    let dllOverrides: String
    let gameDebug: String
    let gameArguments: [String]
    /// Placeholders: {log} becomes a fresh C:\metalflight\logs\ file per launch.
    let gameSettings: [String: String]
    let steamArguments: [String]
    let steamDebug: String
    let steamSettings: [String: String]
    /// Relative to the runtime: PE files copied into the prefix's system32 before each launch.
    let system32: [String]
    /// Relative to the runtime: the CPU helper copied to C:\metalflight\split_push_helper.dll.
    let helper: String
    /// Relative to the runtime: the Steam SDK readiness probe.
    let readinessProbe: String
    /// Exact executable hashes supported by this runtime's CPU patch table.
    /// Absent in older profiles, which support only the original game build.
    var supportedGameBuilds: [String: String]? = nil

    static func load(runtime: URL) throws -> Profile {
        let data = try Data(contentsOf: runtime.appendingPathComponent("share/metalflight/profile.json"))
        return try JSONDecoder().decode(Profile.self, from: data)
    }
}

enum MetalFlightError: LocalizedError {
    case message(String)
    var errorDescription: String? { if case .message(let text) = self { return text }; return nil }
}
