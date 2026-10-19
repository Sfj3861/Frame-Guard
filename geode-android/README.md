# Frame-Guard for Geode (Android)

Frame-Guard is an Android-only Geode prototype targeting Geometry Dash **2.2081**.

## Prototype v0.2.0

- **Hitboxes ON by default:** draws engine-provided rectangles for objects in the PlayLayer object array, plus separate outlines for player 1 and player 2.
- **Trajectory ON by default:** draws a short trail of recent player positions.
- Both features can be toggled in the mod settings.
- The overlay is visual-only. It does not modify physics, collision, inputs, deaths, level completion, or verification state.

### Important accuracy note

The trajectory in this first prototype is the **observed recent path**, not a prediction of future movement. Object rectangles are debug bounds supplied by the game engine; some special collision behavior may not be represented by a simple rectangle. This implementation still needs a real Geode/NDK build and device testing. API names and rendering behavior must be confirmed against the exact Geode SDK for GD 2.2081 before calling it a verified build.

## Compatibility and native builds

The metadata targets Geometry Dash 2.2081 on Android. Android 64-bit (arm64-v8a) and 32-bit (armeabi-v7a) builds are separate. Android native libraries use .so; Windows .dll files are not used. Do not add placeholder binaries: let the Geode/NDK build generate the correct ABI artifacts.

## Build

Install the Geode SDK and Android toolchain, then run from this directory:

```sh
geode sdk install-binaries -p android64
geode build -p android64
```

For 32-bit Android:

```sh
geode sdk install-binaries -p android32
geode build -p android32
```

The generated .geode package is typically produced in the corresponding Android build directory. This repository change has **not** yet been compiled or tested on a device.

See the [official Geode mod guide](https://docs.geode-sdk.org/getting-started/create-mod/).
