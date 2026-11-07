MetalFlight **0.1.8 Beta 5** updates the opt-in Preview channel. Stable remains
**0.1.7**. This release adds runtime `2026.11.07-dev12-preview1` for testing the
experimental issue #8 workaround. Updating the app alone does not install the
runtime; select **Settings → Updates → Preview**, then install the runtime
update.

### Issue #8 Preview experiment

- Adds a targeted TEB fiber stack preflight in Wine `ntdll` for the MSFS game
  process. It preserves Windows guard-page and `StackLimit` bookkeeping.
- Enables the workaround only for `FlightSimulator2024.exe` through the game
  profile. Steam is not opted in.
- The owned four-fiber positive control and disabled/wrong-target guard-page
  controls pass. The app's exact runtime install/reinstall/rollback check also
  passes.
- **The PMDG 737-800 POS INIT and iFly 737 MAX 8 SID cases have not been tested
  with this Preview runtime. This is an experimental test build; the issue is
  not confirmed fixed.** Please report results and sanitized logs on
  [issue #8](https://github.com/AndreaCodinLife/metalflight/issues/8).

The runtime archive contains 1,866 inventoried files and 52 safe symlinks.
The app is ad-hoc signed and not Apple-notarized. The Preview feed and update
signatures use the existing Sparkle signing key. A full interactive updater
transition between notarized releases remains unverified.
