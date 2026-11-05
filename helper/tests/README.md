# Owned helper controls

Build the helper with the pinned llvm-mingw 20260908 toolchain:

```sh
helper/build.sh /path/to/llvm-mingw-20260908-ucrt-macos-universal /tmp/helper-build
```

`game_build_metadata.c` checks all supported metadata and RVA mappings, including
the four mission-guard addresses for 1.9.18, 1.9.21 and 1.9.24. Compile with
`-Ihelper/src`. `unknown_game.c` loads the helper and expects rejection -102
when built under the name `FlightSimulator2024.exe` with unrelated metadata;
provide `MSFS24_LAB_SPLIT_LOG=C:\metalflight\logs\unknown.txt` and create that
directory first. The filename is an authored fixture, not a game executable.
Under another name, pass `-100` as the fixture's second argument to check name
refusal instead.

`owned_mission_guard.c` emits its own getter/caller/cleanup code and checks the
guard exported by the complete helper. Build it as `owned_mission_guard.exe`
with `-O2 -mcx16 -Wall -Wextra -Werror`, then pass the helper DLL path and a mode:

```text
off
negative-null
bad-getter
bad-caller
bad-cleanup
positive
positive-repeat
install-race
```

The negative-null mode must observe read address 16. Mismatch modes must reject
without changing code or incrementing the redirect count. Positive and race
modes require `pass=1` and check returned values, cleanup, flags, unrelated
callers and redirect counts. Beta 3 qualification included sixteen separate
install-race invocations. CPU catalogue fixtures and their authored assembly
bodies are in `helper/src/owned_*`; they test all core and optional sites.

Use an owned-only Wine prefix with no Steam, account, or game data, and the
exact runtime under test. Keep Wine sessions isolated between controls, as in
the recorded successful suite. Do not run fixture controls in a signed-in game
prefix. The 42-arm result and its scope are recorded in
`docs/MSFS-1.9.24-validation.json`; a helper-control pass is not a flight result.
