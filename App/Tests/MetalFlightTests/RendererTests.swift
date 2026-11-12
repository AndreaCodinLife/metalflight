import XCTest
@testable import MetalFlight

/// Owned fixtures only: no Apple file is used. The "toolkit" is a folder of authored bytes with the same shape.
final class RendererTests: XCTestCase {
    private var root: URL!

    override func setUpWithError() throws {
        root = FileManager.default.temporaryDirectory.appendingPathComponent("metalflight-renderer-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    }

    override func tearDownWithError() throws { try? FileManager.default.removeItem(at: root) }

    private func profileJSON(withRenderer: Bool) -> Data {
        let renderer = """
        ,"renderers":{"d3dmetal":{"label":"D3DMetal (experimental)","description":"d","dllOverrides":"d3d12=n;d3dm12=b",
         "gameSettings":{"MSFS24_WINE_STACK_CACHE":"1","SHARED":"overlay"},"steamDllOverridesAppend":"nvapi64=",
         "system32Replace":{"d3d12.dll":"share/metalflight/d3dmetal/d3d12.dll"},
         "gptkSystem32":{"d3dm12.dll":"wine/x86_64-windows/d3d12.dll"},
         "gptkAliases":{"wine/x86_64-windows/d3dm12.dll":"wine/x86_64-windows/d3d12.dll","wine/x86_64-unix/d3dm12.so":"../../external/lib.dylib"},
         "gptk":{"name":"Owned toolkit","redistFiles":{},"redistLinks":{},"signed":[]}}}
        """
        return Data("""
        {"steamAppID":"1","dllOverrides":"d3d12=n","gameDebug":"-all","gameArguments":[],"gameSettings":{"SHARED":"base","KEEP":"1"},
         "steamArguments":[],"steamDebug":"-all","steamSettings":{"WINEDLLOVERRIDES":"d3d11=n"},
         "system32":["share/metalflight/system32/d3d12.dll","share/metalflight/system32/dxgi.dll"],
         "helper":"h","readinessProbe":"p"\(withRenderer ? renderer : "")}
        """.utf8)
    }

    func testOlderProfilesDecodeWithoutRenderers() throws {
        let profile = try JSONDecoder().decode(Profile.self, from: profileJSON(withRenderer: false))
        XCTAssertNil(profile.renderers)
        let runtime = root.appendingPathComponent("runtime")
        let mullion = try LaunchConfiguration.make(profile: profile, renderer: .mullion, runtime: runtime, redist: root)
        XCTAssertEqual(mullion.dllOverrides, "d3d12=n")
        XCTAssertEqual(mullion.gameSettings, ["SHARED": "base", "KEEP": "1"])
        XCTAssertEqual(mullion.system32.map(\.source), [runtime.appendingPathComponent("share/metalflight/system32/d3d12.dll"),
                                                         runtime.appendingPathComponent("share/metalflight/system32/dxgi.dll")])
        XCTAssertThrowsError(try LaunchConfiguration.make(profile: profile, renderer: .d3dmetal, runtime: runtime, redist: root))
    }

    func testD3DMetalOverlayAppliesOnlyWhenSelected() throws {
        let profile = try JSONDecoder().decode(Profile.self, from: profileJSON(withRenderer: true))
        let runtime = root.appendingPathComponent("runtime"), redist = root.appendingPathComponent("redist")
        let mullion = try LaunchConfiguration.make(profile: profile, renderer: .mullion, runtime: runtime, redist: redist)
        XCTAssertEqual(mullion.dllOverrides, "d3d12=n")
        XCTAssertNil(mullion.gameSettings["WINEDLLPATH"])
        XCTAssertEqual(mullion.steamSettings["WINEDLLOVERRIDES"], "d3d11=n")

        let d3dmetal = try LaunchConfiguration.make(profile: profile, renderer: .d3dmetal, runtime: runtime, redist: redist)
        XCTAssertEqual(d3dmetal.dllOverrides, "d3d12=n;d3dm12=b")
        XCTAssertEqual(d3dmetal.gameSettings["SHARED"], "overlay")
        XCTAssertEqual(d3dmetal.gameSettings["KEEP"], "1")
        XCTAssertEqual(d3dmetal.gameSettings["MSFS24_WINE_STACK_CACHE"], "1")
        XCTAssertEqual(d3dmetal.gameSettings["WINEDLLPATH"], redist.appendingPathComponent("wine").path)
        XCTAssertEqual(d3dmetal.steamSettings["WINEDLLOVERRIDES"], "d3d11=n;nvapi64=")
        XCTAssertEqual(d3dmetal.system32.map(\.name), ["d3d12.dll", "dxgi.dll", "d3dm12.dll"])
        XCTAssertEqual(d3dmetal.system32[0].source, runtime.appendingPathComponent("share/metalflight/d3dmetal/d3d12.dll"))
        XCTAssertEqual(d3dmetal.system32[2].source, redist.appendingPathComponent("wine/x86_64-windows/d3d12.dll"))
    }

    /// A toolkit-shaped folder: two PE files, one external library, a framework file and two .so links.
    private func makeToolkit() throws -> (lib: URL, pins: GPTKPins) {
        let fm = FileManager.default
        let lib = root.appendingPathComponent("toolkit/redist/lib")
        for dir in ["wine/x86_64-windows", "wine/x86_64-unix", "external/Kit.framework/Versions/A"] {
            try fm.createDirectory(at: lib.appendingPathComponent(dir), withIntermediateDirectories: true)
        }
        let content = ["wine/x86_64-windows/d3d12.dll": "owned pe 12", "wine/x86_64-windows/dxgi.dll": "owned pe dxgi",
                       "external/lib.dylib": "owned dylib", "external/Kit.framework/Versions/A/Kit": "owned framework"]
        for (relative, text) in content { try Data(text.utf8).write(to: lib.appendingPathComponent(relative)) }
        // A framework's own links (as in D3DMetal.framework): a link inside a directory that has more entries after it.
        let links = ["wine/x86_64-unix/d3d12.so": "../../external/lib.dylib", "wine/x86_64-unix/dxgi.so": "../../external/lib.dylib",
                     "external/Kit.framework/Kit": "Versions/Current/Kit", "external/Kit.framework/Versions/Current": "A"]
        for (relative, target) in links { try fm.createSymbolicLink(atPath: lib.appendingPathComponent(relative).path, withDestinationPath: target) }
        var hashes: [String: String] = [:]
        for relative in content.keys { hashes[relative] = try Updater.sha256(of: lib.appendingPathComponent(relative)) }
        return (lib, GPTKPins(name: "Owned toolkit", redistFiles: hashes, redistLinks: links,
                              signed: ["external/lib.dylib", "external/Kit.framework"]))
    }

    private let aliases = ["wine/x86_64-windows/d3dm12.dll": "wine/x86_64-windows/d3d12.dll",
                           "wine/x86_64-unix/d3dm12.so": "../../external/lib.dylib"]

    func testRedistImportVerifiesAndAddsAliases() throws {
        let (lib, pins) = try makeToolkit()
        let destination = root.appendingPathComponent("support/gptk/redist")
        try GPTK.importRedist(from: lib, pins: pins, aliases: aliases, into: destination, signatureValid: { _ in true })
        let alias = destination.appendingPathComponent("wine/x86_64-windows/d3dm12.dll")
        XCTAssertEqual(try Data(contentsOf: alias), Data("owned pe 12".utf8))
        XCTAssertEqual(try FileManager.default.destinationOfSymbolicLink(atPath: destination.appendingPathComponent("wine/x86_64-unix/d3dm12.so").path),
                       "../../external/lib.dylib")
        let renderer = RendererProfile(label: "l", description: "d", dllOverrides: "", gameSettings: [:], gptkAliases: aliases, gptk: pins)
        XCTAssertTrue(GPTK.redistInstalled(renderer, at: destination, signatureValid: { _ in true }))
        XCTAssertTrue(GPTK.redistPresent(renderer, at: destination))
        XCTAssertFalse(GPTK.redistInstalled(renderer, at: destination, signatureValid: { _ in false }))
        // A changed file after import is detected at Play.
        try Data("tampered".utf8).write(to: destination.appendingPathComponent("wine/x86_64-windows/dxgi.dll"))
        XCTAssertFalse(GPTK.redistInstalled(renderer, at: destination, signatureValid: { _ in true }))
    }

    /// The real codesign requirement (not injected): an Apple-signed system tool passes, an owned unsigned file does not.
    func testAppleSignatureRequirement() throws {
        XCTAssertTrue(GPTK.verifyApple(URL(fileURLWithPath: "/bin/ls")))
        let unsigned = root.appendingPathComponent("unsigned.dylib")
        try Data("owned unsigned bytes".utf8).write(to: unsigned)
        XCTAssertFalse(GPTK.verifyApple(unsigned))
    }

    func testRedistImportRefusesAnyOtherToolkit() throws {
        let fm = FileManager.default
        let destination = root.appendingPathComponent("support/gptk/redist")
        func refuses(_ change: (URL) throws -> Void, signed: Bool = true) throws {
            let (lib, pins) = try makeToolkit()
            try change(lib)
            XCTAssertThrowsError(try GPTK.importRedist(from: lib, pins: pins, aliases: aliases, into: destination,
                                                       signatureValid: { _ in signed }))
            XCTAssertFalse(fm.fileExists(atPath: destination.path), "nothing is installed after a refusal")
            try fm.removeItem(at: root.appendingPathComponent("toolkit"))
        }
        try refuses { try Data("other build".utf8).write(to: $0.appendingPathComponent("wine/x86_64-windows/d3d12.dll")) }
        try refuses { try fm.removeItem(at: $0.appendingPathComponent("wine/x86_64-windows/dxgi.dll")) }
        try refuses { try Data("extra".utf8).write(to: $0.appendingPathComponent("wine/x86_64-windows/extra.dll")) }
        try refuses { lib in
            let link = lib.appendingPathComponent("wine/x86_64-unix/dxgi.so")
            try fm.removeItem(at: link); try fm.createSymbolicLink(atPath: link.path, withDestinationPath: "elsewhere.dylib")
        }
        try refuses({ _ in }, signed: false)
    }
}
