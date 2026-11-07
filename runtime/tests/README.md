# TEB fiber stack guard controls

These controls verify the `MSFS24_WINE_STACK_OPEN` gate against a disposable
Wine prefix. They do not start Steam or Microsoft Flight Simulator.

From the repository root, run:

```sh
python3 runtime/tests/qualify_fiber_stack_open.py \
  --runtime /path/to/metalflight-runtime \
  --cc /opt/homebrew/bin/x86_64-w64-mingw32-gcc \
  --work /path/to/new-owned-control-run
```

Use a fresh `--work` directory for each run. The positive arm creates four
distinct Windows fibers and checks that writes can pass the host guard while
`PAGE_GUARD` and `StackLimit` remain unchanged. The disabled and wrong-target
arms require the expected guard-page stack-overflow fault and no host-open
trace. A timeout is a failure, not a pass.

These controls qualify the guard-page contract. They do not prove that PMDG or
iFly aircraft FMC flows are fixed; those cases require an interactive simulator
test with the named add-ons.
