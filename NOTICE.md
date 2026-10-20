# Third-party software

Drenched is proprietary software. All rights reserved.

It is built with the following third-party components. Each is licensed for
use in closed-source commercial software. GPL-licensed code must never be
added to this repository.

| Component | Use | Licence |
| --- | --- | --- |
| [JUCE](https://juce.com) 9 | Plugin wrappers, GUI, platform layer | JUCE Starter or paid JUCE licence (not the AGPL option) |
| [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) | CLAP format | MIT |
| [CLAP](https://github.com/free-audio/clap) | CLAP format | MIT |
| [Catch2](https://github.com/catchorg/Catch2) | Unit tests (not shipped) | BSL-1.0 |
| [pffft](https://github.com/marton78/pffft) | FFT for convolution | BSD-style FFTPACK licence (Julien Pommier, Hayati Ayguen, Dario Mambro, UCAR) |
| [r8brain-free-src](https://github.com/avaneev/r8brain-free-src) 6.5 | Resampling loaded impulse responses | MIT (Aleksey Vaneev) |
| Ooura FFT (inside r8brain-free-src) | FFT for the resampler | Free for any use, including commercial (Takuya Ooura) |
| [Monocypher](https://monocypher.org) 4.0.2 | Checking the signature on activation codes (Ed25519) | BSD-2-Clause or CC0-1.0, used under CC0 (Loup Vaillant, Michael Savage, Fabio Scotoni) |
| [Microsoft WebView2 SDK](https://developer.microsoft.com/microsoft-edge/webview2/) | Interface web view on Windows (loader linked statically) | Microsoft WebView2 SDK licence (redistributable) |

The r8brain-free-src (MIT) licence requires its copyright and permission
notice in copies of the software; the installer's licence page must include
it.

The pffft licence requires its copyright notices and disclaimer to be
reproduced in documentation shipped with binaries; the installer's licence
page must include them.

## Typefaces

The interface embeds these typefaces, all under the
[SIL Open Font Licence 1.1](source/ui/fonts/LICENSE-OFL.txt), as Latin
subsets from the [Fontsource](https://fontsource.org) packages.

| Typeface | Author |
| --- | --- |
| Fraunces | Undercase Type |
| Inter | Rasmus Andersson |
| IBM Plex Sans, IBM Plex Mono | IBM |
| Instrument Sans | Rodrigo Fuenzalida, Jordan Egstad |
| Geist, Geist Mono | Vercel |
| Space Grotesk | Florian Karsten |
| JetBrains Mono | JetBrains |
| Hanken Grotesk | Alfredo Marco Pradil, Hanken Design Co. |

## Images

The space photographs in `source/ui/spaces/` are placeholder renders made for
this project. They are replaced by capture photography as the space library
is recorded.

VST is a registered trademark of Steinberg Media Technologies GmbH.
