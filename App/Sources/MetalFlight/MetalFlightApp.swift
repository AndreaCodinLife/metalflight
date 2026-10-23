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

    var body: some Scene {
        WindowGroup("MetalFlight") {
            ContentView()
                .environment(model)
        }
        .windowResizability(.contentSize)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }
}
