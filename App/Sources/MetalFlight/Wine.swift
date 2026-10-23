import Foundation

/// The runtime's own Wine, launched without any wrapper (same rules as tools/msfs24_launch.py).
struct Wine {
    let runtime: URL
    var prefix: URL = Paths.prefix

    var loader: URL { runtime.appendingPathComponent("bin/wine") }
    var server: URL { runtime.appendingPathComponent("bin/wineserver") }

    /// Inherited environment minus Wine/CrossOver/diagnostic variables, then the runtime's own.
    func environment(dllOverrides: String? = nil, debug: String = "-all", settings: [String: String] = [:]) -> [String: String] {
        let stripped = ["WINE", "CX_", "DYLD_", "GST_", "MSFS24_", "MULLION_", "ROSETTA_"]
        var env = ProcessInfo.processInfo.environment.filter { key, _ in !stripped.contains { key.hasPrefix($0) } }
        env["WINEPREFIX"] = prefix.path
        env["WINEDEBUG"] = debug
        if let dllOverrides { env["WINEDLLOVERRIDES"] = dllOverrides }
        env["PATH"] = runtime.appendingPathComponent("bin").path + ":" + (env["PATH"] ?? "/usr/bin:/bin:/usr/sbin:/sbin")
        env["GST_PLUGIN_SYSTEM_PATH"] = runtime.appendingPathComponent("lib64/gstreamer-1.0").path
        env["GST_REGISTRY"] = Paths.support.appendingPathComponent("gstreamer-registry.x86_64.bin").path
        env["DOTNET_EnableWriteXorExecute"] = "0"
        if FileManager.default.fileExists(atPath: Paths.d3dshared.path) {
            env["CX_APPLEGPTK_LIBD3DSHARED_PATH"] = Paths.d3dshared.path  // the name the runtime's ntdll reads
        }
        env.merge(settings) { _, new in new }
        if let avx = env["ROSETTA_ADVERTISE_AVX"], avx == "0" || avx.isEmpty { env.removeValue(forKey: "ROSETTA_ADVERTISE_AVX") }
        else { env["ROSETTA_ADVERTISE_AVX"] = "1" }
        return env
    }

    func run(_ program: String, _ arguments: [String] = [], environment: [String: String]? = nil,
             directory: URL? = nil, log: URL? = nil) async throws -> Int32 {
        try await Shell.runAsync(loader, [program] + arguments, environment: environment ?? self.environment(),
                                 directory: directory, log: log)
    }

    func start(_ program: String, _ arguments: [String] = [], environment: [String: String],
               directory: URL? = nil, log: URL? = nil) throws -> Process {
        try Shell.run(loader, [program] + arguments, environment: environment, directory: directory, log: log, wait: false)
    }

    /// Stops every process of this prefix only.
    func stopAll() async {
        _ = try? await Shell.runAsync(server, ["-k"], environment: environment())
        _ = try? await Shell.runAsync(server, ["-w"], environment: environment())
    }

    /// Windows path of a file inside the Mac file system (Z: maps to /).
    static func windowsPath(_ url: URL) -> String { "Z:" + url.path.replacingOccurrences(of: "/", with: "\\") }
}
