# Frame-Guard — Geode for Android

Target: Geometry Dash Android 2.2081, Geode SDK 5.11.2, Android 64-bit.

## v0.6.0

- **FrameGuard telemetry:** counts PlayLayer update frames and records each player input press/release with a monotonic timestamp, button index, frame index, and time since the preceding input.
- **Rolling event window:** retains the latest 16 input events in memory for timing diagnostics.
- **FrameGuard HUD:** displays current frame, input count, and the most recent input timing. Enable or disable it from Geode settings.
- **FrameGuard toggle:** the in-level FG menu can turn telemetry recording on/off; the same setting is available in Geode's mod settings.
- **Object collision overlay:** draws Geometry Dash object rectangles, circles where available, and slope outlines, with different colors for hazards and solids. This is diagnostic visualization, not a claim that every drawn outline exactly reproduces the engine's narrow-phase collision algorithm.
- **Player bounds:** independently toggleable player collision bounds.
- **Movement trail:** short fading trail of observed player positions; it is not a future trajectory prediction.
- **FPS diagnostic:** optional estimate based on PlayLayer update delta time.
- **Settings sync:** in-game menu toggles and Geode settings use the same settings values.
- **Non-invasive:** FrameGuard records and displays information only. It does not suppress deaths, alter collisions, change physics, automate inputs, or modify completion state.

## Installation

Install the generated .geode package with Geode for Android 64-bit. Remove/replace the previous Frame-Guard version when updating.

## Build from source

With the Geode SDK and Android toolchain installed:

    geode sdk install-binaries -p android64
    geode build -p android64

The CI workflow builds an Android64 .geode package. A successful compile does not replace testing inside the target game/device; input timestamps, UI positioning, and overlays still need in-game verification.
