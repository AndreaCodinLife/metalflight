import Foundation

struct ControllerID: Equatable {
    let vendor: UInt16
    let product: UInt16

    init(vendor: UInt16, product: UInt16) {
        self.vendor = vendor
        self.product = product
    }

    init?(_ value: String) {
        let parts = value.split(separator: ":", omittingEmptySubsequences: false)
        guard parts.count == 2,
              parts.allSatisfy({ $0.count == 4 && $0.utf8.allSatisfy {
                  (48...57).contains($0) || (65...70).contains($0) || (97...102).contains($0)
              }}),
              let vendor = UInt16(parts[0], radix: 16), let product = UInt16(parts[1], radix: 16) else { return nil }
        self.init(vendor: vendor, product: product)
    }

    var hex: String { String(format: "%04x:%04x", Int(vendor), Int(product)) }
}

struct ControllerCheck: Equatable {
    let directInput: Bool
    let hidPresent: Bool
    let hidOpen: Bool
    let win32Error: UInt32

    init?(output: String) {
        let records = output.split(whereSeparator: \.isNewline)
            .filter { $0.hasPrefix("METALFLIGHT_CONTROLLER ") }
        guard records.count == 1, let record = records.first else { return nil }
        let fields = record.split(separator: " ")
        guard fields.count == 6, fields[1] == "1",
              fields[2...4].allSatisfy({ $0 == "0" || $0 == "1" }),
              let error = UInt32(fields[5]),
              fields[4] == "0" || (fields[3] == "1" && error == 0),
              fields[3] == "1" || error == 0 else { return nil }
        directInput = fields[2] == "1"
        hidPresent = fields[3] == "1"
        hidOpen = fields[4] == "1"
        win32Error = error
    }

    static func run(id: ControllerID, wine: Wine) async throws -> Self {
        guard let program = Bundle.main.url(forResource: "ControllerProbe", withExtension: "exe") else {
            throw MetalFlightError.message("The controller probe is missing from this app build.")
        }

        let files = FileManager.default
        let work = files.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try files.createDirectory(at: work, withIntermediateDirectories: false)
        defer { try? files.removeItem(at: work) }
        let log = work.appendingPathComponent("controller-probe.log")
        let process = try Shell.run(wine.loader, [program.path, id.hex], environment: wine.environment(),
                                    log: log, wait: false)
        defer { if process.isRunning { process.terminate() } }

        let deadline = ContinuousClock.now + .seconds(12)
        while process.isRunning {
            guard ContinuousClock.now < deadline else {
                throw MetalFlightError.message("Wine did not finish the controller check within 12 seconds.")
            }
            try await Task.sleep(for: .milliseconds(100))
        }
        try Task.checkCancellation()
        guard process.terminationStatus == 0,
              let result = Self(output: try String(contentsOf: log, encoding: .utf8)) else {
            throw MetalFlightError.message("Wine could not complete the controller check (exit \(process.terminationStatus)).")
        }
        return result
    }
}
