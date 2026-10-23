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
        refreshInstallation()
    }

    func refreshInstallation() {
        installedRuntime = Updater.installedVersion()
        gptkInstalled = GPTK.installed
        prefixReady = false; steamInstalled = false; gameInstalled = false
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
            let source = Updater.manifestURL.isFileURL ? "Local preview" : "Latest runtime"
            self.note("\(source): \(manifest.runtime.version)")
        }
    }

    func installRuntime() {
        guard !playing else { return }
        perform("Installing runtime…") {
            let manifest = try await Updater.fetchManifest()
            let alreadyActive = Updater.installedVersion() == manifest.runtime.version
            try await Updater.install(manifest.runtime, progress: { message in Task { @MainActor in self.note(message) } },
                                      beforeActivation: {
                if let current = try? self.launcher() { await current.wine.stopAll() }
            })
            self.availableRuntime = manifest.runtime.version
            if !alreadyActive, (try self.launcher()).prefixReady { try await self.launcher().preparePrefix() }
        }
    }

    func rollbackRuntime() {
        guard !playing else { return }
        perform("Rolling back the runtime…") {
            if let current = try? self.launcher() { await current.wine.stopAll() }
            try Updater.rollback(); try await self.launcher().preparePrefix()
        }
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

    func preparePrefix() {
        guard !playing else { return }
        perform("Creating the Windows environment…") { try await self.launcher().preparePrefix() }
    }

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
