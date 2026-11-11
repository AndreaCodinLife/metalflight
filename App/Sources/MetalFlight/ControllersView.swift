import SwiftUI

struct ControllersView: View {
    let prefixReady: Bool

    @Environment(\.dismiss) private var dismiss
    @State private var monitor = ControllerMonitor()
    @State private var selectedID: UInt64?
    @State private var request: ConnectedController?
    @State private var checkedID: UInt64?
    @State private var wineResult: ControllerCheck?
    @State private var checkError: String?

    private var selected: ConnectedController? {
        monitor.controllers.first(where: { $0.id == selectedID }) ?? monitor.controllers.first
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                Text("Controllers").font(.title2.bold())
                Spacer()
                Button("Done") { dismiss() }.keyboardShortcut(.defaultAction)
            }

            if let error = monitor.error {
                Label(error, systemImage: "exclamationmark.triangle")
            }
            if let controller = selected {
                if monitor.controllers.count > 1 {
                    Picker("Device", selection: Binding(get: { controller.id }, set: { selectedID = $0 })) {
                        ForEach(monitor.controllers) { Text($0.name).tag($0.id) }
                    }
                }
                Label("\(controller.name) · \(controller.usbID.hex)", systemImage: "gamecontroller")
                    .font(.headline)
                Text("Connected to macOS").foregroundStyle(.secondary)
                if monitor.error == nil {
                    GroupBox("Live input") {
                        ControllerInputView(input: monitor.inputs[controller.id] ?? ControllerInput(),
                                            hotasOne: controller.usbID == ControllerID(vendor: 0x044f, product: 0xb68d))
                    }
                }
                GroupBox("Wine visibility") { wineStatus(for: controller) }
            } else if monitor.error == nil {
                Label("No controller detected by macOS", systemImage: "gamecontroller")
                Text("Connect your controller. For a T.Flight HOTAS One, set the selector to PC.")
                    .foregroundStyle(.secondary)
            }
        }
        .padding(20)
        .frame(width: 490)
        .task { await monitor.monitor() }
        .task(id: request) {
            if let request { await checkWine(for: request) }
        }
    }

    private func wineStatus(for controller: ConnectedController) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            if request?.id == controller.id {
                ProgressView("Checking this USB ID in Wine…").controlSize(.small)
            } else if checkedID == controller.id, let result = wineResult {
                if result.directInput {
                    Label("DirectInput sees this controller model", systemImage: "checkmark.circle")
                } else if result.hidPresent && !result.hidOpen {
                    Label("Wine lists the device but can’t open it (Windows error \(result.win32Error)).",
                          systemImage: "exclamationmark.triangle")
                } else if result.hidOpen {
                    Label("Wine can open the device, but DirectInput doesn’t list it.",
                          systemImage: "exclamationmark.triangle")
                } else {
                    Label("Wine doesn’t list this controller.", systemImage: "exclamationmark.triangle")
                }
            } else if checkedID == controller.id, let checkError {
                Label(checkError, systemImage: "exclamationmark.triangle")
            } else {
                Text(prefixReady ? "Check whether Wine can open this device and expose it to DirectInput."
                     : "Create the Windows environment to check Wine visibility.")
                    .foregroundStyle(.secondary)
            }
            Button("Check Wine") { request = controller }
                .disabled(!prefixReady || request != nil)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private func checkWine(for controller: ConnectedController) async {
        defer { request = nil }
        checkedID = controller.id
        wineResult = nil
        checkError = nil
        guard let runtime = Paths.resolvedRuntime() else {
            checkError = "Install the runtime to check Wine visibility."
            return
        }
        do {
            wineResult = try await ControllerCheck.run(id: controller.usbID, wine: Wine(runtime: runtime))
        } catch is CancellationError {
            return
        } catch {
            checkError = error.localizedDescription
        }
    }
}

private struct ControllerInputView: View {
    let input: ControllerInput
    let hotasOne: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            if input.axes.isEmpty {
                Text("Move an axis to see its value.").foregroundStyle(.secondary)
            } else {
                Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 6) {
                    ForEach(input.axes.values.sorted { $0.id < $1.id }) { axis in
                        GridRow {
                            Text(axisLabel(axis.label)).frame(minWidth: 105, alignment: .leading)
                            ProgressView(value: axis.position)
                                .accessibilityLabel(axisLabel(axis.label))
                                .accessibilityValue("\(axis.value)")
                            Text("\(axis.value)").monospacedDigit()
                                .frame(minWidth: 48, alignment: .trailing)
                                .accessibilityHidden(true)
                        }
                    }
                }
            }
            if let hat = input.hat { Text("Hat switch  \(hat)").monospacedDigit() }
            Text(input.buttons.isEmpty ? "Buttons pressed  None" :
                    "Buttons pressed  " + input.buttons.sorted().map(String.init).joined(separator: ", "))
                .monospacedDigit()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private func axisLabel(_ axis: String) -> String {
        guard hotasOne else { return axis }
        return [
            "X": "Stick X", "Y": "Stick Y", "Z": "Throttle", "Rx": "Right brake",
            "Ry": "Left brake", "Rz": "Rudder / twist", "Slider": "Rudder rocker", "Dial": "Trim"
        ][axis] ?? axis
    }
}

#Preview {
    ControllersView(prefixReady: false)
}
