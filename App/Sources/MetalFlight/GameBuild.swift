import Foundation

enum GameBuild {
    /// Older runtime profiles shipped the CPU table for this build only.
    static let legacyBuilds = ["23faa2f27fc86bf5bc347498b8afd09968fd12d1f198d325acfd63c5c7360b13": "1.9.18.0"]

    static func verify(_ executable: URL, supported: [String: String]?) throws -> String {
        let hash = try Updater.sha256(of: executable)
        guard let version = (supported ?? legacyBuilds)[hash] else {
            throw MetalFlightError.message("This simulator build is not supported by the installed runtime. Update MetalFlight and its runtime before playing.")
        }
        return version
    }
}
