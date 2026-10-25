# Building MetalFlight

Everything builds on an Apple Silicon Mac (macOS 15+) with Xcode, Rosetta and Homebrew. The runtime
is x86_64 and runs under Rosetta. The app is arm64.

Homebrew packages: `mingw-w64` (GCC 16.2 cross compilers, used for every PE module),
`meson`, `ninja`, `cmake`, `pkg-config`, `autoconf`, `bison`.

The CPU helper needs llvm-mingw 20260908 (UCRT, clang 23.1.1) to reproduce the qualified binary.

## 1. Sources

```sh
git clone https://github.com/AndreaCodinLife/metalflight-wine runtime/work/metalflight-wine
git -C runtime/work/metalflight-wine checkout 3384084f9de2f8e7f835ef9ed5ca36a36094b101
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
Clone Mullion with `--recurse-submodules` and check out `runtime-2026.10.23-dev4` to reproduce
this runtime's source revision. Its `METALFLIGHT.md` documents the renderer build.

For subsequent builds, the renderer's `winemetal.dll.postproc` Ninja target must
run before staging `winemetal.dll`. The corrected renderer build script and
runtime packager check its Wine builtin marker; a bridge without that marker
cannot load under the profile's `winemetal=b` setting.

The Steam rendering configuration uses
`MSFS24_WINE_STEAM_IN_PROCESS_GPU=1` in the patched Wine source. Validate a
candidate Wine runtime with authored executables in a new private prefix:

```sh
python3 runtime/test_steam_options.py --runtime /path/to/candidate-runtime \
    --compiler /path/to/x86_64-w64-mingw32-gcc --work /path/to/new-owned-check
```

This checks option gating, Unicode arguments, duplicate prevention and child
processes. It does not launch Steam or establish account or game readiness.

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
scripts/package_app.sh 0.1.1
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
scripts/package_app.sh 0.1.1 'Developer ID Application: …' '<notary-profile>'
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


## Visual C++ prerequisites (app 0.1.2)

Before starting the game, `Launcher.play` checks all six native-only x64 C++
DLLs. Wine builtin files do not satisfy this check. Missing or partial setups
trigger Microsoft's official x64 installer from
`https://aka.ms/vc14/vc_redist.x64.exe`, with `/install /quiet /norestart` in
this app's own prefix, without the game's native-only DLL overrides. The files
are checked again before launch. Microsoft DLLs and installers are not release
assets. `--headless install-prerequisites` repairs the same prerequisite without
launching a game; `--headless play` exercises the normal launch sequence and
reports the game's exit without stopping Steam.

The authored dependency loader and MSVC exception/copy/destructor control were
qualified against missing-prerequisite and installed-prerequisite environments
on runtime dev4. Unit tests cover builtin, x86, truncated and partial-file
rejection. These controls do not qualify game sign-in or flight.


## Game build compatibility (app 0.1.3 / runtime dev5)

The original helper table targeted MSFS 1.9.18.0. In 1.9.21.0, the executable's
functions moved, so target 12 failed preparation, no core sites were installed,
and a Rosetta EmulateForward error was observed during loading at 28%.

`helper/src/game_build.h` selects explicit RVA mappings for the two known PE
builds. The launcher checks exact executable hashes from the runtime profile
before installing prerequisites or starting the game. Legacy profiles are
restricted to 1.9.18.0. Unknown PE build metadata is also refused inside the
helper. Exact code preimages and unwind validation remain mandatory before
any edits. There is no runtime pattern scanner or fallback offset guess.

New locations were aligned using matching old/new unwind metadata and nine
neighboring function signatures, then validated by the helper's original
preimage and unwind checks in a live 1.9.21.0 trial. All 22 core sites installed
and the mission-empty guard verified its real caller/getter/cleanup code.
Diagnostic RVA values remain original table keys; `game_build=2` selects the
1.9.21.0 mapping. Owned qualification covers 16 CPU cases, eight mission-guard
cases, and metadata/unknown-image refusal. Ten Swift tests pass.

