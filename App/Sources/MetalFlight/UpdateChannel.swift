import Foundation

enum UpdateChannel: String, CaseIterable, Identifiable {
    case stable
    case preview

    static let preferenceKey = "updateChannel"
    static let previewRuntimeReleasesURL = URL(string:
        "https://api.github.com/repos/AndreaCodinLife/metalflight/releases?per_page=100")!
    static let previewAppcastURL = URL(string:
        "https://raw.githubusercontent.com/AndreaCodinLife/metalflight/preview-channel/appcast.xml")!

    var id: String { rawValue }
    var title: String { self == .stable ? "Stable" : "Preview" }

    static var selected: UpdateChannel {
        get {
            guard let value = UserDefaults.standard.string(forKey: preferenceKey),
                  let channel = UpdateChannel(rawValue: value) else { return .stable }
            return channel
        }
        set { UserDefaults.standard.set(newValue.rawValue, forKey: preferenceKey) }
    }
}
