#!/usr/bin/env python3
"""Builds the bundled space library (library/ir/*.flac) from the source recordings.

Each space is a set of mono impulse responses from a published, licence-checked
collection, assembled into one FLAC file the plugin reads:

  2 channels: one source recorded in stereo (left, right);
  4 channels: true stereo, left-to-left, left-to-right, right-to-left, right-to-right,
              made from a source on each side of the stage.

The level between channels is kept, and each file is peak-normalised to -1 dBFS
before it is stored as 24-bit FLAC. Trimming, noise removal and loudness matching
happen in the plugin, as for any loaded file.

Usage: build_library.py [--download] [--analyse TOOL] SOURCE_DIR

SOURCE_DIR holds the downloaded WAV files (with --download they are fetched there
first). With --analyse, the drenched_analyse_library tool (built with
`cmake --build build --target drenched_analyse_library`) then measures each space,
and its decay per band and room size are written into library/library.json. A
space whose entry has "fixedSize": true keeps the size the manifest gives it.
Needs numpy and ffmpeg.
"""

import argparse
import json
import pathlib
import subprocess
import sys
import urllib.request

import numpy as np

ROOT = pathlib.Path (__file__).resolve().parents[2]
MANIFEST = ROOT / "library" / "library.json"
OUT = ROOT / "library" / "ir"

# The MAIR library: St Paul's Hall, University of Huddersfield, CC BY 4.0.
MAIR = "https://raw.githubusercontent.com/APL-Huddersfield/MAIR-Library-and-Renderer/master/"


def mair (folder, prefix, mic, speaker, channel):
    return f"{MAIR}{folder}/{prefix}_{mic}_{speaker}_{channel}.wav"


def pair (folder, prefix, speaker):
    """Left and right microphones of a two-channel main array, for one speaker."""
    return [mair (folder, prefix, "M01", speaker, "FL"), mair (folder, prefix, "M02", speaker, "FR")]


def decca (speaker):
    """A Decca tree's left, right and centre microphones; the centre is folded into both sides."""
    folder, prefix = "MainArrays/3ch_DeccaTree", "Main_DeccaTree"
    return [mair (folder, prefix, "M01", speaker, "FL"), mair (folder, prefix, "M02", speaker, "FR"),
            mair (folder, prefix, "M03", speaker, "FC")]


# Stage positions (MAIR guide, Fig. 2): S3 and S6 are front-row speakers either
# side of centre, S10 and S13 the far corners of the back row.
SOURCES = {
    "stpauls-ortf":   {"left": pair ("MainArrays/2ch_ORTF", "Main_ORTF", "S03"),
                       "right": pair ("MainArrays/2ch_ORTF", "Main_ORTF", "S06")},
    "stpauls-spaced": {"left": pair ("MainArrays/2ch_AB", "Main_AB", "S03"),
                       "right": pair ("MainArrays/2ch_AB", "Main_AB", "S06")},
    "stpauls-decca":  {"left": decca ("S03"), "right": decca ("S06")},
    "stpauls-back":   {"left": decca ("S10"), "right": decca ("S13")},
    "stpauls-hall":   {"stereo": [mair ("AmbienceArrays/Hamasaki06", "Amb_Hamasaki06", "M01", "S01", "FL"),
                                  mair ("AmbienceArrays/Hamasaki06", "Amb_Hamasaki06", "M02", "S01", "FR")]},
}


def local (source_dir, url):
    return source_dir / url.rsplit ("/", 1)[1]


def read (path):
    """Decodes any audio file to float32 mono samples and its sample rate."""
    probe = subprocess.run (["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=sample_rate,channels",
                             "-of", "json", str (path)], check=True, capture_output=True, text=True)
    stream = json.loads (probe.stdout)["streams"][0]
    if int (stream["channels"]) != 1:
        sys.exit (f"{path}: expected one channel")
    raw = subprocess.run (["ffmpeg", "-v", "error", "-i", str (path), "-f", "f32le", "-"], check=True, capture_output=True).stdout
    return np.frombuffer (raw, dtype="<f4").astype (np.float64), int (stream["sample_rate"])


