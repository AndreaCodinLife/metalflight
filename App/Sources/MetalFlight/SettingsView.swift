import SwiftUI

struct SettingsView: View {
    @AppStorage(UpdateChannel.preferenceKey) private var updateChannel = UpdateChannel.stable.rawValue
    @Environment(AppModel.self) private var model
    let channelChanged: () -> Void

    var body: some View {
        Form {
            Section("Renderer") {
                Picker("Direct3D renderer", selection: Binding(get: { model.renderer }, set: { model.rendererDidChange($0) })) {
                    Text(Renderer.mullion.title).tag(Renderer.mullion)
                    Text(Renderer.d3dmetal.title).tag(Renderer.d3dmetal)
                }
                .pickerStyle(.segmented)
                .disabled(model.playing || !(model.d3dmetalSupported && model.d3dmetalImported) && model.renderer == .mullion)

                Text(!model.d3dmetalSupported
                     ? "The installed runtime offers Mullion only."
                     : !model.d3dmetalImported
                     ? "D3DMetal uses Apple's files from your own Game Porting Toolkit. Import the toolkit's disk image again to enable it."
                     : "D3DMetal is experimental. It uses Apple's D3DMetal from your Game Porting Toolkit with MetalFlight's shim. Mullion remains the default.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Section("Updates") {
                Picker("Update channel", selection: $updateChannel) {
                    ForEach(UpdateChannel.allCases) { channel in
                        Text(channel.title).tag(channel.rawValue)
                    }
                }
                .pickerStyle(.segmented)
                .onChange(of: updateChannel) { channelChanged() }

                Text(updateChannel == UpdateChannel.preview.rawValue
                     ? "Preview updates may contain unfinished changes or known issues. Choose Stable to return to the normal release channel."
                     : "Stable updates are the recommended releases. Preview versions are optional and may be less reliable.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .formStyle(.grouped)
        .padding(20)
        .frame(width: 440)
    }
}
