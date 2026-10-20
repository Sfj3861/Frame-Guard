# Frame-Guard — Geode for Android

Target: Geometry Dash Android 2.2081, Geode SDK 5.11.2, Android 64-bit.

## v0.7.0

- **FrameGuard telemetry:** counts PlayLayer update frames and records each player input press/release with a monotonic timestamp, button index, frame index, and time since the preceding input.
- **Rolling event window:** retains the latest 16 input events in memory for timing diagnostics.
- **FrameGuard HUD:** displays current frame, input count, and the most recent input timing. Enable or disable it from Geode settings.
- **Floating menu:** a translucent circular FG control sits at the upper-left. Tap it or press **F** on a connected keyboard to open/close the menu. The F binding is configurable in Geode settings.
- **Collision-aware hitbox filter:** draws only objects for which Geometry Dash has initialized collision geometry (`m_orientedBox` or a collision radius), excluding decorative sprites and objects without a hitbox. The overlay is diagnostic and does not replace the engine's narrow-phase collision logic.
- **Player bounds:** independently toggleable player collision bounds.
- **Movement trail:** short fading trail of observed player positions; it is not a future trajectory prediction.
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


### v0.7.0 validation note

The collision overlay now filters out objects without initialized collision geometry. The floating FG control is translucent and the default keyboard shortcut is **F**; the keybind can be changed in Geode settings.
