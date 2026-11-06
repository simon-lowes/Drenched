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
| [clap-helpers](https://github.com/free-audio/clap-helpers) (inside clap-juce-extensions) | CLAP format | MIT |
| [Catch2](https://github.com/catchorg/Catch2) | Unit tests (not shipped) | BSL-1.0 |
| [Playwright](https://playwright.dev) | Interface tests in a browser (not shipped) | Apache-2.0 |
| [h5py](https://www.h5py.org), [NumPy](https://numpy.org) | Writing the small SOFA test files in `tests/data` (a script run by hand; not built or shipped) | BSD-3-Clause |
| [pffft](https://github.com/marton78/pffft) | FFT for convolution | BSD-style FFTPACK licence (Julien Pommier, Hayati Ayguen, Dario Mambro, UCAR) |
| [r8brain-free-src](https://github.com/avaneev/r8brain-free-src) 6.5 | Resampling loaded impulse responses | MIT (Aleksey Vaneev) |
| Ooura FFT (inside r8brain-free-src) | FFT for the resampler | Free for any use, including commercial (Takuya Ooura) |
| [Monocypher](https://monocypher.org) 4.0.2 | Checking the signature on activation codes (Ed25519) | BSD-2-Clause or CC0-1.0, used under CC0 (Loup Vaillant, Michael Savage, Fabio Scotoni) |
| [libmysofa](https://github.com/hoene/libmysofa) 1.3.5 | Reading SOFA impulse response files | BSD-3-Clause (Symonics GmbH, Christian Hoene) |
| [miniz](https://github.com/richgel999/miniz) 3.0.2 (its inflater only) | Decompressing SOFA files | MIT (Rich Geldreich, RAD Game Tools, Valve Software) |
| [Microsoft WebView2 SDK](https://developer.microsoft.com/microsoft-edge/webview2/) | Interface web view on Windows (loader linked statically) | Microsoft WebView2 SDK licence (redistributable) |

JUCE compiles these into the plug-in from its own source tree:

| Component | Use | Licence |
| --- | --- | --- |
| [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) | VST3 format | MIT (Steinberg Media Technologies) |
| [Audio Unit SDK](https://github.com/apple/AudioUnitSDK) | Audio Unit format (macOS) | Apache-2.0 (Apple) |
| [FLAC](https://xiph.org/flac/), [Ogg, Vorbis](https://xiph.org), [Opus, opusfile, libopusenc](https://opus-codec.org) | Reading compressed impulse responses | BSD-3-Clause (Xiph.Org Foundation and contributors) |
| [zlib](https://zlib.net) | Compression | zlib (Jean-loup Gailly, Mark Adler) |
| [libpng](http://www.libpng.org/pub/png/libpng.html) | Images | libpng licence |
| [libjpeg](https://ijg.org) | Images | IJG licence (this software is based in part on the work of the Independent JPEG Group) |
| [libwebp](https://chromium.googlesource.com/webm/libwebp) | Images | BSD-3-Clause (Google) |
| [HarfBuzz](https://harfbuzz.github.io) | Text shaping | MIT ("Old MIT") |
| [SheenBidi](https://github.com/Tehreer/SheenBidi) | Bidirectional text | Apache-2.0 (Muhammad Tayyab Akram) |
| [LunaSVG](https://github.com/sammycage/lunasvg), [PlutoVG](https://github.com/sammycage/plutovg) | SVG drawing | MIT (Samuel Ugochukwu) |

The r8brain-free-src (MIT) licence requires its copyright and permission
notice in copies of the software; the installer's licence page must include
it.

The pffft licence requires its copyright notices and disclaimer to be
reproduced in documentation shipped with binaries; the installer's licence
page must include them.

The libmysofa (BSD-3-Clause) licence likewise requires its copyright notice,
conditions and disclaimer in documentation shipped with binaries; the
installer's licence page must include them, as must miniz's (MIT) notice.
libmysofa's test files are used by the tests only and not shipped.

The installers show all of these in full on their licence page, and the Windows one
installs them beside the app: `packaging/third-party-licences.txt`, built by
`tools/third_party_licences.py` from this file and each component's licence. Rebuild it
whenever this file or a dependency changes. The Windows installer is made with
[Inno Setup](https://jrsoftware.org/isinfo.php) (Inno Setup License, free for commercial
use), whose setup program ships inside it.

## Typefaces

The interface embeds these typefaces, all under the
[SIL Open Font Licence 1.1](source/ui/fonts/LICENSE-OFL.txt), as Latin
subsets from the [Fontsource](https://fontsource.org) packages. IBM Plex is the
exception: "Plex" is a Reserved Font Name, which a modified copy (such as another
party's subset) may not use, so it is IBM's own unmodified Latin-1 web subsets
(`@ibm/plex-sans` 1.1.0, `@ibm/plex-mono` 2.5.0). Keep it that way if they are updated.

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
| [OpenAIR library](https://www.openair.hosted.york.ac.uk/) (www.openairlib.net), Audiolab, University of York, with the recordists each space's page names (listed in [library/README.md](library/README.md)); copied from audEERING's public mirror (`audb-public`, openair 1.0.0) | Jack Lyons Concert Hall, Spokane Woman's Club, Dixon Studio Theatre, York Minster, Lady Chapel St Albans, St Patrick's Patrington, St Margaret's York, Heslington Church, Spring Lane Stairwell, Hamilton Mausoleum, Tyndall Bruce Monument, Terry's Factory Warehouse, Terry's Typing Room, R1 Reactor Hall, Genesis 6 Studio Live Room, Elveden Hall Smoking Room, York Guildhall Council Chamber, Innocent Railway Tunnel, Gill Heads Mine, Hoffmann Lime Kiln, Newgrange, University of York Sports Hall, Falkland Palace Royal Tennis Court, Alcuin College Courtyard, Troller's Gill | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| [AIR database](https://www.iks.rwth-aachen.de/en/research/tools-downloads/databases/aachen-impulse-response-database/): Marco Jeub, Magnus Schäfer, Hauke Krüger, Christoph M. Nelke, Christophe Beaugeant and Peter Vary, Institute of Communication Systems, RWTH Aachen University; copied from audEERING's public mirror (`audb-public`, air 1.4) | Aula Carolina, Office and Lecture Room, Aachen | [MIT](https://opensource.org/licenses/MIT) |
| [Detmold SRIR database](https://zenodo.org/records/4116247): Sebastia Vicenc Amengual Gari, Banu Sahin, Dusty Eddy and Malte Kob, Detmold University of Music (2020) | Detmold Konzerthaus (front stalls, mid hall), Brahmssaal, Sommertheater | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| [ARVERIR](https://zenodo.org/records/20098848): Federico Miotello, Gioele Greco, Paolo Ostan, Francesca Del Gaudio, Luca Comanducci, Raffaele Malvermi, Mirco Pezzoli and Fabio Antonacci, Politecnico di Milano (2026) | Arvedi Auditorium, Cremona | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| [ChurchIR](https://zenodo.org/records/14901751): Riccardo Giampiccolo, Sofia Parrinelli and Fabio Antonacci, Politecnico di Milano (2025) | Ss. Marcellino e Pietro, Cremona | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| [WDR broadcast studios compilation](https://zenodo.org/records/3930833): P. Stade, B. Bernschütz and M. Rühl, TH Köln (2012) | WDR Großer Sendesaal (ORTF, spaced omnis), WDR Kleiner Sendesaal | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |

The changes made to each recording (B-format decoding, array encoding,
resampling, trimming, channel mapping, normalising) are listed in [library/README.md](library/README.md)
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

## Scene model

`library/scene/places365.drsn` is the Places365 scene recognition network
(ResNet-18 trained on Places365, with the SUN scene-attribute weights), which
Describe a room uses to tell what kind of place a photograph shows. It is
published by MIT CSAIL at [places2.csail.mit.edu](http://places2.csail.mit.edu)
and [github.com/CSAILVision/places365](https://github.com/CSAILVision/places365)
under [CC BY](https://creativecommons.org/licenses/by/4.0/), which asks for
credit: it is credited in the plugin (Recording credits) and in
[library/scene/README.md](library/scene/README.md). Cite: Bolei Zhou, Agata
Lapedriza, Aditya Khosla, Aude Oliva and Antonio Torralba, "Places: A 10
million Image Database for Scene Recognition", IEEE Transactions on Pattern
Analysis and Machine Intelligence, 2017. The scene attributes follow Genevieve
Patterson and James Hays, "SUN Attribute Database: Discovering, Annotating,
and Recognizing Scene Attributes", CVPR 2012.

The published PyTorch weights are converted by `tools/scene/convert_places365.py`
(weights quantised to 8 bits per output channel, batch normalisation folded
in) into the plugin's own file, read by `source/engine/src/SceneNet.cpp`; no
machine-learning runtime ships. The converter needs
[PyTorch](https://pytorch.org) (BSD-3-Clause) and NumPy, run by hand and not
shipped.

## Images

The space photographs in `source/ui/spaces/` are placeholder renders made for
this project. They are replaced by capture photography as the space library
is recorded.

VST is a registered trademark of Steinberg Media Technologies GmbH.
