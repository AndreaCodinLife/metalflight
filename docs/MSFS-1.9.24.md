# MSFS 1.9.24 compatibility — Preview Beta 3

Install MetalFlight **0.1.8 Beta 3**, choose **Settings → Updates → Preview**, and
install runtime **2026.11.05-dev10-preview1** with **Update**. Existing saved login
and game files stay in your private Windows environment. Earlier runtimes remain
available through **Roll back**. Stable remains 0.1.7 and does not receive this
Preview update automatically.

The runtime profile recognizes exact executable hashes for MSFS 1.9.18.0,
1.9.21.0 and 1.9.24.0. The helper independently rejects unknown PE metadata and
requires exact live code preimages before patching. The 1.9.24 executable hash is
`7f07d859d6c94eec905a7ee914b8f1db2268b3b5952b0ed755e7ed798c484a07`.

Beta 3 ports both the startup patches and the four mission-guard addresses. The
guard redirects an empty result at one verified caller through that caller's
existing cleanup/return path. It does not fabricate a successful result or a
replacement object. Missing guard mappings in the first development candidate
caused the guard to refuse installation; that candidate froze loading an airport.
The corrected helper installed its 22 core sites, six optional sites and mission
guard in the live game. The guard handled 12 empty calls by the takeoff check.
The optional 1.9.21 Rosetta target also accounts for its leading instruction prefix.

The maintainer reports that the corrected configuration works and confirms
takeoff from Charles de Gaulle on an Apple M1 Pro with 32 GB unified memory.
This confirms one takeoff, not a completed mission, long-flight stability or
repeated-launch reliability. Another corrected-helper attempt froze at 6% of
initial loading before the menu. A later Steam SDK readiness check stalled;
after restarting only that isolated Wine/Steam session, the normal visible Play
path reached the successful flight. No claim is made that every startup freeze
is resolved or that the launch-path change alone caused recovery.

Qualification includes 42 owned controls: 3 metadata/name/refusal controls,
23 mission-guard arms (including mismatch rejection and concurrent install),
and 16 CPU regression arms. The expected Rosetta negatives must emit their
failure diagnostic and must not emit the success summary; their bounded timeouts
are not successful application exits. The metadata tests cover all four guard
mappings for all three supported builds. Sources are in `helper/src` and
`helper/tests`; the authored mission fixture is `helper/tests/owned_mission_guard.c`.
The sanitized results are in `MSFS-1.9.24-validation.json`.

Helper SHA-256:
`a851e8adf057a8e8db5599e55c064db6fe226c3b48d933f4fdb1a8f4239e25c8`.
Build it with `helper/build.sh` and the pinned llvm-mingw 20260908 toolchain.
The Wine, renderer and dependency binaries are unchanged from Beta 2. Device12
and BC1 casting retain their existing limited scope. GPU memory exhaustion,
intermittent vegetation, other game modes and other hardware remain outside this
qualification. The application is ad-hoc signed and is not Apple-notarized.

Release assets contain the distributable runtime and corresponding source only.
They contain no signed-in prefix, game files, Apple library, Microsoft installer,
account data, captured game images or private fault/code traces.