def stereo_from (paths, source_dir):
    """Left and right from a pair, or from a Decca tree with its centre folded in at -3 dB."""
    signals, rates = zip (*(read (local (source_dir, p)) for p in paths))
    if len (set (rates)) != 1:
        sys.exit ("mixed sample rates in one space")
    n = max (len (s) for s in signals)
    signals = [np.pad (s, (0, n - len (s))) for s in signals]
    left, right = signals[0], signals[1]
    if len (signals) == 3:
        centre = signals[2] * 10 ** (-3 / 20)
        left, right = left + centre, right + centre
    return [left, right], rates[0]


def build (space_id, recipe, source_dir):
    if "stereo" in recipe:
        channels, rate = stereo_from (recipe["stereo"], source_dir)
    else:
        (ll, lr), rate = stereo_from (recipe["left"], source_dir)
        (rl, rr), other = stereo_from (recipe["right"], source_dir)
        if other != rate:
            sys.exit (f"{space_id}: mixed sample rates")
        channels = [ll, lr, rl, rr]

    n = max (len (c) for c in channels)
    audio = np.stack ([np.pad (c, (0, n - len (c))) for c in channels])

    # A short fade at the end, in case the recording stops mid-tail.
    fade = min (n, int (0.05 * rate))
    audio[:, n - fade:] *= np.linspace (1.0, 0.0, fade)

    audio *= 10 ** (-1 / 20) / max (np.max (np.abs (audio)), 1e-9)

    OUT.mkdir (parents=True, exist_ok=True)
    target = OUT / f"{space_id}.flac"
    subprocess.run (["ffmpeg", "-v", "error", "-y", "-f", "f32le", "-ar", str (rate), "-ac", str (audio.shape[0]),
                     "-ch_layout", "quad" if audio.shape[0] == 4 else "stereo",
                     "-i", "-", "-map_metadata", "-1", "-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24",
                     "-compression_level", "12", str (target)],
                    input=audio.T.astype ("<f4").tobytes(), check=True)
    print (f"{target.relative_to (ROOT)}: {audio.shape[0]} channels, {rate} Hz, {n / rate:.2f} s, {target.stat().st_size // 1024} KB")


def main():
    parser = argparse.ArgumentParser (description=__doc__.split ("\n")[0])
    parser.add_argument ("source_dir", type=pathlib.Path)
    parser.add_argument ("--download", action="store_true", help="fetch the source recordings first")
    parser.add_argument ("--analyse", type=pathlib.Path, help="the drenched_analyse_library tool")
    args = parser.parse_args()

    listed = {space["id"] for space in json.loads (MANIFEST.read_text (encoding="utf-8"))}

    if args.download:
        args.source_dir.mkdir (parents=True, exist_ok=True)
        for recipe in SOURCES.values():
            for url in sum (recipe.values(), []):
                target = local (args.source_dir, url)
                if not target.exists():
                    urllib.request.urlretrieve (url, target)

    for space_id, recipe in SOURCES.items():
        if space_id not in listed:
            sys.exit (f"{space_id} is not in {MANIFEST.relative_to (ROOT)}")
        build (space_id, recipe, args.source_dir)

    if args.analyse:
        analyse (args.analyse)


def analyse (tool):
    measured = json.loads (subprocess.run ([str (tool), str (MANIFEST.parent)], check=True, capture_output=True, text=True).stdout)
    spaces = json.loads (MANIFEST.read_text (encoding="utf-8"))
    for space in spaces:
        result = measured[space["id"]]
        space["bands"] = [round (b, 2) for b in result["bands"]]
        if not space.get ("fixedSize"):
            for key in ("W", "D", "H"):
                space[key] = round (result[key], 1)
        print (f"{space['id']}: RT60 {space['bands'][3]} s at 500 Hz")
    write_manifest (spaces)


def write_manifest (spaces):
    """One block per space and one line per field, so diffs stay readable."""
    blocks = ["  {" + ",\n   ".join (f"{json.dumps (k)}: {json.dumps (v, ensure_ascii=False)}" for k, v in space.items()) + "}"
              for space in spaces]
    MANIFEST.write_text ("[\n" + ",\n".join (blocks) + "\n]\n", encoding="utf-8")


if __name__ == "__main__":
    main()
