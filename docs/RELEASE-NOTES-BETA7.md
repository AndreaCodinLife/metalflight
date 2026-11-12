MetalFlight **0.1.9 Beta 7** updates the opt-in Preview channel and adds an **experimental D3DMetal renderer**. Stable
remains **0.1.7**, and **Mullion stays the default renderer**. This release adds runtime
`2026.11.12-dev12-preview2-d3dmetal1`. Updating the app alone does not install the runtime: under **Settings → Updates
→ Preview**, install the runtime update.

### D3DMetal renderer (experimental)

- **What it is.** MetalFlight can run MSFS 2024 with Apple's D3DMetal instead of Mullion. MetalFlight ships **none of
  Apple's files**. The D3DMetal files come from your own Game Porting Toolkit download, as `libd3dshared.dylib`
  already does.
- **Toolkit requirement.** Only **Game Porting Toolkit 4.0 beta 3** is accepted, and only an exact match to the tested
  build: every file hash and Apple's code signature are checked at import and again before each launch. Any other
  toolkit version is refused, and Mullion is unaffected.
- **How to try it:**
  1. Install the runtime update.
  2. Open the Game Porting Toolkit 4.0 beta 3 disk image and choose **Import from mounted disk** (step 3) again.
  3. Choose **Settings → Renderer → D3DMetal (experimental)**.
  4. Switch back to **Mullion** at any time.
- **MetalFlight's own parts:**
  - a small `d3d12.dll` shim, which keeps the CPU helper and mission guard and bridges the menu video's shared texture
    to D3D12;
  - two opt-in Wine changes used only by this renderer:
    - a per-thread stack-guard cache, because D3DMetal makes a Wine call for almost every Direct3D call;
    - a direct menu-video path, because D3DMetal's 4K D3D11 uploads are slow.
- **Tested on one Apple M1 Pro.**
  - The 4K menu video plays at normal speed (60 fps) without pink artefacts. A flight works.
  - At one spot, with the same settings, D3DMetal reached at most 44 FPS against Mullion's 32. Both were GPU-limited.
  - This is one comparison on one machine, so your results may differ.
- **Known D3DMetal limits:**
  - one compute operation used for quad votes fails in owned tests;
  - no GPU timing queries, so the Developer Mode GPU timings read 0;
  - partial 3D texture views are unsupported;
  - shared textures are bridged only for the menu video.
  - Long flights, repeated launches on many machines and every aircraft and scenery are untested. Please report results
    and sanitized logs.

### Mullion

- With **Mullion** selected, the game uses the same Mullion files and settings as runtime `2026.11.07-dev12-preview1`.
  The two rebuilt Wine modules contain the new code, but it stays off unless D3DMetal is selected.

The runtime archive contains 1,867 inventoried files. The app is ad-hoc signed and not Apple-notarized. A full
interactive updater transition between notarized releases remains unverified.
