# Experimental D3DMetal renderer

MetalFlight's default renderer is Mullion, which ships in the runtime. Runtime `2026.11.12-dev12-preview2-d3dmetal1`
also describes an optional **D3DMetal** renderer: Apple's D3DMetal from the user's own Game Porting Toolkit, plus
MetalFlight's shim and two opt-in Wine changes.

## What is shipped and what is not

| Component | Ships? | Source |
| --- | --- | --- |
| `share/metalflight/d3dmetal/d3d12.dll` (shim) | Yes | `d3dmetal/src`, built by `d3dmetal/build.sh` (llvm-mingw 20260908) |
| Wine `ntdll.so` stack-guard fast path and cache, opt-in (`MSFS24_WINE_STACK_CACHE=1`) | Yes | `runtime/patches/d3dmetal-stack-guard-cache*.patch` |
| Wine `mfmediaengine.dll` direct media path, opt-in (`MSFS24_WINE_MEDIA_D3DMETAL_DIRECT=1`) | Yes | `runtime/patches/d3dmetal-direct-media-*.patch` |
| `D3DMetal.framework`, D3DMetal's Wine DLLs (`redist/lib/wine`), `libd3dshared.dylib` | **No** | Imported by the app from the user's own Game Porting Toolkit |

The app copies the toolkit's `redist/lib` into `gptk/redist` in its support folder. It accepts the copy only if every file
matches the hashes pinned in `profile.json` (`renderers.d3dmetal.gptk`, Game Porting Toolkit 4.0 beta 3), the symlinks
match, and `D3DMetal.framework` and `libd3dshared.dylib` carry Apple's code signature (`anchor apple`). It then adds
`d3dm12.dll`, a copy of the toolkit's `d3d12.dll`, with its `.so` link. Nothing Apple ships is modified.

## How a D3DMetal launch differs

`renderers.d3dmetal` in `runtime/profile.json` overlays the Mullion profile:
- `WINEDLLPATH` points at the imported `gptk/redist/wine`, so Wine loads D3DMetal's builtin d3d11, dxgi, d3d12core and
  d3dm12 from there;
- the game's DLL overrides become `d3d12=n;d3dm12,d3d12core,d3d11,dxgi=b;…`, and `nvapi64` is disabled for the game
  and Steam;
- system32 `d3d12.dll` is the shim, which loads the CPU helper as Mullion does and forwards to `d3dm12.dll`, and
  `d3dm12.dll` is copied from the import;
- game settings: `MSFS24_WINE_STACK_CACHE=1`, `MSFS24_WINE_MEDIA_D3DMETAL_DIRECT=1`, `MSFS24_WINE_MEDIA_SHARED_WAIT=0`.

With Mullion selected, none of this applies, and every launch reinstalls Mullion's system32 files.

## Why the shim and the Wine changes exist

- **Video bridge.** D3DMetal has no shared handles (`GetSharedHandle`/`OpenSharedHandle` return `E_NOTIMPL`), so the
  shim bridges MSFS's legacy shared video texture to a D3D12 texture.
- **Direct video path.** D3DMetal's D3D11 uploads of a 4K frame take 15–24 ms of CPU, and `D3D11On12CreateDevice` is
  unsupported. So the media engine, in direct mode, hands decoded system-memory frames to the shim. The shim copies each
  one once and uploads it on the game's DIRECT queue: 60 fps, with no pink from unordered queues.
- **VBlank.** `WaitForVBlank` returns immediately, as with Mullion.
- **Stack-guard cache.** Wine's per-syscall stack-guard preflight dominated D3DMetal-heavy threads. The cache repeats a
  thread's previous "no guard in range" answer without the lock while the page-protection/view generation, the TEB
  stack bounds and the rsp floor are unchanged.

## Qualification

Owned controls on the exact runtime, with D3DMetal loaded only from a user-style toolkit folder:
- the stack-guard controls pass with the cache on and off;
- capabilities: D3DMetal SM 6.8 / FL 12_2, Mullion SM 6.7 / FL 12_1;
- the menu video runs at 59.95 fps (speed 1.000) with direct mode and 19.25 fps without; owned 4K test-clip pixels are
  exact; disabling the bridge fails as required;
- Steam options pass.

App tests cover renderer profiles, overlay composition, and toolkit import and refusal, including framework symlinks and
the real Apple signature requirement. The packaged archive passes install, reinstall and rollback. In game, on one Apple
M1 Pro, the menu video, a flight and visuals were confirmed with D3DMetal.

Known gaps:
- compute quad-vote failures in owned tests;
- no GPU timestamp queries;
- no partial 3D texture views;
- shared textures are bridged only for the menu video's whole-frame transfers.
