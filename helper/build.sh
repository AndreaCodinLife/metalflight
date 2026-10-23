#!/bin/bash
# Build the portable CPU split-push helper (split_push_helper.dll) exactly as qualified:
# llvm-mingw 20260908 (UCRT, clang 23.1.1), -O2, pinned PE timestamp.
# usage: helper/build.sh /path/to/llvm-mingw-20260908-ucrt-macos-universal [OUTPUT_DIR]
# The DLL name is embedded in its export table: it is built as cpu_split_helper.dll (the qualified
# file); MetalFlight installs it as C:\metalflight\split_push_helper.dll.
set -euo pipefail
cd "$(dirname "$0")"
TOOLCHAIN="${1:?path to llvm-mingw-20260908-ucrt-macos-universal}"
OUT="${2:-.}/cpu_split_helper.dll"
"$TOOLCHAIN/bin/x86_64-w64-mingw32-clang" -O2 -mcx16 -Wall -Wextra -Werror -Isrc -shared \
  src/catalog_home_helper.c src/split_push_padded.c src/home_entry_split.c src/split_push_detour.c src/mission_empty_guard.c \
  -Wl,-Xlink=-timestamp:1792672346 -o "$OUT"
shasum -a 256 "$OUT"
