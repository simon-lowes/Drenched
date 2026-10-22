# Drenched

A premium convolution and hybrid reverb for music and post-production, on
macOS and Windows.

Status: **engine.** The plugin builds and loads in VST3, AU, CLAP and
standalone form with the real interface (one set of controls, a decay-by-
frequency graph you drag, a space browser, Studio and Stage views, twelve
user-selectable looks) and a working zero-latency convolution
engine, true stereo and surround up to 9.1.6 (each room spreads over the
surround and height speakers with decorrelated tails), with a living tail: a feedback delay network fitted to each room
that powers Freeze (Crossover sets where the frozen room stops holding, so
the bass keeps fading). Living tail hands the late reverberation over to it
at the same level, keeping the early reflections, and Movement lets it
drift slowly so it breathes and never repeats. Reverse plays the room
backwards, Gate cuts it off after a set time, and Attack fades it in.
The direct sound (the first arrival, before any reflection) has its own
level, delay and colour. You can load your own impulse response (WAV, AIFF
or FLAC; mono, stereo or four-channel true stereo): it is resampled to the
session rate, its decay is measured into the graph, and dragging the graph
reshapes it. Or capture a space yourself: Capture a space plays a sine
sweep from the left speaker (or left, then right, for true stereo) while
recording the track's input, deconvolves it with the speaker's distortion
removed, saves the result in Documents/Drenched/Captures and loads it.
The space list starts with recorded spaces bundled in the plugin
(`library/`, licence-checked recordings published by others), followed by
modelled spaces whose responses are synthesised from their dimensions and
the decay graph.

Licensing: a 14-day trial, then a licence number (sold through Paddle)
activates up to three computers. Activation is online once; the signed code
that comes back is checked offline from then on, and a computer with no
internet can be activated from another device. After the trial without a
licence the reverb is muted and the dry signal still plays. The activation
server is in `server/` (see its README).

Builds are beta builds for now (`DRENCHED_BETA`, on by default): no trial
and no licence check, and a Beta label in the footer. They never touch the
licence file, so a tester's trial starts with the first release. Release
builds configure with `-DDRENCHED_BETA=OFF`.

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

For 32-bit Windows, configure with `-A Win32`. 32-bit Linux builds inside
an i386 Debian container; see `.github/scripts/linux-i686.sh`.

## Layout

| Path | Contents |
| --- | --- |
| `source/engine/` | The DSP engine: plain C++20, no JUCE, unit tested |
| `source/plugin/` | The JUCE plugin: processor, parameters, editor |
| `source/ui/` | The interface: one web page, twelve skins, embedded in the plugin |
| `library/` | The recorded spaces bundled in the plugin, their credits and how they are built |
| `source/licence/` | Licence numbers and activation codes: plain C++20, no JUCE |
| `server/` | The activation server (Cloudflare Worker) and its tests |
| `tests/` | Catch2 unit tests for the engine and licences |
| `cmake/` | Pinned third-party dependencies |

## The interface

The editor is a JUCE web view (WKWebView on macOS, WebView2 on Windows,
WebKitGTK on Linux) showing `source/ui/index.html`, served from the plugin
binary. Nothing is fetched from the network; only activating a licence
talks to the activation server.

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

Every push builds the plugin on macOS (Universal), Windows (32 and 64-bit)
and Linux (32 and 64-bit) in GitHub Actions. Open the latest run under
**Actions** and download the artifact for your platform: `Drenched-macOS`,
`Drenched-Windows` (one VST3 for 32 and 64-bit hosts, plus both CLAPs and
apps), `Drenched-Linux-x64` or `Drenched-Linux-i686`. See
[32-bit and 64-bit](docs/32-bit-and-64-bit.md) for where to install each,
what 32-bit can't do, and bridging. Builds are unsigned for now: on macOS, clear the
quarantine flag with `xattr -dr com.apple.quarantine <plugin>` before
loading.

See [NOTICE.md](NOTICE.md) for third-party licences.
