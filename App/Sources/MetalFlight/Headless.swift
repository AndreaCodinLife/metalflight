import Foundation

/// `MetalFlight --headless <command>`: the app's own code paths without the window (tests, CI).
///   status | check-runtime | install-runtime | rollback | import-gptk [PATH] | prepare-prefix | apply-patches | install-steam | start-steam
///   run PROGRAM [ARGS...]   (in the prefix, with the game's environment and DLL overrides)
enum Headless {
    static func run(_ arguments: [String]) -> Int32 {
        var status: Int32 = 0
        let done = DispatchSemaphore(value: 0)
        Task.detached {
            do { try await execute(arguments) } catch { print("error: \(error.localizedDescription)"); status = 1 }
            done.signal()
        }
        done.wait()
        return status
    }

    private static func launcher() throws -> Launcher {
        guard let runtime = Paths.resolvedRuntime() else { throw MetalFlightError.message("no runtime installed") }
        return try Launcher(runtime: runtime)
    }

    private static func execute(_ arguments: [String]) async throws {
        try Paths.ensure()
        switch arguments.first {
        case "status":
            let s = SystemCheck.run()
            print("support: \(Paths.support.path)")
            print("system: arm64=\(s.appleSilicon) macOS=\(s.macOSVersion) rosetta=\(s.rosetta) freeGB=\(s.freeGigabytes)")
            print("runtime: \(Updater.installedVersion() ?? "none")  gptk: \(GPTK.installed)")
            if let l = try? launcher() { print("prefix: \(l.prefixReady)  steam: \(l.steamInstalled)  game: \(l.gameInstalled)") }
        case "install-runtime":
            let manifest = try await Updater.fetchManifest()
            try await Updater.install(manifest.runtime, progress: { print($0) }, beforeActivation: {
                if let runtime = Paths.resolvedRuntime() { await Wine(runtime: runtime).stopAll() }
            })
        case "check-runtime":
            let manifest = try await Updater.fetchManifest()
            print("source: \(Updater.manifestURL.isFileURL ? "local preview" : "release feed")")
            print("available runtime: \(manifest.runtime.version)")
        case "rollback":
            if let runtime = Paths.resolvedRuntime() { await Wine(runtime: runtime).stopAll() }
            try Updater.rollback(); print("runtime: \(Updater.installedVersion() ?? "none")")
        case "import-gptk":
            guard let source = arguments.count > 1 ? URL(fileURLWithPath: arguments[1]) : GPTK.findSource() else {
                throw MetalFlightError.message("no Game Porting Toolkit source found")
            }
            try await GPTK.importLibrary(from: source); print("gptk: imported and verified")
        case "prepare-prefix":
            try await launcher().preparePrefix(); print("prefix: ready")
        case "apply-patches":
            try launcher().applyPatches(); print("patches: applied")
        case "install-steam":
            try await launcher().installSteam(); print("steam: installed")
        case "start-steam":
            try launcher().startSteam(); print("steam: started")
        case "run":
            guard arguments.count > 1 else { throw MetalFlightError.message("run needs a program") }
            let l = try launcher()
            try l.applyPatches()
            let settings = l.profile.gameSettings.mapValues { $0.replacingOccurrences(of: "{log}", with: "C:\\metalflight\\logs\\headless.txt") }
            let env = l.wine.environment(dllOverrides: l.profile.dllOverrides, debug: "-all", settings: settings)
            let code = try await l.wine.run(arguments[1], Array(arguments.dropFirst(2)), environment: env, log: l.log("headless-run.log"))
            await l.wine.stopAll()
            print("exit: \(code)")
            if code != 0 { throw MetalFlightError.message("program exited \(code)") }
        default:
            throw MetalFlightError.message("unknown command; see Headless.swift")
        }
    }
}
