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

## v0.11.0 development — bilingual controls / controles bilingües

- The floating menu now exposes the existing visual toggles, FrameGuard telemetry, HUD, informational text, CSV export, overlay opacity, safety margin, language selection, and a configurable 0–200 ms input-delay budget.
- The maximum delay budget is converted to a tick count with `floor(effectiveFPS × milliseconds / 1000)`; e.g. a 200 ms budget gives 28 ticks at 144 FPS. The HUD shows the calculated tick budget when the FPS display is enabled.
- Settings are shared between Geode settings and the floating menu. Geode setting names/descriptions include English and Spanish; the floating menu can switch its labels between English and Spanish.
- **Important limitation:** this commit adds the delay-budget setting and tick calculation/display only. It does **not** delay or reschedule gameplay inputs yet, and the current path predictor is still approximate. Do not treat the mod as an automatic safe-input system until the input scheduler and physics prediction are implemented and tested in Geometry Dash.



### v0.12.0 development — opt-in predictive Jump scheduler

- Added **Smart input delay / Espera inteligente** to both the Geode settings and the floating menu. It is **off by default** so the untested heuristic cannot unexpectedly change normal gameplay.
- When enabled, the hook considers Jump press/release edges. A lightweight forward estimate checks hazards and solid geometry with the current held state versus the requested state. The input is passed through immediately unless the requested edge predicts an earlier collision and waiting appears safer; in that case the scheduler rechecks each frame and applies the edge when the estimate no longer favors waiting or the configured tick budget is exhausted.
- The configured millisecond budget is converted using `floor(effective FPS × milliseconds / 1000)`. The scheduler only delays Jump edges, not every button or every mode-specific mechanic. It does not reproduce Geometry Dash's full physics, collision response, triggers, or portals, so its prediction can be wrong.
- The scheduler is **not validated in-game and is not a guarantee of survival or a completion tool**. It must be compiled against Geode SDK 5.11.2 and tested on Geometry Dash Android 2.2081 before being considered usable. Keep the setting disabled if inputs feel delayed or behave unexpectedly.
