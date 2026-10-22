#!/usr/bin/env python3
"""Builds the bundled space library (library/ir/*.flac) from the source recordings.

Each space is made from impulse responses in a published, licence-checked
collection, assembled into one FLAC file the plugin reads:

  2 channels: one source recorded in stereo (left, right);
  4 channels: true stereo, left-to-left, left-to-right, right-to-left, right-to-right,
              made from a source on each side of the stage.

The sources come in four shapes:

  - mono files, one per microphone (MAIR): paired into stereo, a Decca tree's centre
    folded into both sides;
  - a stereo file (OpenAIR ORTF pairs, AIR's dummy head): used as it is;
  - a first-order B-format file (OpenAIR): decoded to a coincident pair of virtual
    supercardioids 110 degrees apart;
  - two stereo files of one microphone, with the source on either side: true stereo.

The level between channels is kept, and each file is peak-normalised to -1 dBFS
before it is stored as 24-bit FLAC. Files recorded above 48 kHz are resampled to
48 kHz, and the OpenAIR and AIR spaces have the silence after their tail cut off to
keep the plugin small. Noise removal and loudness matching happen in the plugin, as
for any loaded file.

Usage: build_library.py [--download] [--analyse TOOL] [--only ID]... SOURCE_DIR

SOURCE_DIR holds the downloaded WAV files (with --download they are fetched there
first). Each --only names a space to build; without it every space is built. With
--analyse, the drenched_analyse_library tool (built with
`cmake --build build --target drenched_analyse_library`) then measures each space,
and its decay per band and room size are written into library/library.json. A
space whose entry has "fixedSize": true keeps the size the manifest gives it.
Needs numpy and ffmpeg (with soxr).
"""

import argparse
import hashlib
import io
import json
import pathlib
import subprocess
import sys
import urllib.request
import zipfile

import numpy as np

ROOT = pathlib.Path (__file__).resolve().parents[2]
MANIFEST = ROOT / "library" / "library.json"
OUT = ROOT / "library" / "ir"

MAX_RATE = 48000

# The MAIR library: St Paul's Hall, University of Huddersfield, CC BY 4.0.
MAIR = "https://raw.githubusercontent.com/APL-Huddersfield/MAIR-Library-and-Renderer/master/"

# audEERING's public copies of OpenAIR (University of York, CC BY 4.0) and of the AIR
# database (RWTH Aachen, MIT). Each file is a zip holding one WAV; the archive ids and
# MD5 sums are from each database's db.csv.
AUDB = "https://s3.dualstack.eu-north-1.amazonaws.com/audb-public/"


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


def openair (archive, md5, name):
    return f"{AUDB}openair/media/1.0.0/{archive}.zip#{name}#{md5}"


