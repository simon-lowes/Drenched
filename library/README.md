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
  instead of the measured one. The `id` is saved in sessions: never change or
  reuse one.
- `ir/<id>.flac`: the response, 24-bit. Two channels are one source recorded
  in stereo; four are true stereo (left to left, left to right, right to left,
  right to right).

## Rebuilding

    python3 tools/library/build_library.py --download --analyse \
        build/tools/library/drenched_analyse_library_artefacts/Release/drenched_analyse_library \
        /tmp/drenched-library-sources

Build the analysis tool first with
`cmake --build build --target drenched_analyse_library`. The script fetches the
source recordings, assembles each space as `build_library.py` describes, and
writes the measured decay and size into `library.json`. It needs numpy and
ffmpeg.

## Credits

| Space | Recording | Licence |
| --- | --- | --- |
| St Paul's Hall, Huddersfield (ORTF, spaced omnis, Decca tree, back of stage, in the hall) | Hyunkook Lee and Connor Millns, Applied Psychoacoustics Lab, University of Huddersfield: [MAIR library](https://github.com/APL-Huddersfield/MAIR-Library-and-Renderer) (Lee and Millns, "Microphone Array Impulse Response (MAIR) Library for Spatial Audio Research", AES 143rd Convention, 2017). Changes: mono responses combined into stereo and true-stereo files, the Decca tree's centre folded into both sides, level normalised. | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
