import SwiftUI

struct SettingsView: View {
    @AppStorage(UpdateChannel.preferenceKey) private var updateChannel = UpdateChannel.stable.rawValue
    let channelChanged: () -> Void

    var body: some View {
        Form {
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
