# Building MetalFlight

Everything builds on an Apple Silicon Mac (macOS 15+) with Xcode, Rosetta and Homebrew. The runtime
is x86_64 and runs under Rosetta. The app is arm64.

Homebrew packages: `mingw-w64` (GCC 16.2 cross compilers, used for every PE module),
`meson`, `ninja`, `cmake`, `pkg-config`, `autoconf`, `bison`.

The CPU helper needs llvm-mingw 20260908 (UCRT, clang 23.1.1) to reproduce the qualified binary.

## 1. Sources

```sh
git clone https://github.com/AndreaCodinLife/metalflight-wine runtime/work/metalflight-wine
git -C runtime/work/metalflight-wine checkout runtime-2026.10.23-dev3
python3 runtime/fetch_sources.py        # pinned archives, SHA-256 verified
```

## 2. Dependencies (x86_64, from source)

```sh
python3 runtime/build_deps.py           # → runtime/work/deps/prefix, @rpath dylibs + receipt.json
```

## 3. Wine

```sh
python3 runtime/build_wine.py --label <version>   # → runtime/work/runtime/<version>
```

The build maps source paths to `../sources/wine-platform` (`-ffile-prefix-map`). The modules'
layout then does not depend on the build machine, and the CPU helper's site tables, which pin
module RVAs and byte preimages, stay valid.

## 4. Helper, Mullion, probe

```sh
helper/build.sh /path/to/llvm-mingw-20260908-ucrt-macos-universal out/
x86_64-w64-mingw32-gcc -O2 probes/steam_session.c -o out/steam_session.exe
```

Mullion builds from metalflight-mullion. The Steam readiness probe builds from `probes/steam_session.c`.
Clone Mullion with `--recurse-submodules` and check out `runtime-2026.10.23-dev3` to reproduce
this runtime's source revision. Its `METALFLIGHT.md` documents the renderer build.

## 5. Package and publish

Stage the inputs as described in `scripts/package_runtime.py`, then:

```sh
python3 scripts/package_runtime.py --wine runtime/work/runtime/<version> --inputs <inputs> \
    --profile runtime/profile.json --version <version> --output dist \
    --base-url https://github.com/AndreaCodinLife/metalflight/releases/download/<tag>/ --strip-debug
scripts/bundle_app.sh <app-version> "Developer ID Application: …"
```

Upload `metalflight-runtime-<version>.tar.xz` and `metalflight-manifest.json` to the release.
The app reads `releases/latest/download/metalflight-manifest.json`.

## 6. Application installer and application updates

The editable app icon is `App/Sources/MetalFlight/Resources/MetalFlight.icon` (Apple Icon Composer).
Packaging requires Xcode 26 or later: `actool` builds its layered asset and macOS 15 `.icns`
fallback before signing. The fallback is also a Swift package resource, so Xcode runs display
the icon in the Dock. Edit the project's copy; the original download is retained separately.

The public update key is in `config/update-public-key.txt`; the packaging script reads it unless
`METALFLIGHT_UPDATE_PUBLIC_KEY` is explicitly set. The private release key is stored only in the
maintainer's login Keychain under the Sparkle account `metalflight-release-updates`.

The Swift package pins Sparkle 2.10.0 in `App/Package.resolved`. Runtime updates and application
updates have independent release assets. Sparkle follows its upstream
[SwiftUI integration](https://sparkle-project.org/documentation/programmatic-setup/) and
[signing/distribution procedure](https://sparkle-project.org/documentation/).

For an ad-hoc signed, non-notarized preview DMG:

```sh
scripts/package_app.sh 0.1.0
```

Copy `MetalFlight.app` from the DMG to Applications. This DMG is not notarized.
The v0.1.0 public preview uses this signing mode; it does not claim Apple approval.
Tests can override `METALFLIGHT_SUPPORT_DIR` and `METALFLIGHT_MANIFEST_URL` with a temporary
directory and a local HTTP server; use a new prefix, never the research default.
For an offline local preview, `METALFLIGHT_MANIFEST_URL` also accepts a `file://` manifest whose
runtime URL points to a local archive. The archive still needs the published size/hash and a
valid inventory. When `METALFLIGHT_SUPPORT_DIR` is explicitly set, the app also recognizes
`preview-manifest.json` in that directory. This is a configured source, not an HTTP-error
fallback. An already active, verified version is left running when Update is pressed again.

The first public preview already has a signing key. Future maintainer releases must reuse it.
For a separate fork, generate a separate Sparkle signing key once:

```sh
swift package --package-path App resolve
App/.build/artifacts/sparkle/Sparkle/bin/generate_keys --account '<your-fork-account>'
```

The private key stays in the login Keychain. Save the public key in the release configuration as
`METALFLIGHT_UPDATE_PUBLIC_KEY`; retain a private backup of the signing key using Sparkle's tools.
Do not commit a private key. Install a **Developer ID Application** certificate and configure a
`notarytool` Keychain profile through your Apple Developer account, then build:

```sh
export METALFLIGHT_UPDATE_PUBLIC_KEY='<public key printed by generate_keys>'
scripts/package_app.sh 0.1.0 'Developer ID Application: …' '<notary-profile>'
App/.build/artifacts/sparkle/Sparkle/bin/generate_appcast \
  --account metalflight-release-updates \
  --download-url-prefix 'https://github.com/AndreaCodinLife/metalflight/releases/download/v0.1.0/' dist
```

The packaging script signs nested Sparkle services, notarizes and staples the app, builds the DMG,
then signs, notarizes and staples the DMG. `generate_appcast` signs the update with the Keychain
key. Publish the DMG and `appcast.xml` alongside the runtime archive and manifest in the same
GitHub Release. Keep prior update archives when generating later feeds. Publish the corresponding
Wine/Mullion source tags and dependency archives with each runtime, and record their commits.

Validate the updater before publishing:

```sh
swift test --package-path App
```

The tests exercise installation from a hash-pinned archive, repeat installation, round-trip
rollback, corrupted rollback refusal, failed-update preservation and invalid version refusal.
A complete application update still needs testing between two notarized releases using the
real update feed. Fresh Steam sign-in, game installation and flight through this application's
prefix are separate acceptance steps; the earlier research flight does not qualify them.
