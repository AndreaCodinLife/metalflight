import Sparkle
import SwiftUI

/// Sparkle updates the .app; Updater.swift handles the separate Wine runtime.
@MainActor
final class AppUpdates: ObservableObject {
    @Published private(set) var canCheck = false
    let controller: SPUStandardUpdaterController

    init() {
        // Local unsigned builds have no feed key. Do not start a broken update cycle.
        let configured = !(Bundle.main.object(forInfoDictionaryKey: "SUPublicEDKey") as? String ?? "").isEmpty
        controller = SPUStandardUpdaterController(startingUpdater: configured,
                                                  updaterDelegate: nil, userDriverDelegate: nil)
        if configured {
            controller.updater.publisher(for: \.canCheckForUpdates).assign(to: &$canCheck)
        }
    }

    func check() { controller.checkForUpdates(nil) }
}
