#!/bin/bash
# Build a drag-to-Applications DMG; optionally notarize using a Keychain profile.
# Usage: scripts/package_app.sh VERSION [SIGNING_IDENTITY] [NOTARY_PROFILE]
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${1:?usage: package_app.sh VERSION [SIGNING_IDENTITY] [NOTARY_PROFILE]}"
IDENTITY="${2:--}"
NOTARY_PROFILE="${3:-}"
if [[ -n "$NOTARY_PROFILE" ]]; then
  [[ "$IDENTITY" == "Developer ID Application:"* ]] || { echo 'Notarization requires Developer ID Application.' >&2; exit 2; }
  [[ -n "${METALFLIGHT_UPDATE_PUBLIC_KEY:-}" ]] || { echo 'Set METALFLIGHT_UPDATE_PUBLIC_KEY before distributing.' >&2; exit 2; }
fi
scripts/bundle_app.sh "$VERSION" "$IDENTITY"
mkdir -p dist
if [[ -n "$NOTARY_PROFILE" ]]; then
  ditto -c -k --keepParent build/MetalFlight.app build/notarization.zip
  xcrun notarytool submit build/notarization.zip --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple build/MetalFlight.app
fi
STAGING=$(mktemp -d "${TMPDIR:-/tmp}/metalflight-dmg.XXXXXX")
trap 'rm -rf "$STAGING"' EXIT
ditto build/MetalFlight.app "$STAGING/MetalFlight.app"
ln -s /Applications "$STAGING/Applications"
DMG="dist/MetalFlight-${VERSION}.dmg"
hdiutil create -ov -volname MetalFlight -srcfolder "$STAGING" -format UDZO "$DMG"
if [[ "$IDENTITY" != "-" ]]; then codesign --force --timestamp --sign "$IDENTITY" "$DMG"; fi
if [[ -n "$NOTARY_PROFILE" ]]; then
  xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$DMG"
  xcrun stapler validate "$DMG"
fi
shasum -a 256 "$DMG"
