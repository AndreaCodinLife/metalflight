import XCTest
@testable import MetalFlight

final class ControllerCheckTests: XCTestCase {
    func testProbeDistinguishesAStaleHIDLinkFromAWorkingController() throws {
        let stale = try XCTUnwrap(ControllerCheck(output: "METALFLIGHT_CONTROLLER 1 0 1 0 3\r\n"))
        XCTAssertFalse(stale.directInput)
        XCTAssertTrue(stale.hidPresent)
        XCTAssertFalse(stale.hidOpen)
        XCTAssertEqual(stale.win32Error, 3)

        let working = try XCTUnwrap(ControllerCheck(output: "METALFLIGHT_CONTROLLER 1 1 1 1 0\n"))
        XCTAssertTrue(working.directInput)
        XCTAssertTrue(working.hidOpen)
        XCTAssertNil(ControllerCheck(output: "METALFLIGHT_CONTROLLER 1 1 0 1 0\n"))
        XCTAssertNil(ControllerCheck(output: "wine could not start"))
    }

    func testControllerUSBIDPreservesLeadingZeros() {
        XCTAssertEqual(ControllerID("044F:B68D")?.hex, "044f:b68d")
        XCTAssertNil(ControllerID("44f:b68d"))
        XCTAssertNil(ControllerID("+44f:b68d"))
    }
}
