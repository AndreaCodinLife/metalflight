# Building MetalFlight

Everything builds on an Apple Silicon Mac (macOS 14+) with Xcode, Rosetta and Homebrew. The runtime
is x86_64 and runs under Rosetta. The app is arm64.

Homebrew packages: `mingw-w64` (GCC 16.2 cross compilers, used for every PE module),
`meson`, `ninja`, `cmake`, `pkg-config`, `autoconf`, `bison`.

The CPU helper needs llvm-mingw 20260908 (UCRT, clang 23.1.1) to reproduce the qualified binary.

## 1. Sources

```sh
git clone https://github.com/AndreaCodinLife/metalflight-wine runtime/work/metalflight-wine
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
```

Mullion builds from metalflight-mullion. The Steam readiness probe builds from `probes/steam_session.c`.

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
