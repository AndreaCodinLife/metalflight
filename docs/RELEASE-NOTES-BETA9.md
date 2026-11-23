MetalFlight 0.1.9 Beta 9 adds an opt-in keyboard test for [issue #13](https://github.com/AndreaCodinLife/metalflight/issues/13), where one keypress can enter two letters in an MSFS search field.

### Run the test

1. Update the app on the Preview channel, then install runtime `2026.11.23-dev12-keyboard-preview1` using the Runtime section. App and runtime updates are separate.
2. Start MSFS through Play and open the Free Flight departure search.
3. Return to the MetalFlight window and open Keyboard test. Choose Arm test.
4. Return to the search field and tap A once. Wait two seconds. Return to MetalFlight, open Keyboard test again if needed, and choose Copy counts.
5. Paste the counts into issue #13 and say what appeared in the field, for example `a` or `aa`.

The diagnostic runs once per game launch after you arm it. It copies event counts for the test key. There is no need to share the whole game log.

The keyboard behavior itself still needs investigation. The runtime retains the Beta 8 renderer, CPU helper and supported game hashes. Rosetta startup failures are still possible. Repeated launches and long flights remain unverified. The app is ad-hoc signed and has no Apple notarization. The full interactive app updater transition remains unverified.
