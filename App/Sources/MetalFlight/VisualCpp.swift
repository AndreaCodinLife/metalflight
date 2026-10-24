import Foundation

/// The game's native-only C++ overrides require Microsoft's redistributable.
/// Download it from Microsoft into the user's prefix; it is never bundled.
enum VisualCpp {
    static let downloadURL = URL(string: "https://aka.ms/vc14/vc_redist.x64.exe")!
    static let requiredDLLs = ["concrt140.dll", "msvcp140.dll", "msvcp140_1.dll",
                               "msvcp140_2.dll", "vcruntime140.dll", "vcruntime140_1.dll"]

    static func installed(in prefix: URL) -> Bool {
        let system32 = prefix.appendingPathComponent("drive_c/windows/system32")
        return requiredDLLs.allSatisfy { isNativeX64DLL(system32.appendingPathComponent($0)) }
    }

    /// File existence is insufficient: fresh prefixes contain Wine's builtin DLLs,
    /// which are rejected by the game's native-only overrides.
    static func isNativeX64DLL(_ file: URL) -> Bool {
        guard let data = try? Data(contentsOf: file, options: .mappedIfSafe), data.count >= 0x40,
              data[0] == 0x4d, data[1] == 0x5a else { return false }
        let marker = Data("Wine builtin DLL\0".utf8)
        if data.count >= 64 + marker.count, data[64..<(64 + marker.count)] == marker { return false }
        let offset = (0..<4).reduce(0) { $0 | (Int(data[0x3c + $1]) << (8 * $1)) }
        guard offset >= 0x40, offset <= data.count - 26 else { return false }
        return data[offset..<(offset + 4)] == Data([0x50, 0x45, 0, 0])
            && data[offset + 4] == 0x64 && data[offset + 5] == 0x86
            && data[offset + 24] == 0x0b && data[offset + 25] == 0x02
            && data[offset + 23] & 0x20 != 0 // IMAGE_FILE_DLL
    }

    static func ensure(wine: Wine, status: @escaping (String) -> Void) async throws {
        guard !installed(in: wine.prefix) else { return }
        status("Installing Microsoft Visual C++ prerequisites…")
        try Paths.ensure()
        let (temporary, response) = try await URLSession.shared.download(from: downloadURL)
        defer { try? FileManager.default.removeItem(at: temporary) }
        guard let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode) else {
            throw MetalFlightError.message("Downloading Microsoft Visual C++ failed (HTTP \((response as? HTTPURLResponse)?.statusCode ?? 0)).")
        }
        let header = try FileHandle(forReadingFrom: temporary)
        defer { try? header.close() }
        guard try header.read(upToCount: 2) == Data([0x4d, 0x5a]) else {
            throw MetalFlightError.message("Microsoft's Visual C++ download is not a Windows installer.")
        }
        let installer = Paths.downloads.appendingPathComponent("vc_redist.x64.exe")
        try? FileManager.default.removeItem(at: installer)
        try FileManager.default.copyItem(at: temporary, to: installer)
        // The installer needs Wine's default DLL selection while setting up the
        // native files. Never run it with the game's native-only C++ overrides.
        let code = try await wine.run(installer.path, ["/install", "/quiet", "/norestart"],
                                     environment: wine.environment(dllOverrides: "mscoree,mshtml="),
                                     log: Paths.logs.appendingPathComponent("visual-cpp-install.log"))
        guard installed(in: wine.prefix) else {
            throw MetalFlightError.message("Microsoft Visual C++ setup did not install the required x64 libraries (exit \(code)). See visual-cpp-install.log, then try Play again.")
        }
        status("Microsoft Visual C++ prerequisites are ready.")
    }
}
