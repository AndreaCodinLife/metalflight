import Sparkle
import SwiftUI

@MainActor
private final class UpdateFeedDelegate: NSObject, SPUUpdaterDelegate {
    func feedURLString(for updater: SPUUpdater) -> String? {
        guard UpdateChannel.selected == .preview else { return nil }
        return UpdateChannel.previewAppcastURL.absoluteString
    }

    func allowedChannels(for updater: SPUUpdater) -> Set<String> {
        UpdateChannel.selected == .preview ? ["beta"] : []
    }
}

/// Sparkle updates the .app; Updater.swift handles the separate Wine runtime.
@MainActor
final class AppUpdates: ObservableObject {
    @Published private(set) var canCheck = false
    let controller: SPUStandardUpdaterController
    private let feedDelegate: UpdateFeedDelegate
    private let configured: Bool

    init() {
        // Local unsigned builds have no feed key. Do not start a broken update cycle.
        let configured = !(Bundle.main.object(forInfoDictionaryKey: "SUPublicEDKey") as? String ?? "").isEmpty
        self.configured = configured
        let delegate = UpdateFeedDelegate()
        feedDelegate = delegate
        controller = SPUStandardUpdaterController(startingUpdater: configured,
                                                  updaterDelegate: delegate, userDriverDelegate: nil)
        if configured {
            controller.updater.publisher(for: \.canCheckForUpdates).assign(to: &$canCheck)
        }
    }

    func check() { controller.checkForUpdates(nil) }

    func channelDidChange() {
        guard configured else { return }
        controller.updater.resetUpdateCycleAfterShortDelay()
    }
}
