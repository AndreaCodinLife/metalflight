# Issue 9: windowed resolution and Microsoft sign-in

## Resolution alignment

On the affected Mac display, the client area used logical display points while
MSFS rendered its windowed UI at the smaller `UserCfg.opt` resolution. The
image was stretched but pointer hit-testing remained in the old coordinate
space, so the Microsoft Continue button highlighted above its visible bounds.

Before launching MSFS, MetalFlight now checks the unique user's `UserCfg.opt`.
For windowed mode only, it temporarily sets `Resolution` and
`FullScreenResolution` to the main display's logical dimensions. It records the
previous values in a mode-0600 journal, restores them when the game exits, and
recovers an interrupted override at the next launch. If the user changes either
resolution while playing, MetalFlight leaves that choice intact. Fullscreen,
ambiguous profiles, missing settings, and already-matched resolutions are left
alone.

The user confirmed that clicking the centre of the visible Continue button
works at 1280×720 with the change installed.

## Remembered Microsoft account

The Wine `CredEnumerateW` implementation combines credentials from the Wine
registry and the host credential provider. When the host had no matching
credential, it returned `ERROR_NOT_FOUND` and discarded an already successful
registry result. The Wine patch treats that specific empty-host response as an
empty contribution. Other host errors remain errors, and the combined API
still returns `ERROR_NOT_FOUND` if neither source contributed a credential.

The user reported that the remembered Microsoft account proceeded directly to
the game menu in the patched candidate. Owned credential controls verified
write/read across a Wine server restart, filtered enumeration, deletion, and
empty-filter and invalid-flag failures. The unsupported
`CRED_ENUMERATE_ALL_CREDENTIALS` mode is not claimed as fixed.

## Candidate qualification

MetalFlight 0.1.8 Beta 4 was tested against MSFS 2024 1.9.24 with genuine Steam
SDK initialization, login, and ownership. The user confirmed menu and moving
video, then a successful flight on both the retained Hybrid Metal 4 Preview7
candidate and the dev10-based candidate containing only the credential fix.
The menu/video and flight observations belong to separate runs. Flight details
were not specified. The earlier 6% initial-loading freeze on a separate
attempt remains recorded and is not classified as a crash.

The app's Swift suite includes resolution restoration and user-change cases.
The Beta 4 package validation and exact runtime inventory receipts accompany
the release staging records. The local DMG is ad-hoc signed and is not Apple
notarized; the Preview update feed and enclosure signatures use the existing
Sparkle signing key.
