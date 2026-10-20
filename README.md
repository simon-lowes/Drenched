# Drenched

A premium convolution and hybrid reverb for music and post-production, on
macOS and Windows.

Status: **interface.** The plugin builds and loads in VST3, AU, CLAP and
standalone form with the real interface: one set of controls, a decay-by-
frequency graph you drag, a space browser, Studio and Stage views, and
twelve user-selectable looks. The convolution engine comes next, so the
wet path is silent for now.

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
On Windows, install the WebView2 SDK package and point CMake at it:

```sh
nuget install Microsoft.Web.WebView2 -Version 1.0.2903.40 -OutputDirectory packages
cmake -S . -B build -DJUCE_WEBVIEW2_PACKAGE_LOCATION=%cd%/packages
```

## Layout

| Path | Contents |
| --- | --- |
| `source/engine/` | The DSP engine: plain C++20, no JUCE, unit tested |
| `source/plugin/` | The JUCE plugin: processor, parameters, editor |
| `source/ui/` | The interface: one web page, twelve skins, embedded in the plugin |
| `tests/` | Catch2 unit tests for the engine |
| `cmake/` | Pinned third-party dependencies |

## The interface

The editor is a JUCE web view (WKWebView on macOS, WebView2 on Windows,
WebKitGTK on Linux) showing `source/ui/index.html`, served from the plugin
binary. Nothing is fetched from the network.

- `js/engine.js` holds the shared model and the twelve compositions, one
  function per skin. Every skin draws the same state: the space, the eight
  band decays, the macros, the deep parameters, locks and A/B.
- `js/app.js` renders, handles mouse and keyboard, and talks to the plugin
  through `window.__JUCE__`: `params` and `gesture` events go to the
  processor, `state` and `meters` events come back.
- `css/skins.css` carries the engine styling and a token block per skin
  (`.s-anodised`, `.s-section`, ...). A skin is a token set plus a layout.

Click the Drenched wordmark in any look to open the picker. The chosen look
is saved with the session and becomes the default for new instances.

To preview the interface in a browser without the plugin, serve
`source/ui/` with any static file server and open `index.html#rotunda`
(or any other skin id).

## Downloads

Every push builds the plugin on macOS (Universal), Windows and Linux in
GitHub Actions. Open the latest run under **Actions** and download the
artifact for your platform. Builds are unsigned for now: on macOS, clear the
quarantine flag with `xattr -dr com.apple.quarantine <plugin>` before
loading.

See [NOTICE.md](NOTICE.md) for third-party licences.
