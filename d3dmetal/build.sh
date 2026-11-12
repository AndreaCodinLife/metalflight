#!/bin/bash
# Build the D3DMetal renderer shim (d3d12.dll) exactly as qualified (research shim v7, c71f8c2e…):
# llvm-mingw 20260908 (UCRT), -O2, no PE timestamp, source paths mapped to ".".
# usage: d3dmetal/build.sh /path/to/llvm-mingw-20260908-ucrt-macos-universal [OUTPUT_DIR]
# The shim keeps Mullion's CPU-helper load point in D3D12CreateDevice and forwards every other d3d12 export to Apple's
# D3DMetal d3d12.dll, which MetalFlight installs from the user's own Game Porting Toolkit as d3dm12.dll. It also bridges
# legacy shared D3D11 textures to D3D12 (menu video; MSFS24BridgeSubmitFrame for the patched media engine) and makes
# IDXGIOutput::WaitForVBlank return immediately, as Mullion does. No Apple code is shipped or modified.
set -euo pipefail
TOOLCHAIN="${1:?path to llvm-mingw-20260908-ucrt-macos-universal}"
TOOLCHAIN="$(cd "$TOOLCHAIN" && pwd -P)"
mkdir -p "${2:-.}"
OUT="$(cd "${2:-.}" && pwd -P)/d3d12.dll"
cd "$(dirname "$0")/src"
"$TOOLCHAIN/bin/x86_64-w64-mingw32-clang" -O2 -Wall -Wextra -Wl,--no-insert-timestamp "-ffile-prefix-map=$PWD=." \
  -shared -o "$OUT" d3d12_helper_shim.c shared_bridge.c vblank.c d3d12.def
shasum -a 256 "$OUT"