def air (archive, md5, name):
    return f"{AUDB}air/media/1.4.0/{archive}.zip#{name}#{md5}"


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

    # OpenAIR. B-format files are FuMa order (W, X, Y, Z); the level of W against
    # X, Y and Z is taken from each file's diffuse tail, since it differs between captures.
    "york-jacklyons":   {"bformat": openair ("6ccd87a8-e38a-dffa-f841-ad819f2902af", "788d24a04159d58191f62ae9b892f31b",
                                             "jack_lyons_concert_hall_university_york__b-format__rir_jack_lyons_lp1_96k.wav")},
    "spokane-club":     {"file": openair ("0baca816-1949-ec24-cd66-4183e6a6299b", "4e36d3a2ede9bdefbf079e88078e1911",
                                          "spokane_womans_club__stereo__spokane_womans_club_ir.wav")},
    "york-dixon":       {"bformat": openair ("48c5bb02-fa78-77d9-3646-7ea6409eaa88", "e8bbf8875b9bcfb858af80a65873e137",
                                             "dixon_studio_theatre_university_york__b-format__r3_rir_bformat.wav")},
    "york-minster":     {"file": openair ("40357e66-0bed-f841-fca2-ec873ade3fac", "3a18b00948747d4e65da8040e6f250aa",
                                          "york_minster__stereo__minster1_000_ortf_48k.wav")},
    "stalbans-lady":    {"file": openair ("8b50a145-8ea6-8b65-e56b-4f6e832b0011", "1e57722d6edfe8f121ab3f110adb3d45",
                                          "lady_chapel_st_albans_cathedral__stereo__stalbans_a_ortf.wav")},
    "heslington-church": {"bformat": openair ("6cbe6c5e-85ee-e804-46bb-7c75ff30271d", "94d034008a278b088bd9d99424067569",
                                              "heslington_church_vaa_group_2__b-format__impulseresponseheslingtonchurch-001.wav")},
    "york-stmargarets": {"bformat": openair ("fb3eda6c-a67e-604a-8e5c-34af524fa9e5", "c2b8604885f8604b667960150da263ba",
                                             "st_margarets_church_national_centre_early_music__b-format__r13_1st_configuration.wav")},
    "patrington-church": {"file": openair ("0f097c96-9074-a313-0203-159095186f2c", "a7afef7afa5408905153f1e90ce6ebc4",
                                           "st_patricks_church_patrington__stereo__ortf_s3r3.wav")},
    "hamilton-mausoleum": {"file": openair ("17b70ae3-ed3a-e2d6-90ec-8ad1f480345d", "b3c1c717ac2c73cd1cde000cdd6b38ec",
                                            "hamilton_mausoleum__stereo__hm2_000_ortf_48k.wav")},
    "tyndall-monument": {"file": openair ("c7e9714b-325d-bfa8-5545-b02c2d027235", "ca4f84b51afd37402bd37c07fadb7699",
                                          "tyndall_bruce_monument__stereo__tyndall_bruce_ortf.wav")},
    "york-stairwell":   {"file": openair ("2f7a08d5-1b3d-448c-4fb7-bf68edbc65d4", "920461d9539fff09b46bfffc8a869f67",
                                          "spring_lane_building_university_york__stereo__stairwell_ir_stereo_trimmed.wav")},
    "terrys-warehouse": {"file": openair ("cdd2f994-7f8a-a496-880a-ffca36c20384", "5a0d4a34464ac2fbbcc1924f9348b9f1",
                                          "terrys_factory_warehouse__stereo__terrys_warehouse_stereo.wav")},
    "terrys-typing":    {"file": openair ("176e47f1-470c-277a-ad8f-1da90f664932", "7b55773284393ebf5dd03efea312bc60",
                                          "terrys_typing_room__stereo__terrys_typing_ortf.wav")},
    "r1-reactor":       {"file": openair ("c0caa95a-76a0-e477-b6b2-1facbce061fc", "422deb49e20926f742dc1db95ea6cd5e",
                                          "r1_nuclear_reactor_hall__stereo__r1_ortf-48k.wav")},

    # AIR: the dummy head at the third position, turned so the source is 45 degrees to
    # its left (azimuth 45: the left ear hears it first and 12 dB louder) and to its
    # right (azimuth 135). The 16-bit copies end in digital silence, so their last
    # stretch is faded out.
    "aachen-aula":      {"left_file": air ("4fc1b220-9f80-13fd-afa4-9fb4bdcc92e8", "05b833aa0b0b60c7378ad082fc711c14",
                                           "air_binaural_aula_carolina_1_3_45_3.wav"),
                         "right_file": air ("13589139-f0c4-23b7-2ac0-b8a940f40060", "990c198b98f807a46051795a2a612dfd",
                                            "air_binaural_aula_carolina_1_3_135_3.wav"),
                         "fadeTail": True},
}


def urls (recipe):
    """Every source file a recipe uses."""
    found = []
    for value in recipe.values():
        if isinstance (value, str):
            found.append (value)
        elif isinstance (value, list):
            found.extend (value)
    return found


def local (source_dir, url):
    name = url.split ("#")[1] if "#" in url else url.rsplit ("/", 1)[1]
    return source_dir / name


def download (url, target):
    if "#" not in url:
        urllib.request.urlretrieve (url, target)
        return

    archive, name, md5 = url.split ("#")
    with urllib.request.urlopen (archive) as response:
        packed = zipfile.ZipFile (io.BytesIO (response.read()))
    members = [m for m in packed.namelist() if m.rsplit ("/", 1)[-1] == name]
    if len (members) != 1:
        sys.exit (f"{archive}: expected {name}, found {packed.namelist()}")
    data = packed.read (members[0])
    if hashlib.md5 (data).hexdigest() != md5:
        sys.exit (f"{name}: checksum does not match the database's")
    target.write_bytes (data)


def read (path, channels=1):
    """Decodes an audio file to float64 samples (one row per channel) and its sample rate.
    Files above 48 kHz are resampled to 48 kHz."""
    probe = subprocess.run (["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=sample_rate,channels",
                             "-of", "json", str (path)], check=True, capture_output=True, text=True)
    stream = json.loads (probe.stdout)["streams"][0]
    if int (stream["channels"]) != channels:
        sys.exit (f"{path}: expected {channels} channels")
    rate = int (stream["sample_rate"])
    resample = []
    if rate > MAX_RATE:
        resample = ["-af", "aresample=resampler=soxr:precision=28", "-ar", str (MAX_RATE)]
        rate = MAX_RATE
    raw = subprocess.run (["ffmpeg", "-v", "error", "-i", str (path), *resample, "-f", "f32le", "-"],
                          check=True, capture_output=True).stdout
    audio = np.frombuffer (raw, dtype="<f4").astype (np.float64).reshape (-1, channels).T
    return (audio[0] if channels == 1 else audio), rate


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


