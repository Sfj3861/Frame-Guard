# Frame-Guard — Geode for Android

Target: Geometry Dash Android 2.2081, Geode SDK 5.11.2, Android 64-bit.

## v0.5.0

- **Object bounds:** draws bounds returned by Geometry Dash's `GameObject::getObjectRect()`, transformed through world coordinates to align nested objects. Green outlines mark regular objects; red outlines mark hazards.
- **Player bounds:** independently toggleable cyan outlines for player 1 and player 2.
- **Movement trail:** a short, fading trail of observed player positions. It is not a prediction of future movement.
- **FPS diagnostic:** optional lightweight estimate based on PlayLayer update delta time.
- **In-game menu:** tap **FG** at the upper-left during a level to show controls for object bounds, player bounds, trail, and FPS.
- **Geode settings:** all four toggles are also available in the mod's settings page. In-game controls update the same Geode settings.
- Visual-only diagnostics: no changes to physics, collisions, inputs, or completion state.

## Installation

Install the generated `.geode` package with Geode for Android 64-bit. Remove/replace the previous Frame-Guard version when updating.

## Build from source

With the Geode SDK and Android toolchain installed:

    geode sdk install-binaries -p android64
    geode build -p android64

The CI workflow builds an Android64 `.geode` package. A successful compile does not replace testing inside the target game/device; UI positioning and visuals should still be checked in-game.
