# Frame-Guard for Geode (Android)

This directory is the Android-only Geode mod target. It is intentionally separate from the standalone C++ timing simulator at the repository root.

## Scope

The first milestone is a small, visible in-game diagnostics shell that confirms the mod loads. Planned analysis features will use synthetic/replay data and clearly labelled diagnostics. This project will not suppress deaths, rewrite live gameplay inputs, or provide hidden assistance for official level verifications.

## Compatibility

The initial metadata targets Geometry Dash **2.2081 on Android**. Change `mod.json` only after testing against the exact Geometry Dash and Geode versions installed on the target device. Android 64-bit and 32-bit builds are separate.

## Build

Use the Geode CLI and Android NDK on a development computer:

```sh
geode sdk install-binaries -p android64
geode build -p android64
```

For a 32-bit Android target:

```sh
geode sdk install-binaries -p android32
geode build -p android32
```

The generated `.geode` package is typically placed in `build-android64` or `build-android32`. Install it through Geode's supported mod workflow. This repository scaffold has not yet been compiled or tested on a device.

See the [official Geode Android build guide](https://docs.geode-sdk.org/getting-started/create-mod/).