def decode_bformat (wxyz, rate):
    """A coincident pair of virtual supercardioids at +-55 degrees from first-order B-format
    (W, X, Y, Z, with Y to the left). W is scaled to the pressure X, Y and Z are measured
    against, using the diffuse part of the tail, where X, Y and Z together carry the
    pressure's energy whatever the format's normalisation."""
    w, x, y, z = wxyz
    onset = int (np.argmax (np.abs (w) > 0.3 * np.max (np.abs (w))))
    late = slice (onset + int (0.08 * rate), onset + int (0.4 * rate))
    gain = np.sqrt ((np.sum (x[late] ** 2) + np.sum (y[late] ** 2) + np.sum (z[late] ** 2)) / np.sum (w[late] ** 2))
    pressure = w * gain
    a = 0.37 # supercardioid
    angle = np.radians (55.0)
    left = a * pressure + (1 - a) * (np.cos (angle) * x + np.sin (angle) * y)
    right = a * pressure + (1 - a) * (np.cos (angle) * x - np.sin (angle) * y)
    return [left, right]


def tail_end (audio, rate):
    """Where the recording has sunk into its noise floor (or 100 dB below its loudest),
    plus a quarter of a second of the floor so the plugin can still measure it."""
    energy = np.mean (audio ** 2, axis=0)
    width = int (0.05 * rate)
    level = 10 * np.log10 (np.convolve (energy, np.ones (width) / width, mode="same") + 1e-30)
    floor = 10 * np.log10 (np.mean (energy[int (0.9 * len (energy)):]) + 1e-30)
    threshold = max (floor + 3.0, np.max (level) - 100.0)
    above = np.nonzero (level > threshold)[0]
    end = int (above[-1]) if len (above) else len (energy)
    return min (len (energy), end + int (0.25 * rate), int (30 * rate))


def build (space_id, recipe, source_dir):
    trimmed = False
    if "stereo" in recipe:
        channels, rate = stereo_from (recipe["stereo"], source_dir)
    elif "left" in recipe:
        (ll, lr), rate = stereo_from (recipe["left"], source_dir)
        (rl, rr), other = stereo_from (recipe["right"], source_dir)
        if other != rate:
            sys.exit (f"{space_id}: mixed sample rates")
        channels = [ll, lr, rl, rr]
    elif "file" in recipe:
        audio, rate = read (local (source_dir, recipe["file"]), 2)
        channels, trimmed = list (audio), True
    elif "bformat" in recipe:
        audio, rate = read (local (source_dir, recipe["bformat"]), 4)
        channels, trimmed = decode_bformat (audio, rate), True
    else:
        (ll, lr), rate = read (local (source_dir, recipe["left_file"]), 2)
        (rl, rr), other = read (local (source_dir, recipe["right_file"]), 2)
        if other != rate:
            sys.exit (f"{space_id}: mixed sample rates")
        channels, trimmed = [ll, lr, rl, rr], True

    n = max (len (c) for c in channels)
    audio = np.stack ([np.pad (c, (0, n - len (c))) for c in channels])

    if trimmed:
        n = tail_end (audio, rate)
        audio = audio[:, :n]

    # A short fade at the end, in case the recording stops mid-tail; a long one where
    # a 16-bit source has run out of resolution.
    fade = min (n, int ((0.3 * n) if recipe.get ("fadeTail") else (0.05 * rate)))
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
    parser.add_argument ("--only", action="append", metavar="ID", help="build only this space (repeat for more)")
    args = parser.parse_args()

    listed = {space["id"] for space in json.loads (MANIFEST.read_text (encoding="utf-8"))}
    chosen = {space_id: recipe for space_id, recipe in SOURCES.items() if not args.only or space_id in args.only}

    for space_id in args.only or []:
        if space_id not in SOURCES:
            sys.exit (f"{space_id} is not a space this script builds")

    if args.download:
        args.source_dir.mkdir (parents=True, exist_ok=True)
        for recipe in chosen.values():
            for url in urls (recipe):
                target = local (args.source_dir, url)
                if not target.exists():
                    download (url, target)

    for space_id, recipe in chosen.items():
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
