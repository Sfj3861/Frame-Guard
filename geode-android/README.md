# Frame-Guard — Geode for Android

Target: Geometry Dash Android 2.2081, Geode SDK 5.11.2, Android 64-bit.

## v0.4.0

- **Object bounds:** draws the bounds returned by Geometry Dash's `GameObject::getObjectRect()`, transformed through world coordinates so nested objects align with the overlay. Green outlines mark regular objects; red outlines mark hazards.
- **Player bounds:** cyan outlines show the player object's engine-provided bounds.
- **Movement trail:** a short, fading trail of observed player positions. It is not a future trajectory prediction.
- **In-game menu:** tap **FG** at the upper-left during a level to open controls for hitboxes and trail.
- **Geode settings:** the same settings remain available in the mod's settings page. In-game buttons write directly to the same Geode settings, so both paths stay synchronized.
- Visual-only: no changes to physics, collisions, inputs, or completion state.

## Installation

Install the generated `.geode` package with Geode for Android 64-bit. Do not install the old v0.2.2 package over this version without replacing it.

## Build from source

With the Geode SDK and Android toolchain installed:

    geode sdk install-binaries -p android64
    geode build -p android64

The CI workflow builds an Android64 `.geode` package. A successful compile does not replace testing inside the target game/device; UI positioning and visuals should still be checked in-game.
