import XCTest
@testable import MetalFlight

final class VisualCppTests: XCTestCase {
    private var directory: URL!

    override func setUpWithError() throws {
        directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    }

    override func tearDownWithError() throws { try FileManager.default.removeItem(at: directory) }

    private func peDLL() -> Data {
        var data = Data(repeating: 0, count: 512)
        data[0] = 0x4d; data[1] = 0x5a; data[0x3c] = 0x80
        data[0x80] = 0x50; data[0x81] = 0x45
        data[0x84] = 0x64; data[0x85] = 0x86
        data[0x97] = 0x20; data[0x98] = 0x0b; data[0x99] = 0x02
        return data
    }

    func testWineBuiltinsDoNotSatisfyNativePrerequisites() throws {
        let file = directory.appendingPathComponent("vcruntime140.dll")
        var data = peDLL()
        try data.write(to: file)
        XCTAssertTrue(VisualCpp.isNativeX64DLL(file))
        data.replaceSubrange(64..<81, with: Data("Wine builtin DLL\0".utf8))
        try data.write(to: file)
        XCTAssertFalse(VisualCpp.isNativeX64DLL(file))
    }

    func testMissingTruncatedMalformedAndWrongArchitectureAreRejected() throws {
        let file = directory.appendingPathComponent("runtime.dll")
        XCTAssertFalse(VisualCpp.isNativeX64DLL(file))
        var variants = [Data(), Data("download failed".utf8), Data(peDLL().prefix(100))]
        var invalidOffset = peDLL()
        invalidOffset.replaceSubrange(0x3c..<0x40, with: [0xff, 0xff, 0xff, 0xff])
        variants.append(invalidOffset)
        var x86 = peDLL(); x86[0x84] = 0x4c; x86[0x85] = 0x01
        variants.append(x86)
        var executable = peDLL(); executable[0x97] = 0
        variants.append(executable)
        for data in variants {
            try data.write(to: file)
            XCTAssertFalse(VisualCpp.isNativeX64DLL(file))
        }
    }

    func testPartialInstallationIsNotReady() throws {
        let system32 = directory.appendingPathComponent("drive_c/windows/system32")
        try FileManager.default.createDirectory(at: system32, withIntermediateDirectories: true)
        for name in VisualCpp.requiredDLLs {
            XCTAssertFalse(VisualCpp.installed(in: directory))
            try peDLL().write(to: system32.appendingPathComponent(name))
        }
        XCTAssertTrue(VisualCpp.installed(in: directory))
        try FileManager.default.removeItem(at: system32.appendingPathComponent("msvcp140_2.dll"))
        XCTAssertFalse(VisualCpp.installed(in: directory))
    }
}
