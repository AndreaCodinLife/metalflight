# MetalFlight

MetalFlight is a macOS app that runs **your own Steam copy of Microsoft Flight Simulator 2024**
on Apple Silicon Macs, through a Wine runtime built from source with patches for this game.

> **Status: experimental.** A complete session (sign-in, menu video, globe, free flight, normal
> exit) has been verified on one Mac. Repeated-launch reliability, long flights and other Mac models
> are not yet established. Expect rough edges.

MetalFlight is not affiliated with, endorsed by or supported by Microsoft, Asobo Studio, Valve,
Apple or CodeWeavers. "Microsoft Flight Simulator" is a trademark of Microsoft; it is used here only
to say which game the app runs.

## Requirements

- An Apple Silicon Mac. The Mullion renderer targets M3-generation and later GPUs.
- macOS 14 or later, with Rosetta 2 (macOS offers to install it).
- About 20 GB free for MetalFlight, plus the game itself.
- A free Apple Account, to download Apple's **Game Porting Toolkit** from developer.apple.com. It is
  only used to get Apple's `libd3dshared.dylib`; MetalFlight never redistributes it.
- Your own Steam account that owns Microsoft Flight Simulator 2024.

## What the app does

1. Checks your Mac.
2. Downloads the MetalFlight runtime from this repository's releases and verifies its SHA-256.
   Updates install side by side, and one click rolls back to the previous runtime.
3. Imports `libd3dshared.dylib` from the Game Porting Toolkit you downloaded, after checking that
   Apple signed it.
4. Creates a private Windows environment (Wine prefix) in
   `~/Library/Application Support/MetalFlight`.
5. Installs Valve's Steam client. You sign in and install the game as on Windows.
6. **Play** starts Steam if needed and waits until the Steam SDK reports you are signed in and own
   the game. It then starts the simulator with MetalFlight's patches.

## What the runtime contains

| Component | Source | Licence |
| --- | --- | --- |
| Wine (CrossOver 26.3 source release + MetalFlight patches) | [metalflight-wine](https://github.com/AndreaCodinLife/metalflight-wine) | LGPL-2.1-or-later |
| Mullion Direct3D 11/12 → Metal renderer (DXMT fork) | [metalflight-mullion](https://github.com/AndreaCodinLife/metalflight-mullion) | LGPL-2.1 |
| CPU split-push helper (Rosetta workarounds) | `helper/` | GPL-3.0 |
| GLib, GStreamer, gnutls, nettle, GMP, FreeType, libffi, PCRE2, proxy-libintl, SDL2 | built by `runtime/` | see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) |

The runtime contains no binary from CrossOver.app, no Apple software and no game file.

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

C'est un projet expérimental, sans lien avec Microsoft, Valve, Apple ou CodeWeavers.
