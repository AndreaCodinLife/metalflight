# Issue #8: TEB fiber stack preflight Preview

Preview Beta 5 includes an opt-in-in-profile Wine change for the PMDG and iFly
FMC hangs reported in [issue #8](https://github.com/AndreaCodinLife/metalflight/issues/8).
The change is an experimental compatibility workaround; the reported aircraft
and FMC cases have not yet been verified with this runtime.

At syscall-return stack preflight, the Wine `ntdll` change opens host access
below the active fiber's `StackLimit` down to the guaranteed reserve. Windows
guard-page state, `StackLimit`, and `VirtualQuery` bookkeeping remain intact.
The runtime profile enables `MSFS24_WINE_STACK_OPEN=1` only for
`FlightSimulator2024.exe`; Steam settings do not enable it.

The source is based on Wine commit
`3384084f9de2f8e7f835ef9ed5ca36a36094b101`. Both the credential-enumeration
patch and TEB fiber patch, their source hashes, and the expected patched source
hashes are recorded in `runtime/sources.lock.json`. The build verifies and
applies those patches to the pinned source. The candidate `ntdll.so` SHA-256 is
`6b7d5fdf682bde0b13c78fd4e7b5bac2ce54d668a3ed1b48227d9da4f7e76d53`.

Owned controls pass for four distinct fibers with the workaround enabled, and
the disabled and wrong-target cases retain their expected guard-page fault.
The exact runtime archive passed install, reinstall, rollback, and roll-forward
in the app's isolated package test. These checks do not establish that the
PMDG 737-800 POS INIT airport entry or iFly 737 MAX 8 SID selection is fixed.
Please report the outcome of those steps on issue #8, with sanitized logs if a
hang remains.
