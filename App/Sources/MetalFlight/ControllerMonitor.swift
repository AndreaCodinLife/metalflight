import Foundation
import IOKit.hid
import Observation

struct ConnectedController: Identifiable, Equatable {
    let id: UInt64
    let name: String
    let usbID: ControllerID
}

struct ControllerAxis: Identifiable, Equatable {
    let id: IOHIDElementCookie
    let label: String
    let value: Int
    let minimum: Int
    let maximum: Int

    var position: Double {
        guard maximum > minimum else { return 0 }
        return min(1, max(0, (Double(value) - Double(minimum)) / (Double(maximum) - Double(minimum))))
    }
}

struct ControllerInput: Equatable {
    var axes: [IOHIDElementCookie: ControllerAxis] = [:]
    var buttons: Set<UInt32> = []
    var hat: Int?
}

@MainActor @Observable
final class ControllerMonitor {
    private(set) var controllers: [ConnectedController] = []
    private(set) var inputs: [UInt64: ControllerInput] = [:]
    private(set) var error: String?
    @ObservationIgnored private var pending: [UInt64: ControllerInput] = [:]

    private static func makeManager() -> IOHIDManager {
        let manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
        IOHIDManagerSetDeviceMatchingMultiple(manager, [kHIDUsage_GD_Joystick, kHIDUsage_GD_GamePad, kHIDUsage_GD_MultiAxisController].map {
            [kIOHIDDeviceUsagePageKey: kHIDPage_GenericDesktop, kIOHIDDeviceUsageKey: $0]
        } as CFArray)
        return manager
    }

    /// A failed device query must not be reported as an empty controller list.
    static func hasConnectedController() -> Bool? {
        let manager = makeManager()
        let status = IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone))
        defer { IOHIDManagerClose(manager, IOOptionBits(kIOHIDOptionsTypeNone)) }
        guard status == kIOReturnSuccess else { return nil }
        let devices = IOHIDManagerCopyDevices(manager) as? Set<IOHIDDevice> ?? []
        return !devices.isEmpty
    }

    func monitor() async {
        let manager = Self.makeManager()
        IOHIDManagerRegisterInputValueCallback(manager, { context, result, _, value in
            guard let context, result == kIOReturnSuccess else { return }
            MainActor.assumeIsolated {
                Unmanaged<ControllerMonitor>.fromOpaque(context).takeUnretainedValue().receive(value)
            }
        }, Unmanaged.passUnretained(self).toOpaque())
        IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetMain(), CFRunLoopMode.commonModes.rawValue)
        defer {
            IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetMain(), CFRunLoopMode.commonModes.rawValue)
            IOHIDManagerClose(manager, IOOptionBits(kIOHIDOptionsTypeNone))
            pending = [:]
        }
        let status = IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone))
        if status == kIOReturnNotPermitted {
            error = "macOS denied access to live controller input."
        } else if status != kIOReturnSuccess {
            error = String(format: "macOS could not open controller monitoring (0x%08x).", status)
        }

        while !Task.isCancelled {
            let connected = (IOHIDManagerCopyDevices(manager) as? Set<IOHIDDevice> ?? [])
                .compactMap(Self.describe)
                .sorted { $0.id < $1.id }
            if connected != controllers { controllers = connected }
            let ids = Set(connected.map(\.id))
            pending = pending.filter { ids.contains($0.key) }
            if pending != inputs { inputs = pending }
            try? await Task.sleep(for: .milliseconds(80))
        }
    }

    private static func registryID(_ device: IOHIDDevice) -> UInt64? {
        var id: UInt64 = 0
        return IORegistryEntryGetRegistryEntryID(IOHIDDeviceGetService(device), &id) == KERN_SUCCESS ? id : nil
    }

    private static func describe(_ device: IOHIDDevice) -> ConnectedController? {
        guard let id = registryID(device),
              let vendor = IOHIDDeviceGetProperty(device, kIOHIDVendorIDKey as CFString) as? NSNumber,
              let product = IOHIDDeviceGetProperty(device, kIOHIDProductIDKey as CFString) as? NSNumber,
              let vid = UInt16(exactly: vendor.intValue), let pid = UInt16(exactly: product.intValue) else { return nil }
        let name = IOHIDDeviceGetProperty(device, kIOHIDProductKey as CFString) as? String ?? "Controller"
        return ConnectedController(id: id, name: name, usbID: ControllerID(vendor: vid, product: pid))
    }

    private func receive(_ value: IOHIDValue) {
        let element = IOHIDValueGetElement(value)
        guard let id = Self.registryID(IOHIDElementGetDevice(element)) else { return }
        let page = IOHIDElementGetUsagePage(element)
        let usage = IOHIDElementGetUsage(element)
        let raw = IOHIDValueGetIntegerValue(value)

        if page == kHIDPage_Button {
            if raw == 0 { pending[id, default: ControllerInput()].buttons.remove(usage) }
            else { pending[id, default: ControllerInput()].buttons.insert(usage) }
            return
        }
        guard page == kHIDPage_GenericDesktop else { return }
        if usage == kHIDUsage_GD_Hatswitch {
            pending[id, default: ControllerInput()].hat = raw
            return
        }
        guard let label = Self.axisLabels[usage] else { return }
        let cookie = IOHIDElementGetCookie(element)
        pending[id, default: ControllerInput()].axes[cookie] = ControllerAxis(
            id: cookie, label: label, value: raw,
            minimum: IOHIDElementGetLogicalMin(element), maximum: IOHIDElementGetLogicalMax(element))
    }

    private static let axisLabels: [UInt32: String] = [
        0x30: "X", 0x31: "Y", 0x32: "Z", 0x33: "Rx", 0x34: "Ry", 0x35: "Rz",
        0x36: "Slider", 0x37: "Dial", 0x38: "Wheel"
    ]
}
