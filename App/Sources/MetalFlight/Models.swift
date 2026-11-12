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
    /// Optional renderers besides the default (Mullion), keyed by Renderer.rawValue. Absent in older profiles.
    var renderers: [String: RendererProfile]? = nil

    static func load(runtime: URL) throws -> Profile {
        let data = try Data(contentsOf: runtime.appendingPathComponent("share/metalflight/profile.json"))
        return try JSONDecoder().decode(Profile.self, from: data)
    }
}

/// A renderer overlay on the default profile (share/metalflight/profile.json "renderers").
struct RendererProfile: Codable {
    let label: String
    let description: String
    /// Replaces Profile.dllOverrides for the game.
    let dllOverrides: String
    /// Merged over Profile.gameSettings for the game.
    let gameSettings: [String: String]
    /// Appended to the Steam WINEDLLOVERRIDES.
    var steamDllOverridesAppend: String? = nil
    /// system32 file name -> runtime-relative replacement for the Profile.system32 entry of that name.
    var system32Replace: [String: String]? = nil
    /// system32 file name -> file inside the imported GPTK redist copied there before each launch.
    var gptkSystem32: [String: String]? = nil
    /// Files created inside the imported redist: name -> source file (copy) or symlink target (".so" names).
    var gptkAliases: [String: String]? = nil
    /// The exact Game Porting Toolkit build this renderer was qualified with; nothing else is accepted.
    let gptk: GPTKPins?
}

struct GPTKPins: Codable {
    let name: String
    /// redist/lib-relative path -> SHA-256 of every regular file (framework contents included).
    let redistFiles: [String: String]
    /// redist/lib-relative path -> symlink target.
    let redistLinks: [String: String]
    /// redist/lib-relative items that must carry Apple's code signature.
    let signed: [String]
}

enum MetalFlightError: LocalizedError {
    case message(String)
    var errorDescription: String? { if case .message(let text) = self { return text }; return nil }
}
