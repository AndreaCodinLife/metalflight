import Foundation

/// Which Direct3D implementation the game uses. Mullion (shipped in the runtime) is the default.
/// D3DMetal is Apple's, imported from the user's own Game Porting Toolkit; it is offered only when the
/// runtime profile describes it and the exact qualified GPTK build has been imported.
enum Renderer: String, CaseIterable, Identifiable {
    case mullion
    case d3dmetal

    static let preferenceKey = "renderer"
    var id: String { rawValue }
    var title: String { self == .mullion ? "Mullion" : "D3DMetal (experimental)" }

    static var selected: Renderer {
        get { UserDefaults.standard.string(forKey: preferenceKey).flatMap(Renderer.init(rawValue:)) ?? .mullion }
        set { UserDefaults.standard.set(newValue.rawValue, forKey: preferenceKey) }
    }
}

/// The effective launch settings for one renderer: the profile, with the renderer's overlay applied.
struct LaunchConfiguration {
    let renderer: Renderer
    let dllOverrides: String
    let gameSettings: [String: String]
    let steamSettings: [String: String]
    /// system32 file name -> source file, copied into the prefix before each launch.
    let system32: [(name: String, source: URL)]

    /// - Parameters:
    ///   - runtime: the runtime directory (Profile paths are relative to it).
    ///   - redist: the imported GPTK redist (Paths.gptkRedist); required for D3DMetal.
    static func make(profile: Profile, renderer: Renderer, runtime: URL, redist: URL) throws -> LaunchConfiguration {
        var files = profile.system32.map { (name: URL(fileURLWithPath: $0).lastPathComponent, source: runtime.appendingPathComponent($0)) }
        guard renderer == .d3dmetal else {
            return LaunchConfiguration(renderer: .mullion, dllOverrides: profile.dllOverrides, gameSettings: profile.gameSettings,
                                       steamSettings: profile.steamSettings, system32: files)
        }
        guard let overlay = profile.renderers?[Renderer.d3dmetal.rawValue] else {
            throw MetalFlightError.message("This runtime does not include the D3DMetal renderer. Choose Mullion in Settings.")
        }
        for (name, relative) in overlay.system32Replace ?? [:] {
            guard let index = files.firstIndex(where: { $0.name.caseInsensitiveCompare(name) == .orderedSame }) else {
                throw MetalFlightError.message("The runtime profile replaces an unknown system32 file: \(name).")
            }
            files[index].source = runtime.appendingPathComponent(relative)
        }
        for (name, relative) in (overlay.gptkSystem32 ?? [:]).sorted(by: { $0.key < $1.key }) {
            files.append((name: name, source: redist.appendingPathComponent(relative)))
        }
        var settings = profile.gameSettings.merging(overlay.gameSettings) { _, new in new }
        settings["WINEDLLPATH"] = redist.appendingPathComponent("wine").path  // D3DMetal's builtins come from the user's GPTK
        var steam = profile.steamSettings
        if let extra = overlay.steamDllOverridesAppend, !extra.isEmpty {
            steam["WINEDLLOVERRIDES"] = [steam["WINEDLLOVERRIDES"], extra].compactMap { $0 }.filter { !$0.isEmpty }.joined(separator: ";")
        }
        return LaunchConfiguration(renderer: .d3dmetal, dllOverrides: overlay.dllOverrides, gameSettings: settings,
                                   steamSettings: steam, system32: files)
    }
}
