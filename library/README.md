# Recorded spaces

The spaces bundled in the plugin. Each is an impulse response from a published
collection whose licence allows use in a product that is sold (CC0, CC BY, or
MIT/BSD-style). Share-alike, non-commercial and unlicensed recordings are left
out, and so is anything made for or taken from another reverb product.

- `library.json`: one entry per space, in menu order. The interface shows
  `name`, `no`, `place`, `rec` and `date`; `credit`, `licence` and `source`
  appear under Recording credits; `bands` (RT60 per octave, 63 Hz to 8 kHz)
  and `W`, `D`, `H` (metres) are measured from the recording. `kind` and
  `img` pick the plan drawing and picture. `fixedSize` keeps the size given
  instead of the measured one (the real dimensions, or a shoebox of the
  volume the source publishes). The `id` is saved in sessions: never change or
  reuse one.
- `ir/<id>.flac`: the response, 24-bit, at 44.1 or 48 kHz. Two channels are one
  source recorded in stereo; four are true stereo (left to left, left to right,
  right to left, right to right).

## Rebuilding

    python3 tools/library/build_library.py --download --analyse \
        build/tools/library/drenched_analyse_library_artefacts/Release/drenched_analyse_library \
        /tmp/drenched-library-sources

Build the analysis tool first with
`cmake --build build --target drenched_analyse_library`. The script fetches the
source recordings, assembles each space as `build_library.py` describes, and
writes the measured decay and size into `library.json`. It needs numpy and
ffmpeg (built with soxr). `--only <id>` (repeatable) rebuilds just those spaces.

The St Paul's Hall files come from the MAIR repository on GitHub. The OpenAIR
and AIR files come from audEERING's public copies of those databases
(`audb-public` on Amazon S3: `openair` 1.0.0 and `air` 1.4.x), because the
original servers were not reachable from the build machine; each file is
checked against the MD5 sum in the database's own file table. The Detmold,
ARVERIR, ChurchIR and WDR files come from their Zenodo records: the script
reads only the files it needs out of each zip, using HTTP range requests, and
checks each against the MD5 sum recorded when it was first fetched.

What the script does to each kind of source:

- OpenAIR stereo files: used as recorded.
- OpenAIR B-format files (W, X, Y, Z): decoded to a coincident pair of virtual
  supercardioids 110 degrees apart. W is matched to X, Y and Z from the diffuse
  part of each file's tail, since the captures differ in how W is scaled.
