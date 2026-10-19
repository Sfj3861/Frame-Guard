# Frame-Guard

**Frame-Guard** is an open-source, standalone laboratory for studying frame timing, input timestamps, and reproducible gameplay experiments.

> Status: early prototype. This project does not modify Geometry Dash, intercept live gameplay, prevent deaths, or provide hidden assistance for official level completions.

## Goals

- Record and inspect synthetic input events with frame numbers and timestamps.
- Compare event timing across simulated frame rates.
- Run repeatable, offline experiments with a configurable event window (up to 16 events).
- Export readable logs for debugging and research.
- Keep the first version dependency-free and easy to compile.

## Scope and integrity

Frame-Guard is a timing-analysis sandbox, not a clone of a private cheat. It does not inject code into a game, alter collision outcomes, suppress deaths, or conceal assisted play. Any future game integration must be transparent and restricted to permitted diagnostic/practice use.

## Build

Requires a C++17 compiler and CMake 3.16 or newer.

```sh
cmake -S . -B build
cmake --build build
./build/frame-guard
```

On Windows, run `build\Debug\frame-guard.exe` or `build\Release\frame-guard.exe`, depending on the generator.

## Prototype commands

The initial executable prints a sample timing report from synthetic events. No game installation is needed.

## Roadmap

- [x] Minimal C++17 command-line prototype
- [x] Synthetic frame/event timing report
- [ ] CSV export
- [ ] Unit tests for frame-window boundaries
- [ ] Configurable simulated refresh rates
- [ ] Documentation of measurement assumptions

## Contributing

Issues and pull requests are welcome. Please include reproduction steps and compiler/platform details for bug reports.

## License

MIT. See [LICENSE](LICENSE).
