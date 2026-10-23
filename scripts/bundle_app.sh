#!/bin/bash
# Build MetalFlight.app from the Swift package.
# usage: scripts/bundle_app.sh [VERSION] [SIGNING_IDENTITY]
#   SIGNING_IDENTITY defaults to "-" (ad hoc). Use a "Developer ID Application: ..." identity
#   for distribution, then scripts/notarize.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${1:-0.1.0}"
IDENTITY="${2:--}"
OUT="build/MetalFlight.app"

swift build -c release --package-path App --arch arm64
BIN="$(swift build -c release --package-path App --arch arm64 --show-bin-path)"
rm -rf "$OUT"
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"
cp "$BIN/MetalFlight" "$OUT/Contents/MacOS/MetalFlight"
cat > "$OUT/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>MetalFlight</string>
  <key>CFBundleDisplayName</key><string>MetalFlight</string>
  <key>CFBundleIdentifier</key><string>com.andreacodinlife.metalflight</string>
  <key>CFBundleExecutable</key><string>MetalFlight</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>CFBundleVersion</key><string>${VERSION}</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>LSArchitecturePriority</key><array><string>arm64</string></array>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>MetalFlight is free software under the GNU GPL v3. Not affiliated with Microsoft, Valve, Apple or CodeWeavers.</string>
</dict>
</plist>
PLIST
codesign --force --options runtime --timestamp=none --sign "$IDENTITY" "$OUT"
codesign --verify --strict "$OUT"
echo "Built $OUT ($VERSION, signed with: $IDENTITY)"
