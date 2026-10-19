# Drenched

A premium convolution and hybrid reverb for music and post-production, on
macOS and Windows.

Status: **foundations.** The plugin builds and loads in VST3, AU, CLAP and
standalone form with a placeholder interface. The convolution engine comes
next.

## Building

Requirements: CMake 3.25+, a C++20 compiler (Xcode 15+, Visual Studio 2022,
or GCC 13 / Clang 16 on Linux), and Ninja (optional). Dependencies are
downloaded on the first configure.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Plugins land in `build/source/plugin/Drenched_artefacts/Release/`.

On Linux, install JUCE's dependencies first (see `.github/workflows/build.yml`).

## Layout

| Path | Contents |
| --- | --- |
| `source/engine/` | The DSP engine: plain C++20, no JUCE, unit tested |
| `source/plugin/` | The JUCE plugin: processor, parameters, editor |
| `tests/` | Catch2 unit tests for the engine |
| `cmake/` | Pinned third-party dependencies |

## Downloads

Every push builds the plugin on macOS (Universal), Windows and Linux in
GitHub Actions. Open the latest run under **Actions** and download the
artifact for your platform. Builds are unsigned for now: on macOS, clear the
quarantine flag with `xattr -dr com.apple.quarantine <plugin>` before
loading.

See [NOTICE.md](NOTICE.md) for third-party licences.
