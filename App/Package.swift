// swift-tools-version:5.10
import PackageDescription

let package = Package(
    name: "MetalFlight",
    platforms: [.macOS(.v14)],
    dependencies: [
        .package(url: "https://github.com/sparkle-project/Sparkle", exact: "2.10.0")
    ],
    targets: [
        .executableTarget(name: "MetalFlight", dependencies: [
            .product(name: "Sparkle", package: "Sparkle")
        ], path: "Sources/MetalFlight", linkerSettings: [
            .unsafeFlags(["-Xlinker", "-rpath", "-Xlinker", "@executable_path/../Frameworks"])
        ]),
        .testTarget(name: "MetalFlightTests", dependencies: ["MetalFlight"])
    ]
)
