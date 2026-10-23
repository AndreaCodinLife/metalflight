import SwiftUI

@main
enum Entry {
    static func main() {
        if CommandLine.arguments.count > 2, CommandLine.arguments[1] == "--headless" {
            exit(Headless.run(Array(CommandLine.arguments.dropFirst(2))))
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
