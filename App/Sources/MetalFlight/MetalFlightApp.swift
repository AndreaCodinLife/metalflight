import SwiftUI

@main
enum Entry {
    static func main() {
        if CommandLine.arguments.count > 2, CommandLine.arguments[1] == "--headless" {
            exit(Headless.run(Array(CommandLine.arguments.dropFirst(2))))
        }
        // Swift package runs in Xcode have no application bundle icon metadata.
        // Packaged .apps use their compiled Icon Composer asset automatically.
        if Bundle.main.bundleURL.pathExtension != "app",
           let url = Bundle.module.url(forResource: "MetalFlight", withExtension: "icns"),
           let icon = NSImage(contentsOf: url) {
            NSApplication.shared.applicationIconImage = icon
        }
        MetalFlightApp.main()
    }
}

struct MetalFlightApp: App {
    @State private var model = AppModel()
    @StateObject private var updates = AppUpdates()

    var body: some Scene {
        WindowGroup("MetalFlight") {
            ContentView()
                .environment(model)
        }
        .windowResizability(.contentSize)
        .commands {
            CommandGroup(replacing: .newItem) {}
            CommandGroup(after: .appInfo) {
                Button("Check for Application Updates…") { updates.check() }
                    .disabled(!updates.canCheck || model.playing || model.busy != nil)
            }
        }
    }
}
