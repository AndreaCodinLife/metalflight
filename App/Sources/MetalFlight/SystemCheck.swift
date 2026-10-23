import Foundation

struct SystemCheck {
    var appleSilicon = false
    var macOSVersion = ""
    var macOSSupported = false
    var rosetta = false
    var freeGigabytes: Int64 = 0

    var ok: Bool { appleSilicon && macOSSupported && rosetta && freeGigabytes >= 20 }

    static func run() -> SystemCheck {
        var check = SystemCheck()
        var arm: Int32 = 0
        var size = MemoryLayout<Int32>.size
        check.appleSilicon = sysctlbyname("hw.optional.arm64", &arm, &size, nil, 0) == 0 && arm == 1
        let version = ProcessInfo.processInfo.operatingSystemVersion
        check.macOSVersion = "\(version.majorVersion).\(version.minorVersion)"
        check.macOSSupported = version.majorVersion >= 15
        // Rosetta is present when an x86_64 slice of a system binary runs.
        check.rosetta = (try? Shell.capture("/usr/bin/arch", ["-x86_64", "/usr/bin/true"]).0) == 0
        if let values = try? Paths.support.deletingLastPathComponent()
            .resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey]),
           let bytes = values.volumeAvailableCapacityForImportantUsage {
            check.freeGigabytes = bytes / 1_000_000_000
        }
        return check
    }
}
