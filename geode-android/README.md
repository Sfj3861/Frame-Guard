# Frame-Guard — Geode for Android

Target: Geometry Dash Android 2.2081, Geode SDK 5.11.2, Android 64-bit.

## v0.8.0

- **FrameGuard telemetry:** counts PlayLayer update frames and records each player input press/release with a monotonic timestamp, button index, frame index, and time since the preceding input.
- **Rolling event window:** retains the latest 16 input events in memory for timing diagnostics.
- **FrameGuard HUD:** displays current frame, input count, and the most recent input timing. Enable or disable it from Geode settings.
- **Floating menu:** a translucent circular FG control sits at the upper-left. Tap it or press **F** on a connected keyboard to open/close the menu. The F binding is configurable in Geode settings.
- **Show Hitboxes:** draws supported gameplay collision geometry for hazards, solids, slopes, portals, pads, rings, and interactables. Decorative-only objects are filtered out; circles, slope triangles, and rotated boxes are rendered separately. It is a visual diagnostic, not the engine's collision test.
- **Player hitboxes:** draws the current player collision bounds and oriented box.
- **Show Trajectory:** a fading trail of historical player collision-box snapshots, including the smaller box, for both players when active. This implementation shows the observed hitbox trail; it does not simulate future physics or predict collisions.
- **FPS diagnostic:** optional estimate based on PlayLayer update delta time.
- **Settings sync:** in-game menu toggles and Geode settings use the same settings values. The F keybind defaults to `F` and is configurable.
- **Non-invasive:** FrameGuard records and displays information only. It does not suppress deaths, alter collisions, change physics, automate inputs, or modify completion state.

## Installation

Install the generated .geode package with Geode for Android 64-bit. Remove/replace the previous Frame-Guard version when updating.

## Build from source

With the Geode SDK and Android toolchain installed:

    geode sdk install-binaries -p android64
    geode build -p android64

The CI workflow builds an Android64 .geode package. A successful compile does not replace testing inside the target game/device; input timestamps, UI positioning, transparency, keyboard behavior, and hitbox filtering still need in-game verification.


### v0.8.0 validation note

Hitbox rendering now checks supported gameplay object types and valid geometry, and the trajectory overlay draws historical player hitbox outlines instead of only connecting player-center positions. The floating FG control is translucent and the default keyboard shortcut is **F**; the keybind can be changed in Geode settings. Build and in-game validation are still required before calling this release tested.


### v0.9.0 — forward trajectory preview

The trajectory overlay now projects a short approximate future path for both players using current velocity and held input. It is a visual estimate only: future collisions, portals, rings, and trigger effects are not simulated. Hitboxes continue to use the game's collision rectangles/oriented boxes and omit decorative-only objects.


### v0.15.0 — tick-window correction

- Separates **Prediction horizon** (1–12 modeled game-update ticks) from **Safe margin** (extra visual padding around projected hitboxes). The previous build reused one setting for both, so changing the tick value unintentionally changed two unrelated behaviors.
- Adds a dedicated **Forecast** slider to the floating menu and keeps it synchronized with Geode settings; the same value now controls the visible trajectory length and timing-label horizon.
- Advances the input-window and hazard-warning estimates one modeled update per tick instead of half an update, so displayed tick counts have consistent meaning.
- Treats an empty hazard cache as a clear simulated path instead of incorrectly reporting that no input window exists.
- This is still a forward estimate, not a replay of Geometry Dash's complete physics, triggers, portals, and collision solver. Test in-game before relying on timing labels.


### v0.15.1 — flight-mode tick routing

- Routes bird flight through the continuous-input timing estimate instead of the generic gravity-only hazard lead-time estimate, matching the mode handling used by the forward trajectory preview.
- Keeps the timing display explicitly approximate; the forecast does not replay Geometry Dash's complete physics, triggers, portals, or collision solver.
