# MetalFlight

MetalFlight is a macOS app that runs **your own Steam copy of Microsoft Flight Simulator 2024**
on Apple Silicon Macs, through a Wine runtime built from source with patches for this game.

> **Experimental preview.** The maintainer completed a **free-flight mission** with
> **MetalFlight 0.1.3 / runtime dev5 and MSFS 1.9.21.0** on an **Apple M1 Pro with
> 32 GB of unified memory**, then exited normally (code 0). Steam sign-in and saved login
> are also user-confirmed. Long flights, repeated-launch reliability, other game modes
> and other Mac models remain unverified. Bloom-on sign-in, menu and moving video were
> confirmed in a separate research launch; behavior in MetalFlight's own prefix and
> bloom-on flight remain unverified.

## Download

Download [MetalFlight-0.1.5.dmg](https://github.com/AndreaCodinLife/metalflight/releases/download/v0.1.5/MetalFlight-0.1.5.dmg),
open it and drag **MetalFlight** to **Applications**. Launch the app and follow the setup steps.
The runtime downloads separately; Steam and the game are installed using your own account.

This preview is **ad-hoc signed and not notarized by Apple**. macOS may block its first
launch; review the app and use **System Settings → Privacy & Security → Open Anyway** if you
choose to allow it. [Release notes and checksums](https://github.com/AndreaCodinLife/metalflight/releases/tag/v0.1.5)
describe the exact build and its limits.

MetalFlight is not affiliated with, endorsed by or supported by Microsoft, Asobo Studio, Valve,
Apple or CodeWeavers. "Microsoft Flight Simulator" is a trademark of Microsoft; it is used here only
to say which game the app runs.

## Requirements

- An Apple Silicon Mac. The verified research configuration is **M1 Pro with 32 GB of unified
  memory**; other chips and memory configurations have not yet been qualified.
- macOS 15 or later, with Rosetta 2 (the shipped renderer requires macOS 15).
- About 20 GB free for MetalFlight, plus the game itself.
- A free Apple Account, to download Apple's **Game Porting Toolkit** from developer.apple.com. It is
  only used to get Apple's `libd3dshared.dylib`; MetalFlight never redistributes it.
- Your own Steam account that owns Microsoft Flight Simulator 2024.

## What the app does

1. Checks your Mac.
2. Downloads the MetalFlight runtime from this repository's releases and verifies its SHA-256.
   Updates install side by side, and one click rolls back to the previous runtime.
   Application updates use Sparkle, with a separate “Check for Application Updates…” menu item.
   Update archives are signed with the project's Ed25519 key; its private key stays in the
   maintainer's Keychain. An application update between two public versions is still untested.
3. Imports `libd3dshared.dylib` from the Game Porting Toolkit you downloaded, after checking that
   Apple signed it.
4. Creates a private Windows environment (Wine prefix) in
   `~/Library/Application Support/MetalFlight`.
5. Installs Valve's Steam client. Its interface uses a separate DirectX 11 configuration;
   you sign in and install the game as on Windows. Steam remembers the login when enabled.
   Version 0.1.1 fixes the missing graphics bridge and keeps Steam's browser rendering
   in one process, while leaving the simulator's DirectX 12 configuration separate.
6. **Play** installs Microsoft's official x64 Visual C++ prerequisites when missing, then
   starts Steam if needed and waits until the Steam SDK reports you are signed in and own
   the game. It then starts the simulator with MetalFlight's patches. Version 0.1.2 fixes
   an immediate exit 53 caused by missing native C++ libraries in a fresh environment.
   The Microsoft installer downloads directly from Microsoft; it is not bundled in MetalFlight.
   Version 0.1.3 supports game builds **1.9.18.0 and 1.9.21.0** with separate CPU patch
   locations. Other executable builds are refused before launch, with an update message.
   Version 0.1.4 selects classic wheel notches for MSFS trackpad scrolling, which the user
   found improves click response after scrolling on the globe in the research launcher. The
   app applies it only to `FlightSimulator2024.exe`; globe zoom may feel less smooth. The
   0.1.4 app-prefix behavior has not been separately confirmed. Version 0.1.5 enables the
   renderer's bloom input by default. Bloom-on sign-in, menu and moving video were confirmed
   in a separate research launch; bloom appearance, flight, and behavior in MetalFlight's
   own prefix remain unverified.

## What the runtime contains

| Component | Source | Licence |
| --- | --- | --- |
| Wine (CrossOver 26.3 source release + MetalFlight patches) | [metalflight-wine](https://github.com/AndreaCodinLife/metalflight-wine) | LGPL-2.1-or-later |
| Mullion Direct3D 11/12 → Metal renderer (DXMT fork) | [metalflight-mullion](https://github.com/AndreaCodinLife/metalflight-mullion) | LGPL-2.1 |
| CPU split-push helper (Rosetta workarounds) | `helper/` | GPL-3.0 |
| GLib, GStreamer, gnutls, nettle, GMP, FreeType, libffi, PCRE2, proxy-libintl, SDL2 | built by `runtime/` | see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) |

The runtime contains no binary from CrossOver.app, no Apple software and no game file.
Runtime dev6 ships on the app 0.1.5 release. It keeps runtime dev5's Wine, Mullion and
CPU helper binaries, changing the game profile to leave bloom enabled. Wine, Mullion and
dependency source archives remain on the
[dev4 release](https://github.com/AndreaCodinLife/metalflight/releases/tag/v0.1.1);
those components are unchanged in dev5 and dev6. Wine and Mullion are
pinned by `runtime/sources.lock.json` and tagged `runtime-2026.10.23-dev4` in their repositories.

## Building

See [docs/BUILDING.md](docs/BUILDING.md). In short: `runtime/fetch_sources.py`,
`runtime/build_deps.py`, `runtime/build_wine.py`, `scripts/package_runtime.py` and
`scripts/bundle_app.sh`.

## Licence

The MetalFlight app, scripts and helper are free software under the
[GNU General Public License v3](LICENSE). The bundled components keep their own licences.

---

**En français.** MetalFlight lance votre propre copie Steam de Microsoft Flight Simulator 2024 sur
les Mac Apple Silicon, avec un runtime Wine compilé depuis les sources. Il vous faut :
- un compte Apple gratuit, pour télécharger le Game Porting Toolkit d'Apple, dont l'app utilise
  uniquement `libd3dshared.dylib` ;
- votre compte Steam, avec le jeu.

La configuration de recherche vérifiée est un **Mac M1 Pro avec 32 Go de mémoire unifiée**.

C'est un projet expérimental, sans lien avec Microsoft, Valve, Apple ou CodeWeavers.
