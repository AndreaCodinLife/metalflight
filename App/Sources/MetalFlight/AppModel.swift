import AppKit
import Foundation
import Observation

@MainActor @Observable
final class AppModel {
    var system = SystemCheck()
    var installedRuntime: String?
    var availableRuntime: String?
    private(set) var updateChannel = UpdateChannel.selected
    var gptkInstalled = false
    /// The installed runtime offers the D3DMetal renderer / its toolkit files are imported.
    var d3dmetalSupported = false
    var d3dmetalImported = false
    var renderer = Renderer.selected
    var prefixReady = false
    var steamInstalled = false
    var gameInstalled = false
    var playing = false
    var busy: String?
    var lastError: String?
    var lines: [String] = []

    private var game: Process?
    private var memoryErrorMonitor: Task<Void, Never>?
    private var memoryErrorAlertShown = false
    @ObservationIgnored private var runtimeRequestGeneration = UUID()
    @ObservationIgnored private let fetchRuntimeManifest: () async throws -> ReleaseManifest

    init(fetchRuntimeManifest: @escaping () async throws -> ReleaseManifest = { try await Updater.fetchManifest() }) {
        self.fetchRuntimeManifest = fetchRuntimeManifest
    }

    func updateChannelDidChange(_ channel: UpdateChannel) {
        updateChannel = channel
        runtimeRequestGeneration = UUID()
        availableRuntime = nil
        lastError = nil
    }

    /// A channel may change while the network is suspended, including away and back.
    private func currentRuntimeManifest() async throws -> ReleaseManifest? {
        let generation = runtimeRequestGeneration
        do {
            let manifest = try await fetchRuntimeManifest()
            return generation == runtimeRequestGeneration ? manifest : nil
        } catch {
            guard generation == runtimeRequestGeneration else { return nil }
            throw error
        }
    }

    func refresh() {
        system = SystemCheck.run()
        refreshInstallation()
    }

    func refreshInstallation() {
        installedRuntime = Updater.installedVersion()
        gptkInstalled = GPTK.installed
        prefixReady = false; steamInstalled = false; gameInstalled = false
        d3dmetalSupported = false; d3dmetalImported = false
        if let runtime = Paths.resolvedRuntime(), let launcher = try? Launcher(runtime: runtime) {
            prefixReady = launcher.prefixReady
            steamInstalled = launcher.steamInstalled
            gameInstalled = launcher.gameInstalled
            let overlay = launcher.profile.renderers?[Renderer.d3dmetal.rawValue]
            d3dmetalSupported = overlay != nil
            d3dmetalImported = GPTK.redistPresent(overlay)
        }
        renderer = Renderer.selected
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
            self.availableRuntime = nil
            let source = Updater.manifestURL.isFileURL ? "Local preview" : "Latest \(self.updateChannel.title.lowercased()) runtime"
            guard let manifest = try await self.currentRuntimeManifest() else { return }
            self.availableRuntime = manifest.runtime.version
            self.note("\(source): \(manifest.runtime.version)")
        }
    }

    func installRuntime() {
        guard !playing else { return }
        perform("Installing runtime…") {
            let generation = self.runtimeRequestGeneration
            guard let manifest = try await self.currentRuntimeManifest() else { return }
            let alreadyActive = Updater.installedVersion() == manifest.runtime.version
            try await Updater.install(manifest.runtime, progress: { message in Task { @MainActor in self.note(message) } },
                                      beforeActivation: {
                if let current = try? self.launcher() { await current.wine.stopAll() }
            })
            if generation == self.runtimeRequestGeneration { self.availableRuntime = manifest.runtime.version }
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
        perform("Importing from the Game Porting Toolkit…") {
            guard let source = url ?? GPTK.findSource() else {
                throw MetalFlightError.message("Mount the Game Porting Toolkit disk image downloaded from developer.apple.com, then try again.")
            }
            let overlay = Paths.resolvedRuntime().flatMap { try? Profile.load(runtime: $0) }?.renderers?[Renderer.d3dmetal.rawValue]
            let result = try await GPTK.importLibrary(from: source, d3dmetal: overlay)
            self.note("libd3dshared.dylib imported and verified (Apple signature).")
            switch result {
            case .notRequested: break
            case .imported(let name): self.note("D3DMetal imported from \(name) and verified (Apple signatures, qualified file hashes).")
            case .unsupported(let reason): self.note("D3DMetal not imported: \(reason). Mullion is unaffected.")
            }
        }
    }

    func rendererDidChange(_ renderer: Renderer) {
        Renderer.selected = renderer
        self.renderer = renderer
        note(renderer == .d3dmetal ? "Renderer: D3DMetal (experimental) for the next launch." : "Renderer: Mullion for the next launch.")
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
        guard busy == nil, !playing else { return }
        if ControllerMonitor.hasConnectedController() == false {
            let alert = NSAlert()
            alert.alertStyle = .warning
            alert.messageText = "Connect your controller before launching"
            alert.informativeText = "No HOTAS or game controller is connected. Connect it before starting MSFS 2024. Controllers connected after launch may not appear until you restart the simulator."
            alert.addButton(withTitle: "Cancel")
            alert.addButton(withTitle: "Launch anyway")
            NSApp.activate(ignoringOtherApps: true)
            guard alert.runModal() == .alertSecondButtonReturn else { return }
        }
        perform("Preparing launch…") { [self] in
            let launch = try await self.launcher().play { message in Task { @MainActor in self.note(message) } }
            let process = launch.process
            self.memoryErrorMonitor?.cancel()
            self.memoryErrorAlertShown = false
            self.memoryErrorMonitor = GameMemoryErrorMonitor.watch(log: launch.log, process: process) { [weak self] in
                self?.showMemoryErrorAlert()
            }
            self.game = process
            self.playing = true
            Task.detached {
                process.waitUntilExit()
                let code = process.terminationStatus
                try? launch.resolutionOverride?.restore()
                await MainActor.run {
                    self.playing = false
                    if code == 53 {
                        self.note("The game could not load a required Windows library (exit 53). Check the newest game log using Logs.")
                    } else { self.note("The game exited (\(code)).") }
                }
            }
        }
    }

    private func showMemoryErrorAlert() {
        guard !memoryErrorAlertShown else { return }
        memoryErrorAlertShown = true

        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = "MSFS: insufficient graphics memory"
        alert.informativeText = "The game's graphics device reported insufficient memory. The game may have stopped responding.\n\nClose the game before relaunching. Diagnostic details have been saved in MetalFlight's Logs."
        alert.addButton(withTitle: "OK")
        NSApp.activate(ignoringOtherApps: true)
        alert.runModal()
    }

    func stopEverything() {
        perform("Stopping Wine…") { try await self.launcher().wine.stopAll() }
    }

    func openLogs() { NSWorkspace.shared.open(Paths.logs) }
}