- OpenAIR B-format pairs: Genesis 6 Studio (the ride's and the hi-hat's
  positions in the drum kit) and York Guildhall (sources 2 and 3, either side
  of the council pews, heard from receiver 1 in the public pews) are each two
  B-format responses, decoded as above, with the source to the microphone's
  left and to its right (from the direct sound's direction), made into one
  true-stereo file.
- OpenAIR open-air and underground spaces: where a site has a response made
  with one loudspeaker and one made with an array of two or four loudspeakers
  pointing different ways, the array's is used.
- AIR: two dummy-head responses from one position, with the head turned so the
  source is 45 degrees to its left and to its right, make one true-stereo file.
  The 16-bit copies end in digital silence, so the last 30 % of the kept tail is
  faded out. The office and lecture room are one dummy-head response each,
  with the source straight ahead, faded the same way.
- Detmold: dummy-head responses. The Konzerthaus spaces (set C, measured with
  the hall's enhancement system off) pair two sources of the loudspeaker
  orchestra into true stereo: S2 and S3 (4 m apart) for the front stalls (seat
  126), S1 and S4 (8 m apart) for mid hall (seat 285). Brahmssaal (row 9) and
  Sommertheater (row 6) use the one stage source. The set A responses with the
  enhancement system's artificial reverberation switched on are left out.
- ARVERIR: each 8-capsule response is encoded to first-order B-format by a
  least-squares fit over its cardioid capsules, at the positions in
  `pos_receivers.csv`, then decoded as for OpenAIR, facing the centre of the
  stage. Seat 10 of row 4, with the first violin's source (S1) on the left and
  the cello's (S4) on the right, makes one true-stereo file.
- ChurchIR: four omni responses in row 5, at the left and right microphone
  positions (about 5 m apart, from the arrival times) with the left and right
  sources, make one true-stereo file.
- WDR: the ORTF and large AB pairs, with the omni source at the centre of the
  stage, used as recorded.
- Files above 48 kHz are resampled to 48 kHz (soxr). Every space except St
  Paul's Hall is cut a quarter of a second after its tail sinks into the noise
  floor (or 100 dB below its loudest). Elveden Hall's Smoking Room and Gill
  Heads Mine, whose noise floors wander too much for that test, are cut at 1.5
  and 2.5 s, where their tails have reached the floor. Every file is
  peak-normalised to -1 dBFS.

## Credits

| Space | Recording | Licence |
| --- | --- | --- |
| St Paul's Hall, Huddersfield (ORTF, spaced omnis, Decca tree, back of stage, in the hall) | Hyunkook Lee and Connor Millns, Applied Psychoacoustics Lab, University of Huddersfield: [MAIR library](https://github.com/APL-Huddersfield/MAIR-Library-and-Renderer) (Lee and Millns, "Microphone Array Impulse Response (MAIR) Library for Spatial Audio Research", AES 143rd Convention, 2017). Changes: mono responses combined into stereo and true-stereo files, the Decca tree's centre folded into both sides, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Jack Lyons Concert Hall, University of York | Alex Duffell, Aishwarya Sridhar and Zhong Li, Audiolab, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=571) (www.openairlib.net). Changes: B-format decoded to stereo, resampled from 96 to 48 kHz, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Spokane Woman's Club | James Cadwallader: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=659). Changes: silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Dixon Studio Theatre, University of York | Ben Lavin, Darren Robinson and Ya-Hsin Chou, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=452). Changes: B-format decoded to stereo, resampled from 96 to 48 kHz, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| York Minster | [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=797) (www.openairlib.net), University of York. Changes: silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Lady Chapel, St Albans Cathedral | Marcin Gorzel, Gavin Kearney, Aglaia Foteinou, Sorrel Hoare and Simon Shelley, Audiolab, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=595). Changes: silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| St Patrick's Church, Patrington | Aglaia Foteinou and Damian T. Murphy, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=722). Changes: resampled from 96 to 48 kHz, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| St Margaret's Church, York (National Centre for Early Music) | Aglaia Foteinou and Simon Shelley, Audiolab, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=696). Changes: B-format decoded to stereo, resampled from 96 to 48 kHz, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Heslington Church, York | Marc Ciufo Green, Michael Lovedee-Turner and Yuchen Gong, AudioLab, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=508). Changes: B-format decoded to stereo, resampled from 96 to 48 kHz, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Aula Carolina, Aachen | Marco Jeub, Magnus Schäfer, Hauke Krüger, Christoph M. Nelke, Christophe Beaugeant and Peter Vary, Institute of Communication Systems, RWTH Aachen University: [AIR database](https://www.iks.rwth-aachen.de/en/research/tools-downloads/databases/aachen-impulse-response-database/) (Jeub, Schäfer and Vary, "A Binaural Room Impulse Response Database for the Evaluation of Dereverberation Algorithms", DSP 2009). Changes: two dummy-head responses combined into a true-stereo file, the end of the tail faded where the 16-bit copy runs out of resolution, level normalised. | [MIT](https://opensource.org/licenses/MIT) (see [NOTICE.md](../NOTICE.md)) |
| Spring Lane Stairwell, University of York | Gavin Davies, Ignacio Gomez-Lanzaco, James Geary and Thomas Wood, Audiolab, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=670). Changes: silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Genesis 6 Studio, Live Room, University of York | Daniel Johnston and Panagiotis Kompotis, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=483). Changes: B-format decoded to stereo, two responses (sources on the left and right) combined into a true-stereo file, resampled from 96 to 48 kHz, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Office and Lecture Room, Aachen | Marco Jeub, Magnus Schäfer, Hauke Krüger, Christoph M. Nelke, Christophe Beaugeant and Peter Vary, Institute of Communication Systems, RWTH Aachen University: [AIR database](https://www.iks.rwth-aachen.de/en/research/tools-downloads/databases/aachen-impulse-response-database/) (Jeub, Schäfer and Vary, 2009). Changes: silence after the tail removed, the end of the tail faded where the 16-bit copy runs out of resolution, level normalised. | [MIT](https://opensource.org/licenses/MIT) (see [NOTICE.md](../NOTICE.md)) |
| Elveden Hall, Smoking Room, Suffolk | Matt Rogalsky: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=459). Changes: silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| York Guildhall, Council Chamber | Benjamin Tsui, Oliver McIntyre and Elizabeth Wilson, Audiolab, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=790). Changes: B-format decoded to stereo, two responses (sources on the left and right) combined into a true-stereo file, resampled from 96 to 48 kHz, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Hamilton Mausoleum | Damian T. Murphy, Audiolab, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=502). Changes: silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Tyndall Bruce Monument | Damian T. Murphy, Audiolab, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=764). Changes: silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Terry's Factory Warehouse, York | Damian T. Murphy, Audiolab, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=735). Changes: level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Terry's Typing Room, York | Damian T. Murphy, Audiolab, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=740). Changes: resampled from 96 to 48 kHz, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| R1 Reactor Hall | [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=626) (www.openairlib.net). Changes: silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Innocent Railway Tunnel, Edinburgh | Andrew Chadwick and Simon Shelley: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=525). Changes: B-format decoded to stereo, resampled from 96 to 48 kHz, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Gill Heads Mine, Yorkshire Dales | Andrew Chadwick and Simon Shelley: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=494). Changes: B-format decoded to stereo, resampled from 96 to 48 kHz, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Hoffmann Lime Kiln, Langcliffe | Jack Smith, Juan García, Hashim Riaz and Pau Rosell, Audiolab, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=518). Changes: B-format decoded to stereo, resampled from 96 to 48 kHz, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Newgrange, County Meath | Francis Stevens: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=611). Changes: B-format decoded to stereo, resampled from 96 to 48 kHz, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Sports Hall, University of York | Aglaia Foteinou and Simon Shelley, Audiolab, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=665). Changes: resampled from 96 to 48 kHz, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Falkland Palace Royal Tennis Court | Damian T. Murphy, Audiolab, University of York: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=476). Changes: silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Alcuin College Courtyard, University of York | Francis Stevens and Simon Shelley: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=416). Changes: B-format decoded to stereo, resampled from 96 to 48 kHz, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Troller's Gill, Yorkshire Dales | Andrew Chadwick and Simon Shelley: [OpenAIR library](https://www.openair.hosted.york.ac.uk/?page_id=745). Changes: B-format decoded to stereo, resampled from 96 to 48 kHz, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Detmold Konzerthaus (front stalls, mid hall) | Sebastia Vicenc Amengual Gari, Banu Sahin, Dusty Eddy and Malte Kob, Detmold University of Music: [Detmold SRIR database](https://zenodo.org/records/4116247) (Amengual Gari, Sahin, Eddy and Kob, "Open Database of Spatial Room Impulse Responses at Detmold University of Music", AES 149th Convention, 2020). Changes: two dummy-head responses (a source on the left and on the right) combined into a true-stereo file, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Brahmssaal, Detmold | As above: [Detmold SRIR database](https://zenodo.org/records/4116247). Changes: silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Detmold Sommertheater | As above: [Detmold SRIR database](https://zenodo.org/records/4116247). Changes: silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Arvedi Auditorium, Cremona | Federico Miotello, Gioele Greco, Paolo Ostan, Francesca Del Gaudio, Luca Comanducci, Raffaele Malvermi, Mirco Pezzoli and Fabio Antonacci, Politecnico di Milano: [ARVERIR dataset](https://zenodo.org/records/20098848) ("The Sound of the Violin's Home: A Higher-Order Room Impulse Response Dataset of the Arvedi Auditorium in Cremona", IWAENC 2026). Changes: capsule responses encoded to first-order B-format and decoded to a pair of virtual supercardioids, two sources (left and right) combined into a true-stereo file, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Ss. Marcellino e Pietro, Cremona | Riccardo Giampiccolo, Sofia Parrinelli and Fabio Antonacci, Politecnico di Milano: [ChurchIR dataset](https://zenodo.org/records/14901751) (2025). Changes: four omni responses (left and right microphone positions, sources on the left and right) combined into a true-stereo file, silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| WDR Großer Sendesaal (ORTF, spaced omnis) and Kleiner Sendesaal, Cologne | P. Stade, B. Bernschütz and M. Rühl, TH Köln: [WDR broadcast studios compilation](https://zenodo.org/records/3930833) (Stade, Bernschütz and Rühl, "A Spatial Audio Impulse Response Compilation Captured at the WDR Broadcast Studios", 27th Tonmeistertagung, 2012). Changes: silence after the tail removed, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
