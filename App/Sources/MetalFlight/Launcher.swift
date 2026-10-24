import Foundation

/// Prefix creation, Steam installation, and the Steam + MSFS 2024 launch sequence.
struct Launcher {
    let wine: Wine
    let profile: Profile

    init(runtime: URL) throws {
        wine = Wine(runtime: runtime)
        profile = try Profile.load(runtime: runtime)
    }

    static let steamInstaller = URL(string: "https://cdn.akamai.steamstatic.com/client/installer/SteamSetup.exe")!

    var prefixReady: Bool { FileManager.default.fileExists(atPath: Paths.prefix.appendingPathComponent("system.reg").path) }
    var steamInstalled: Bool { FileManager.default.fileExists(atPath: Paths.steam.appendingPathComponent("steam.exe").path) }
    var gameInstalled: Bool { FileManager.default.fileExists(atPath: Paths.game.path) }

    func log(_ name: String) -> URL { Paths.logs.appendingPathComponent(name) }

    /// Creates the prefix, or updates it after a runtime change (wineboot -u is idempotent).
    func preparePrefix() async throws {
        let status = try await wine.run("wineboot", [prefixReady ? "-u" : "-i"],
                                        environment: wine.environment(dllOverrides: "mscoree,mshtml="), log: log("wineboot.log"))
        await wine.stopAll()
        guard status == 0 else { throw MetalFlightError.message("Creating the Windows environment failed (wineboot \(status)).") }
    }

    /// Downloads Valve's official installer and runs it silently in the prefix.
    func installSteam() async throws {
        try Paths.ensure()
        let installer = Paths.downloads.appendingPathComponent("SteamSetup.exe")
        let (temporary, _) = try await URLSession.shared.download(from: Self.steamInstaller)
        try? FileManager.default.removeItem(at: installer)
        try FileManager.default.moveItem(at: temporary, to: installer)
        let status = try await wine.run(installer.path, ["/S"], log: log("steam-install.log"))
        guard status == 0, steamInstalled else { throw MetalFlightError.message("The Steam installer did not finish (\(status)).") }
    }

    /// Copies the patched system32 files and the CPU helper into the prefix (every launch,
    /// because a prefix update may restore Wine's own builtins).
    func applyPatches() throws {
        let fm = FileManager.default
        let system32 = Paths.prefix.appendingPathComponent("drive_c/windows/system32")
        for relative in profile.system32 {
            let source = wine.runtime.appendingPathComponent(relative)
            let destination = system32.appendingPathComponent(source.lastPathComponent)
            try? fm.removeItem(at: destination)
            try fm.copyItem(at: source, to: destination)
        }
        try fm.createDirectory(at: Paths.prefixTools.appendingPathComponent("logs"), withIntermediateDirectories: true)
        let helper = Paths.prefixTools.appendingPathComponent("split_push_helper.dll")
        try? fm.removeItem(at: helper)
        try fm.copyItem(at: wine.runtime.appendingPathComponent(profile.helper), to: helper)
    }

    var steamEnvironment: [String: String] { wine.environment(debug: profile.steamDebug, settings: profile.steamSettings) }

    /// True when the Steam client is running, signed in and owns the game (genuine SDK check).
    func steamReady() async -> Bool {
        guard gameInstalled else { return false }
        let probe = wine.runtime.appendingPathComponent(profile.readinessProbe)
        let api = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\MSFS2024\\steam_api64.dll"
        let status = try? await wine.run(probe.path, [api, "--loader-results"], environment: steamEnvironment, log: log("steam-ready.log"))
        return status == 0
    }

    func startSteam() throws {
        // Steam's CEF also needs the native D3D11 pair selected by its profile.
        try applyPatches()
        _ = try wine.start(Paths.steam.appendingPathComponent("steam.exe").path, profile.steamArguments,
                           environment: steamEnvironment, directory: Paths.steam, log: log("steam.log"))
    }

    /// Starts Steam if needed, waits for genuine readiness, then launches the game.
    func play(status: @escaping (String) -> Void) async throws -> Process {
        guard gameInstalled else { throw MetalFlightError.message("Install Microsoft Flight Simulator 2024 from Steam first.") }
        try await VisualCpp.ensure(wine: wine, status: status)
        try applyPatches()
        if !(await steamReady()) {
            status("Starting Steam…")
            try startSteam()
            var ready = false
            for _ in 0..<48 {
                try await Task.sleep(nanoseconds: 5_000_000_000)
                if await steamReady() { ready = true; break }
            }
            guard ready else { throw MetalFlightError.message("Steam did not become ready. Sign in to Steam, then press Play again.") }
        }
        status("Launching Microsoft Flight Simulator 2024…")
        let stamp = Int(Date().timeIntervalSince1970)
        let settings = profile.gameSettings.mapValues { $0.replacingOccurrences(of: "{log}", with: "C:\\metalflight\\logs\\cpu-\(stamp).txt") }
        return try wine.start(Paths.game.path, profile.gameArguments,
                              environment: wine.environment(dllOverrides: profile.dllOverrides, debug: profile.gameDebug, settings: settings),
                              directory: Paths.game.deletingLastPathComponent(), log: log("game-\(stamp).log"))
    }
}
