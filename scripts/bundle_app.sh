#!/bin/bash
# Build MetalFlight.app from the Swift package.
# usage: scripts/bundle_app.sh [VERSION] [SIGNING_IDENTITY]
#   SIGNING_IDENTITY defaults to "-" (ad hoc). Use a "Developer ID Application: ..." identity
#   for distribution, then scripts/package_app.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
PROJECT_ROOT="$(pwd -P)"
if [[ -z "${METALFLIGHT_UPDATE_PUBLIC_KEY:-}" && -f config/update-public-key.txt ]]; then
  METALFLIGHT_UPDATE_PUBLIC_KEY="$(cat config/update-public-key.txt)"
fi
VERSION="${1:-0.1.0}"
IDENTITY="${2:--}"
OUT="build/MetalFlight.app"
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Version must have the form 0.1.0" >&2; exit 2
fi

mkdir -p build/icon
xcrun actool "$PROJECT_ROOT/App/Sources/MetalFlight/Resources/MetalFlight.icon" \
  --compile "$PROJECT_ROOT/build/icon" --app-icon MetalFlight \
  --enable-on-demand-resources NO --target-device mac --platform macosx \
  --minimum-deployment-target 15.0 --output-partial-info-plist "$PROJECT_ROOT/build/icon-info.plist" \
  --output-format human-readable-text --errors --warnings
cp build/icon/MetalFlight.icns App/Sources/MetalFlight/Resources/MetalFlight.icns
swift build -c release --package-path App --arch arm64
BIN="$(swift build -c release --package-path App --arch arm64 --show-bin-path)"
rm -rf "$OUT"
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources" "$OUT/Contents/Frameworks"
cp "$BIN/MetalFlight" "$OUT/Contents/MacOS/MetalFlight"
for RESOURCE_BUNDLE in "$BIN"/*.bundle; do
  [[ -d "$RESOURCE_BUNDLE" ]] && ditto "$RESOURCE_BUNDLE" "$OUT/Contents/Resources/$(basename "$RESOURCE_BUNDLE")"
done
cp build/icon/Assets.car build/icon/MetalFlight.icns "$OUT/Contents/Resources/"
SPARKLE="App/.build/artifacts/sparkle/Sparkle"
ditto "$SPARKLE/Sparkle.xcframework/macos-arm64_x86_64/Sparkle.framework" "$OUT/Contents/Frameworks/Sparkle.framework"
cp LICENSE THIRD-PARTY-NOTICES.md "$OUT/Contents/Resources/"
cp "$SPARKLE/LICENSE" "$OUT/Contents/Resources/Sparkle-LICENSE"
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
  <key>LSMinimumSystemVersion</key><string>15.0</string>
  <key>LSArchitecturePriority</key><array><string>arm64</string></array>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>MetalFlight is free software under the GNU GPL v3. Not affiliated with Microsoft, Valve, Apple or CodeWeavers.</string>
</dict>
</plist>
PLIST
/usr/libexec/PlistBuddy -c 'Merge build/icon-info.plist' "$OUT/Contents/Info.plist"
if [[ -n "${METALFLIGHT_UPDATE_PUBLIC_KEY:-}" ]]; then
  /usr/libexec/PlistBuddy -c "Add :SUPublicEDKey string $METALFLIGHT_UPDATE_PUBLIC_KEY" "$OUT/Contents/Info.plist"
  /usr/libexec/PlistBuddy -c 'Add :SUFeedURL string https://github.com/AndreaCodinLife/metalflight/releases/latest/download/appcast.xml' "$OUT/Contents/Info.plist"
  /usr/libexec/PlistBuddy -c 'Add :SUEnableAutomaticChecks bool true' "$OUT/Contents/Info.plist"
fi
SIGN_ARGS=(--force --options runtime --sign "$IDENTITY")
if [[ "$IDENTITY" == "-" ]]; then SIGN_ARGS+=(--timestamp=none); else SIGN_ARGS+=(--timestamp); fi
FRAMEWORK="$OUT/Contents/Frameworks/Sparkle.framework/Versions/B"
for ITEM in "$FRAMEWORK"/XPCServices/*.xpc "$FRAMEWORK/Autoupdate" "$FRAMEWORK/Updater.app" "$OUT/Contents/Frameworks/Sparkle.framework"; do
  codesign "${SIGN_ARGS[@]}" --preserve-metadata=entitlements "$ITEM"
done
if [[ "$IDENTITY" == "-" ]]; then
  # Local ad-hoc builds cannot satisfy hardened runtime library validation.
  cat > build/local.entitlements <<'ENTITLEMENTS'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>com.apple.security.cs.disable-library-validation</key><true/></dict></plist>
ENTITLEMENTS
  codesign "${SIGN_ARGS[@]}" --entitlements build/local.entitlements "$OUT"
else
  codesign "${SIGN_ARGS[@]}" "$OUT"
fi
codesign --verify --deep --strict "$OUT"
echo "Built $OUT ($VERSION, signed with: $IDENTITY)"
