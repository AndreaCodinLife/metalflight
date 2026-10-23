import SwiftUI

struct StepRow<Actions: View>: View {
    let number: Int
    let title: String
    let detail: String
    let done: Bool
    @ViewBuilder let actions: Actions

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: done ? "checkmark.circle.fill" : "\(number).circle")
                .font(.title2)
                .foregroundStyle(done ? Color.green : Color.secondary)
            VStack(alignment: .leading, spacing: 4) {
                Text(title).font(.headline)
                Text(detail).font(.callout).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                HStack { actions }.padding(.top, 2)
            }
            Spacer()
        }
        .padding(.vertical, 6)
    }
}

struct ContentView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            List {
                let s = model.system
                StepRow(number: 1, title: "This Mac",
                        detail: "Apple Silicon: \(s.appleSilicon ? "yes" : "no") · macOS \(s.macOSVersion) · Rosetta: \(s.rosetta ? "installed" : "missing") · \(s.freeGigabytes) GB free",
                        done: s.ok) {
                    if !s.rosetta { Button("Install Rosetta…") { NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:")!) } }
                }
                StepRow(number: 2, title: "Runtime",
                        detail: "Installed: \(model.installedRuntime ?? "none")" + (model.availableRuntime.map { " · Latest: \($0)" } ?? "")
                            + (Updater.manifestURL.isFileURL ? " · Local preview" : ""),
                        done: model.installedRuntime != nil && (model.availableRuntime == nil || model.availableRuntime == model.installedRuntime)) {
                    Button(model.installedRuntime == nil ? "Install" : "Update") { model.installRuntime() }.disabled(model.playing)
                    Button("Check") { model.checkForUpdates() }
                    if model.installedRuntime != nil { Button("Roll back") { model.rollbackRuntime() }.disabled(model.playing) }
                }
                StepRow(number: 3, title: "Apple Game Porting Toolkit",
                        detail: "Download the Game Porting Toolkit from developer.apple.com (free Apple Account, Apple's licence) and open its disk image. MetalFlight copies only libd3dshared.dylib, after checking Apple's signature.",
                        done: model.gptkInstalled) {
                    Button("Open Apple's download page") { NSWorkspace.shared.open(URL(string: "https://developer.apple.com/download/all/?q=game%20porting%20toolkit")!) }
                    Button("Import from mounted disk") { model.importGPTK() }
                    Button("Choose file…") { model.chooseGPTK() }
                }
                StepRow(number: 4, title: "Windows environment", detail: "A private Wine prefix inside MetalFlight's folder.", done: model.prefixReady) {
                    Button(model.prefixReady ? "Repair" : "Create") { model.preparePrefix() }.disabled(model.installedRuntime == nil || model.playing)
                }
                StepRow(number: 5, title: "Steam", detail: "Installs Valve's Steam client. Sign in, then install Microsoft Flight Simulator 2024 (your own copy).",
                        done: model.steamInstalled && model.gameInstalled) {
                    if !model.steamInstalled { Button("Install Steam") { model.installSteam() }.disabled(!model.prefixReady) }
                    else { Button("Open Steam") { model.openSteam() } }
                }
                StepRow(number: 6, title: "Play", detail: model.playing ? "Microsoft Flight Simulator 2024 is running." : "Starts Steam if needed, then the simulator with MetalFlight's patches.",
                        done: false) {
                    Button("Play") { model.play() }
                        .buttonStyle(.borderedProminent)
                        .disabled(!(model.gptkInstalled && model.gameInstalled) || model.playing)
                    Button("Stop everything") { model.stopEverything() }
                    Button("Logs") { model.openLogs() }
                }
            }
            Divider()
            VStack(alignment: .leading, spacing: 4) {
                if let busy = model.busy { HStack { ProgressView().controlSize(.small); Text(busy) } }
                if let error = model.lastError { Text(error).foregroundStyle(.red) }
                ScrollView {
                    Text(model.lines.joined(separator: "\n"))
                        .font(.system(.caption, design: .monospaced))
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .textSelection(.enabled)
                }
                .frame(height: 110)
            }
            .padding(10)
        }
        .frame(minWidth: 640, minHeight: 620)
        .onAppear { model.refresh() }
        .onReceive(Timer.publish(every: 5, on: .main, in: .common).autoconnect()) { _ in
            model.refreshInstallation()
        }
    }
}
