import Foundation

/// Runs external programs and appends their output to a log file.
enum Shell {
    @discardableResult
    static func run(_ executable: URL, _ arguments: [String], environment: [String: String]? = nil,
                    directory: URL? = nil, log: URL? = nil, wait: Bool = true) throws -> Process {
        let process = Process()
        process.executableURL = executable
        process.arguments = arguments
        if let environment { process.environment = environment }
        if let directory { process.currentDirectoryURL = directory }
        process.standardInput = FileHandle.nullDevice
        if let log {
            if !FileManager.default.fileExists(atPath: log.path) { FileManager.default.createFile(atPath: log.path, contents: nil) }
            let handle = try FileHandle(forWritingTo: log)
            handle.seekToEndOfFile()
            process.standardOutput = handle
            process.standardError = handle
        } else {
            process.standardOutput = FileHandle.nullDevice
            process.standardError = FileHandle.nullDevice
        }
        try process.run()
        if wait { process.waitUntilExit() }
        return process
    }

    /// Runs and returns (status, combined output).
    static func capture(_ executable: String, _ arguments: [String], environment: [String: String]? = nil) throws -> (Int32, String) {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        if let environment { process.environment = environment }
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe
        process.standardInput = FileHandle.nullDevice
        try process.run()
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        return (process.terminationStatus, String(decoding: data, as: UTF8.self))
    }

    static func runAsync(_ executable: URL, _ arguments: [String], environment: [String: String]? = nil,
                         directory: URL? = nil, log: URL? = nil) async throws -> Int32 {
        try await Task.detached(priority: .userInitiated) {
            let process = try run(executable, arguments, environment: environment, directory: directory, log: log)
            return process.terminationStatus
        }.value
    }
}
