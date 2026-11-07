MetalFlight **0.1.8 Beta 4** updates the opt-in Preview channel. Stable remains
**0.1.7**. This release includes the app changes and the separate runtime
correction; installing the app alone does not install the runtime.

### Changes

- Aligns the windowed simulator resolution with the Mac display so visible UI
  buttons and pointer hit-testing line up. The app restores the prior settings
  after the game exits and preserves changes made while playing.
- Fixes Wine credential enumeration when the host credential provider has no
  matching entry, allowing an existing registry credential for a remembered
  Microsoft account to remain available.
- Adds runtime `2026.11.07-dev11-preview1`, based on the published dev10 runtime
  with the credential fix. The Wine source patch and source lock are included.

### Validation

The user confirmed a working centre click at the Microsoft Continue button,
usable sign-in, menu and moving video. The user also confirmed a successful
flight in the Beta 4 candidate. The Hybrid Metal 4 Preview7 flight and the
dev10-based credential-fix flight were separate runs; airport, aircraft,
duration, takeoff and mission completion were not specified. An earlier
separate attempt froze at 6% during initial loading; startup freezes are not
claimed universally fixed.

All 22 standard app tests pass; a separate test of the exact runtime archive through install, reinstall, rollback, and roll-forward also passes. The runtime archive contains 1,866 inventoried files
and 52 safe symlinks, and its inventory matches the archive. The app DMG is
ad-hoc signed and not Apple-notarized. The Preview feed and update signatures
use the existing Sparkle key. The full interactive updater transition between
two notarized releases has not been tested.