The Wine and renderer binaries remain those from dev4. Per-frame shared media
tracing is omitted from the profile: this renderer treats the variable's
presence as enabled, including a value of "0". Saved prefixes, credentials,
game files and Apple libraries remain outside release assets.


The 2026-10-24 trial subsequently completed a user-confirmed free-flight mission
on the maintainer's M1 Pro (32 GB) and MSFS 1.9.21.0, with normal exit 0 and no
Rosetta EmulateForward diagnostic. The 22 core sites and mission guard installed.
The later optional GI and RI sites were skipped by their existing checks; they
are not counted as installed. This is one successful application-prefix trial,
not a repeated-launch, long-flight, other-mode or other-hardware qualification.

## Hardware context cache correction

Runtime 2026.10.25-dev7 in release 0.1.6 uses the [Wine context-cache correction](https://github.com/AndreaCodinLife/metalflight-wine/pull/1).
The game profile enables `MSFS24_WINE_HW_CONTEXT_CACHE=1` with the existing
Rosetta hardware-watch setting. Steam's profile is unchanged. Build a new Wine
runtime from the pinned source: changing a profile cannot add the correction to
an older native module. The new archive retains dev6 files except for the native
`ntdll.so`, the game profile, source lock and inventory metadata. Prior release
assets are preserved.

An authored suspended-thread/NOP control reproduces zero debug-register
readback and a false `0x80000003` breakpoint in the previous implementation.
The correction passes six owned contracts and all 16 CPU helper regression
controls, including expected failure controls. The portable source test runner
also passes its six ON contracts in a fresh owned prefix.

One MSFS 1.9.21.0 candidate launch with bounded trap diagnostics reached
user-confirmed usable sign-in, moving video and menu, followed by a normal quit
and exit 0. A repeat with extra trap tracing off reached a Microsoft sign-in
error and was restarted at the user's request. After that restart, the user
confirmed usable sign-in, moving video and the menu with extra trap tracing
still off. That repeat remains open; no normal quit or exit is inferred.
The natural game breakpoint has not reproduced in the new clone, and the issue reporter's
M2 Max remains untested. This addresses a reproduced platform defect related to
[issue #1](https://github.com/AndreaCodinLife/metalflight/issues/1); it does not
establish long-term game reliability or close the original report. Saved prefixes and raw fault captures are not source
or release assets.

## Recycled-stack recovery correction

Release 0.1.7 / runtime 2026.10.25-dev8 pins Wine source
`3384084f9de2f8e7f835ef9ed5ca36a36094b101`. Relative to dev7, only the native
`ntdll.so` and the source-lock/inventory metadata change. The game profile,
renderer, CPU helper and all other dependency binaries remain unchanged.
`MSFS24_WINE_SKIPPED_GUARD_COMMIT=1` was already enabled in the game profile.
No incoming-fiber priming or extra per-frame diagnostics are enabled.

Rebuild Wine from the pinned source with the existing runtime build procedure.
The qualified module was linked from the dev7 context-cache object and the new
`virtual.c` object; all thirteen compared non-debug sections of an unmodified
baseline rebuild match dev7. Full source archives and component hashes accompany
the release.

The new authored fixture and portable runner live in the Wine source tree:

```sh
python3 msfs24/test_recycled_stack.py --runtime /path/to/dev8-runtime \
  --cc /path/to/x86_64-w64-mingw32-clang --work /new/owned-stack-test
```

Use `--baseline` with dev7 to require the two expected recycled-stack failures.
Ten candidate stack contracts, six existing context contracts and sixteen CPU
helper regressions pass; the portable stack runner independently repeats all ten
contracts. Guard-off, read-only, no-access, committed-gap, missing-guard and
excessive-distance cases must remain refused without partial page mutation.
The earlier branch handling a guard marker on the faulting page is unchanged.

These controls match the final stack-fault shape of both issue-4 logs, not a
complete game trace. A320 ON APU / INIT A DEP-ARR / FPLN recovery, sign-in/video
on dev8, flight, repeated launches and reporter-hardware behavior remain
unverified. Do not inherit dev7's game confirmations as new dev8 observations.
