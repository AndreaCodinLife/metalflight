import AppKit
import Foundation
import Observation

@MainActor @Observable
final class AppModel {
    var system = SystemCheck()
    var installedRuntime: String?
    var availableRuntime: String?
    var gptkInstalled = false
    var prefixReady = false
    var steamInstalled = false
    var gameInstalled = false
    var playing = false
    var busy: String?
    var lastError: String?
    var lines: [String] = []

    private var game: Process?

    func refresh() {
        system = SystemCheck.run()
        installedRuntime = Updater.installedVersion()
        gptkInstalled = GPTK.installed
        if let runtime = Paths.resolvedRuntime(), let launcher = try? Launcher(runtime: runtime) {
            prefixReady = launcher.prefixReady
            steamInstalled = launcher.steamInstalled
            gameInstalled = launcher.gameInstalled
        }
        playing = game?.isRunning ?? false
    }

    func note(_ text: String) { lines.append(text); if lines.count > 300 { lines.removeFirst(lines.count - 300) } }

    private func perform(_ title: String, _ work: @escaping () async throws -> Void) {
        guard busy == nil else { return }
        busy = title; lastError = nil; note(title)
        Task {
            do { try await work() } catch { lastError = error.localizedDescription; note("Error: \(error.localizedDescription)") }
            busy = nil
            refresh()
        }
    }

    private func launcher() throws -> Launcher {
        guard let runtime = Paths.resolvedRuntime() else { throw MetalFlightError.message("Install the runtime first.") }
        return try Launcher(runtime: runtime)
    }

    func checkForUpdates() {
        perform("Checking for updates…") {
            let manifest = try await Updater.fetchManifest()
            self.availableRuntime = manifest.runtime.version
            self.note("Latest runtime: \(manifest.runtime.version)")
        }
    }

    func installRuntime() {
        perform("Installing runtime…") {
            let manifest = try await Updater.fetchManifest()
            try await Updater.install(manifest.runtime) { message in Task { @MainActor in self.note(message) } }
            self.availableRuntime = manifest.runtime.version
            if (try self.launcher()).prefixReady { try await self.launcher().preparePrefix() }
        }
    }

    func rollbackRuntime() {
        perform("Rolling back the runtime…") { try Updater.rollback(); try await self.launcher().preparePrefix() }
    }

    func importGPTK(from url: URL? = nil) {
        perform("Importing libd3dshared from the Game Porting Toolkit…") {
            guard let source = url ?? GPTK.findSource() else {
                throw MetalFlightError.message("Mount the Game Porting Toolkit disk image downloaded from developer.apple.com, then try again.")
            }
            try await GPTK.importLibrary(from: source)
            self.note("libd3dshared.dylib imported and verified (Apple signature).")
        }
    }

    func chooseGPTK() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = []
        panel.allowsOtherFileTypes = true
        panel.message = "Choose “Evaluation environment for Windows games … .dmg” or libd3dshared.dylib"
        if panel.runModal() == .OK, let url = panel.url { importGPTK(from: url) }
    }

    func preparePrefix() { perform("Creating the Windows environment…") { try await self.launcher().preparePrefix() } }

    func installSteam() {
        perform("Installing Steam…") {
            try await self.launcher().installSteam()
            try self.launcher().startSteam()
            self.note("Steam is starting: sign in, then install Microsoft Flight Simulator 2024.")
        }
    }

    func openSteam() { perform("Starting Steam…") { try self.launcher().startSteam() } }

    func play() {
        perform("Preparing launch…") {
            let process = try await self.launcher().play { message in Task { @MainActor in self.note(message) } }
            self.game = process
            self.playing = true
            Task.detached {
                process.waitUntilExit()
                let code = process.terminationStatus
                await MainActor.run { self.playing = false; self.note("The game exited (\(code)).") }
            }
        }
    }

    func stopEverything() {
        perform("Stopping Wine…") { try await self.launcher().wine.stopAll() }
    }

    func openLogs() { NSWorkspace.shared.open(Paths.logs) }
}
