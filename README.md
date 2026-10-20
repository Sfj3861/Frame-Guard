# Frame-Guard

**Frame-Guard** is a timing and gameplay-diagnostics project with two separate targets:

- A small standalone C++ timing-analysis prototype in the repository root.
- A native **Geode mod for Geometry Dash Android 64-bit**, in `geode-android/`.

The Geode mod is an experimental diagnostic tool. It can display approximate collision geometry, player bounds and an estimated trajectory, and record recent input timing. It is not a complete reproduction of Geometry Dash physics, and no feature is guaranteed to work correctly until tested in the game.

## Android / Geode target

The current development version is listed in [`geode-android/mod.json`](geode-android/mod.json). The mod targets:

- Geometry Dash Android **2.2081**
- Geode SDK **5.11.2**
- Android **64-bit**

Features include camera/object-layer-aligned overlay rendering, hitbox outlines for supported objects, player bounds, approximate trajectory preview, a bilingual floating menu, FPS and input HUD, CSV export of the most recent 16 recorded input events, and an optional experimental Jump scheduler that is disabled by default.

The overlay and trajectory are diagnostic approximations. They do not run Geometry Dash's full collision/trigger system, and the experimental scheduler can delay Jump press/release events in ways that affect gameplay. Keep it disabled unless deliberately testing it.

### Build and downloads

- [Latest build runs and logs](https://github.com/Sfj3861/Frame-Guard/actions)
- [Published Android64 prereleases](https://github.com/Sfj3861/Frame-Guard/releases)
- [Geode mod README and installation notes](geode-android/README.md)

Each published package includes a `.sha256` checksum alongside the `.geode` file. On Linux, verify the download from the same folder with:

```sh
sha256sum -c Frame-Guard-Android64.geode.sha256
```

A successful CI compile verifies that the native library and package can be built and that the package archive is structurally valid. It does **not** replace on-device testing in the exact target game/version. Back up existing mod settings and remove the mod if the game crashes or inputs behave unexpectedly.

To run the project's offline consistency checks without an Android SDK, from the repository root run:

```sh
python3 geode-android/tools/validate_project.py
```

This checks the mod/CMake version alignment, declared settings referenced by the source, target metadata, and several important source invariants. It is a static check, not a replacement for compiling or testing in-game.

With the Geode SDK and Android toolchain installed, build from the Geode subdirectory:

```sh
cd geode-android
geode sdk install-binaries -p android64
geode build -p android64
```

## Standalone C++ prototype

The root CMake project is independent of Geometry Dash and Geode. It prints a sample report generated from synthetic frame/input events.

Requires a C++17 compiler and CMake 3.16 or newer.

```sh
cmake -S . -B build
cmake --build build
./build/frame-guard
```

On Windows, run `build\\Debug\\frame-guard.exe` or `build\\Release\\frame-guard.exe`, depending on the generator.

## Integrity and limitations

Frame-Guard is intended for transparent diagnostics and practice experimentation. It does not promise safe routes, suppress deaths, guarantee level completions, or validate the behavior of the official game engine. Any visual collision or trajectory output may be wrong in special cases such as slopes, portals, trigger-driven changes, or unusual camera transforms.

## Contributing

Issues and pull requests are welcome. Please include the exact Geometry Dash/Geode versions, device or OS, reproduction steps, and logs when reporting problems.

## License

MIT. See [LICENSE](LICENSE).
