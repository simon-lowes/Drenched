# Drenched

A premium convolution and hybrid reverb for music and post-production, on
macOS and Windows.

Status: **engine.** The plugin builds and loads in VST3, AU, CLAP and
standalone form with the real interface (one set of controls, a decay-by-
frequency graph you drag, a space browser, Studio and Stage views, twelve
user-selectable looks) and a working zero-latency convolution
engine, true stereo and surround up to 9.1.6 (each room spreads over the
surround and height speakers with decorrelated tails, and Reverb speakers
sets the reverb's level and delay in each speaker, with a meter for each), with a living tail: a feedback delay network fitted to each room
that powers Freeze (Crossover sets where the frozen room stops holding, so
the bass keeps fading). Living tail hands the late reverberation over to it
at the same level, keeping the early reflections, and Movement lets it
drift slowly so it breathes and never repeats. Reverse plays the room
backwards, Gate cuts it off after a set time, and Attack fades it in.
The direct sound (the first arrival, before any reflection) has its own
level, delay and colour, and Cross-feed sets how much each side of the
input reaches the other side of the room (at 0 the left and right get
separate reverbs). Listener position and Source position place you and the sound across
the room (drag them on the room map, or automate them). Cloud size lets the reverb follow where the sound is
panned: lower it and a voice panned to one side, or to a rear speaker,
keeps its reverb gathered around it. Unlink the decay graph and the left and right
sides each get their own decay curve (surround speakers follow their side, centre
speakers the mean of the two). Low CPU runs the convolution on about a third of the
processing by playing everything 21 ms late, which the DAW makes up. You can load your own impulse response (WAV, AIFF,
FLAC or SOFA; mono, stereo, four-channel true stereo, surround up to 9.1.6, or B-format): it is resampled to the
session rate, its decay is measured into the graph, dragging the graph
reshapes it, and Size makes the room bigger or smaller by moving its
reflections later or sooner while keeping its decay and tone. A B-format
(first-order ambisonic) file is played through virtual microphones: on
stereo a pair of hypercardioids aimed 60 degrees left and right, which keeps
the room wide, and on surround one aimed at each speaker. Drenched knows a
B-format file by its ambisonic metadata, the .amb type, or "AmbiX",
"B-format" or "FuMa" in its name (higher-order AmbiX files play at first
order); a four-channel file without any of these is true stereo. A SOFA file of room
measurements plays a pair of its sources about 30 degrees either side of the
listener as true stereo, heard by its leftmost and rightmost receivers (a
dummy head's ears, say); without such a pair it plays the source closest to
straight ahead. Ambisonic SOFA files play as B-format. A surround
file plays its own channels on a surround track: each speaker plays the
channel recorded nearest it, channels with no speaker nearby join the
nearest one, and speakers the recording lacks get reverb made from its
fold-down; on a stereo track it plays folded down. The speaker order comes
from the WAV channel mask, or the usual WAV order for the channel count. Or capture a space yourself: Capture a space plays a sine
sweep from the left speaker (or left, then right, for true stereo) while
recording the track's input, deconvolves it with the speaker's distortion
removed, saves the result in Documents/Drenched/Captures and loads it.
With no speakers to hand, choose Hand clap: Drenched listens while you clap,
keeps the loudest clap that didn't clip, and uses the room's ring-out after it.
To match a room you only have a recording of, choose Match a room and pick
the recording (a voice, a band, a drum kit, anything with pauses in it):
Drenched hears how fast the room dies away each time the sound stops, sets
the decay graph to match, and switches to the closest space.
For post, Scenes (in the space menu) keep every setting and the space under
a name, up to 32 per session: recall one there, or automate the Scene
parameter to change rooms on the cut. Scenes follow automation within a
twentieth of a second of real time, so bounce in real time when a scene
changes the space. Scene Off leaves the settings alone.
Wet EQ (next to Freeze, or in the space menu) shapes the reverb alone with
six bands: a low shelf, four bells and a high shelf, after the wet cuts. It
starts flat, and a band at 0 dB does nothing.
Waterfall (beside Wet EQ, or in the space menu) shows the response you are
hearing: energy over time in third-octave bands, the broadband decay with
the RT60 measured per octave, and a live spectrum of the reverb, which also
sits behind the curve in the Wet EQ window.
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

To hunt for races or memory errors, configure a separate Debug or RelWithDebInfo
build with `-DDRENCHED_SANITIZE=thread` (or `address,undefined`) and run the tests.

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

## User guide

The [user guide](docs/user-guide.md) covers installing, every control and window, and how
to report a bug. Both installers include it as a web page (`packaging/user-guide.html`,
rebuilt from the Markdown with `tools/user_guide_html.sh`, which needs pandoc).

## Downloads

Every push builds the plugin on macOS (Universal), Windows (32 and 64-bit)
and Linux (32 and 64-bit) in GitHub Actions. Open the latest run under
**Actions** and download the installer for your platform: `Drenched-macOS-Installer`
(a .pkg with the Audio Unit, VST3, CLAP and the app, each optional) or
`Drenched-Windows-Installer` (puts the 64-bit and 32-bit plug-ins where each kind of
host looks, and the app). The installers are unsigned for now: on macOS, right-click
the .pkg and choose Open; on Windows, choose More info > Run anyway.
Both show the third-party licences (`packaging/third-party-licences.txt`); after changing
`NOTICE.md` or a dependency, rebuild that file with `python3 tools/third_party_licences.py`
(after a CMake configure, which fetches the licence texts) and commit it.

The plug-ins on their own are there too: `Drenched-macOS`,
`Drenched-Windows` (one VST3 for 32 and 64-bit hosts, plus both CLAPs and
apps), `Drenched-Linux-x64` or `Drenched-Linux-i686`. See
[32-bit and 64-bit](docs/32-bit-and-64-bit.md) for where to install each,
what 32-bit can't do, and bridging. Builds are unsigned for now: on macOS, clear the
quarantine flag with `xattr -dr com.apple.quarantine <plugin>` before
loading.

See [NOTICE.md](NOTICE.md) for third-party licences.
