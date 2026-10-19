# Frame-Guard for Geode (Android)

Frame-Guard is a native Geode mod targeting Geometry Dash **2.2081 on Android**.

## v0.3.0 — engine-aligned visualizer

- Draws object bounds from Geometry Dash's oriented-box data (OBB2D), instead of treating every object as an axis-aligned rectangle.
- Uses separate colors for solid/slope objects, hazards, and interactive/special objects.
- Draws circular hazard bounds when the game provides a radius and triangle outlines for slopes.
- Draws the player's oriented bounds plus a separate inset rectangle reference.
- Shows a short history of the player's observed positions; this is a trail, **not a prediction of future movement**.
- Hitboxes and the trail are independently configurable in Geode settings.

## Compatibility and integrity

- Target: Geometry Dash Android **2.2081**, Geode SDK **5.11.2**.
- Android 64-bit builds use native ARM64 `.so` libraries inside a `.geode` package. No Windows `.dll` is required.
- Visual-only diagnostics: this mod does not change physics, collision results, inputs, deaths, or level completion.
- The source draws engine-provided OBB points in the level's object-layer coordinate space. It still requires a successful Android build and in-game testing before its rendering can be called device-verified.

## Build

From this directory, with the Geode SDK and Android toolchain installed:

    geode sdk install-binaries -p android64
    geode build -p android64

For 32-bit Android, build separately with `-p android32`.
