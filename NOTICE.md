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

## Recorded spaces

The bundled impulse responses in `library/ir/` are recordings published by
others under licences that allow use in a commercial product. Each is credited
in the plugin (space menu, Recording credits) and in
[library/README.md](library/README.md).

| Recordings | Spaces | Licence |
| --- | --- | --- |
| [MAIR library](https://github.com/APL-Huddersfield/MAIR-Library-and-Renderer): Hyunkook Lee and Connor Millns, Applied Psychoacoustics Lab, University of Huddersfield (2017) | St Paul's Hall, Huddersfield (five microphone setups) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| [OpenAIR library](https://www.openair.hosted.york.ac.uk/) (www.openairlib.net), Audiolab, University of York, with the recordists each space's page names (listed in [library/README.md](library/README.md)); copied from audEERING's public mirror (`audb-public`, openair 1.0.0) | Jack Lyons Concert Hall, Spokane Woman's Club, Dixon Studio Theatre, York Minster, Lady Chapel St Albans, St Patrick's Patrington, St Margaret's York, Heslington Church, Spring Lane Stairwell, Hamilton Mausoleum, Tyndall Bruce Monument, Terry's Factory Warehouse, Terry's Typing Room, R1 Reactor Hall | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| [AIR database](https://www.iks.rwth-aachen.de/en/research/tools-downloads/databases/aachen-impulse-response-database/): Marco Jeub, Magnus Schäfer, Hauke Krüger, Christoph M. Nelke, Christophe Beaugeant and Peter Vary, Institute of Communication Systems, RWTH Aachen University; copied from audEERING's public mirror (`audb-public`, air 1.4) | Aula Carolina, Aachen | [MIT](https://opensource.org/licenses/MIT) |

The changes made to each recording (B-format decoding, resampling, trimming,
channel mapping, normalising) are listed in [library/README.md](library/README.md)
and under Recording credits in the plugin.

The MIT licence of the AIR database requires its copyright notice and
permission notice in copies; the installer's licence page must include them.
The permission notice is the standard MIT text below. The copyright line must
be taken word for word from the `LICENSE` file in the original AIR release
(`air_database_release_1_4.zip` from the IKS site), which the mirror does not
carry; until it is, credit it as copyright the AIR database authors, Institute
of Communication Systems, RWTH Aachen University.

> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## Images

The space photographs in `source/ui/spaces/` are placeholder renders made for
this project. They are replaced by capture photography as the space library
is recorded.

VST is a registered trademark of Steinberg Media Technologies GmbH.
