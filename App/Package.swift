// swift-tools-version:5.10
import PackageDescription

let package = Package(
    name: "MetalFlight",
    platforms: [.macOS(.v14)],
    targets: [
        .executableTarget(name: "MetalFlight", path: "Sources/MetalFlight")
    ]
)
