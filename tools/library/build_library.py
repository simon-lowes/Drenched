#!/usr/bin/env python3
"""Builds the bundled space library (library/ir/*.flac) from the source recordings.

Each space is made from impulse responses in a published, licence-checked
collection, assembled into one FLAC file the plugin reads:

  2 channels: one source recorded in stereo (left, right);
  4 channels: true stereo, left-to-left, left-to-right, right-to-left, right-to-right,
              made from a source on each side of the stage.

The sources come in six shapes:

  - mono files, one per microphone (MAIR, ChurchIR): paired into stereo, a Decca
    tree's centre folded into both sides;
  - a stereo file (OpenAIR ORTF pairs, dummy heads, WDR's stereo pairs): used as it is;
  - a first-order B-format file (OpenAIR): decoded to a coincident pair of virtual
    supercardioids 110 degrees apart;
  - two B-format files of one microphone, with the source on either side: each
    decoded as above, and made into true stereo;
  - two stereo files of one microphone, with the source on either side: true stereo;
  - two files of an array of cardioid capsules (ARVERIR), with the source on either
    side: each encoded to first-order B-format from the capsule positions, decoded
    as above, and made into true stereo.

The level between channels is kept, and each file is peak-normalised to -1 dBFS
before it is stored as 24-bit FLAC. Files recorded above 48 kHz are resampled to
48 kHz, and every space except St Paul's Hall has the silence after its tail cut off
to keep the plugin small. Noise removal and loudness matching happen in the plugin,
as for any loaded file.

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
import csv
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

# Zenodo records, each one zip read in place over HTTP ranges, so only the files used
# are fetched. The MD5 sums are of each file as first fetched for the library.
ZENODO = "https://zenodo.org/api/records/"


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


def zenodo (record, archive, name, md5):
    """A file inside a zip in a Zenodo record. A name with "!" is a file in a zip inside that zip."""
    return f"{ZENODO}{record}/files/{archive}/content#{name}#{md5}"


# Detmold SRIR database (Detmold University of Music, CC BY 4.0), dummy-head responses.
def detmold (name, md5):
    return zenodo (4116247, "DetmoldSRIR_v01.zip", name, md5)


# ARVERIR (Politecnico di Milano, CC BY 4.0): 8-capsule arrays in the Arvedi Auditorium.
def arvedi (name, md5):
    return zenodo (20098848, "arvedi_auditorium_dataset.zip", f"arvedi_auditorium_dataset/{name}", md5)


# ChurchIR (Politecnico di Milano, CC BY 4.0): omni responses in Ss. Marcellino e Pietro.
def church (name, md5):
    return zenodo (14901751, "ChurchIR.zip", f"OMNI/{name}", md5)


# WDR broadcast studios (TH Köln, CC BY 4.0): the stereo microphone responses.
def wdr (name, md5):
    return zenodo (3930833, "miro_wav_format_data.zip", f"miro_wav_format_data/WDR_MIC.zip!{name}", md5)


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

    # Rooms. Genesis 6: the microphone faces the drum kit, measured from where each
    # drum stands; the ride (to the microphone's left, from the direct sound's
    # direction) and the hi-hat (to its right) make true stereo. Guildhall: receiver 1,
    # in the public pews, with sources 2 and 3 on either side of the council pews
    # (left and right, from the direct sound's direction).
    "york-genesis-studio": {"left_bformat": openair ("91e300e3-a5e8-72bf-3564-1dbc3e286675", "466495b6262ff0b9ef4e1fe98ec59a7c",
                                                     "genesis_6_studio_live_room_drum_set__b-format__ride_ir.wav"),
                            "right_bformat": openair ("58722005-382f-ea68-065d-b48afd504967", "6c24e5929af2be69e5e0ecc30235cfd2",
                                                      "genesis_6_studio_live_room_drum_set__b-format__hh_ir.wav")},
    "elveden-smoking-room": {"file": openair ("32dd5b26-8785-cfc5-0875-a85cc8f4f8ba", "17ca5ee736c2812a6c432e562d0287c0",
                                              "elveden_hall_suffolk_england__stereo__18a_smoking_room.wav"),
                             "seconds": 1.5},
    "york-guildhall":   {"left_bformat": openair ("bb62ac94-2cbd-7d5e-3247-503c92d0373b", "2c870d83f16a25e77139f51828e87a54",
                                                  "york_guildhall_council_chamber__b-format__councilchamber_s2_r1_ir_1_96000.wav"),
                         "right_bformat": openair ("9d7aa0c8-d4cb-1792-8804-1f2e5378bc9b", "1036cdb1d51d5f610389d318599d278c",
                                                   "york_guildhall_council_chamber__b-format__councilchamber_s3_r1_ir_1_96000.wav")},

    # Odd spaces and open air. Where OpenAIR has a response made with one loudspeaker
    # and one made with an array of two or four pointing different ways (to come
    # closer to an omni source), the array's is used.
    "edinburgh-tunnel": {"bformat": openair ("3deb645b-c18d-609a-302e-34bf9118fbd6", "4e28663cd85e48d9f9eeee3e9d4aa73b",
                                             "innocent_railway_tunnel__b-format__middle_tunnel_4way_bformat.wav")},
    "gillheads-mine":   {"bformat": openair ("2b7a3aa5-26e0-59ee-4c5b-9ba395cd25d3", "4aff9f69f241b6c3e91dcb67a86e0125",
                                             "gill_heads_mine__b-format__mine_site2_2way_bformat.wav"),
                         "seconds": 2.5},
    "hoffmann-kiln":    {"bformat": openair ("64adfe47-790d-11fd-8452-e5c284e8516b", "90e1197aca514fc9f79ededb77e90e6d",
                                             "hoffmann_lime_kiln_langcliffeuk__b-format__ir_p2.wav")},
    "newgrange":        {"bformat": openair ("96812303-b2d1-bca0-f07b-20223e1c768c", "dab6841cd1991af3f0e5430321adba42",
                                             "newgrange__b-format__newgrange_s1r1.wav")},
    "york-sports-hall": {"file": openair ("512cb966-2ba4-9c45-bfcf-3e4563ee9b3c", "2b725b94e9e1f9a6cc782a7f2d9bcb9b",
                                          "sports_centre_university_york__stereo__sportscentre_ortf.wav")},
    "falkland-tennis-court": {"file": openair ("a803eb1d-2c85-d831-65a5-1ae6222e66bb", "99633af8a825809da96a308745bbc154",
                                               "falkland_palace_royal_tennis_court__stereo__falkland_tennis_court_ortf.wav")},
    "york-alcuin-courtyard": {"bformat": openair ("b4ad5e35-4737-157f-a6f9-349c03295c44", "ff194ab86994cc1f374f143d4df6b13a",
                                                  "alcuin_college_university_york__b-format__alcuin_s2r1_bformat.wav")},
    "trollers-gill":    {"bformat": openair ("a85a85b5-3b73-0c18-a35f-d10c7725e2ee", "76522c55005d74d18ace256993bfaf2a",
                                             "trollers_gill__b-format__dales_site2_4way_bformat.wav")},

    # AIR: the dummy head at the third position, turned so the source is 45 degrees to
    # its left (azimuth 45: the left ear hears it first and 12 dB louder) and to its
    # right (azimuth 135). The 16-bit copies end in digital silence, so their last
    # stretch is faded out.
    "aachen-aula":      {"left_file": air ("4fc1b220-9f80-13fd-afa4-9fb4bdcc92e8", "05b833aa0b0b60c7378ad082fc711c14",
                                           "air_binaural_aula_carolina_1_3_45_3.wav"),
                         "right_file": air ("13589139-f0c4-23b7-2ac0-b8a940f40060", "990c198b98f807a46051795a2a612dfd",
                                            "air_binaural_aula_carolina_1_3_135_3.wav"),
                         "fadeTail": True},

    # AIR office and lecture room: the dummy head with the source straight ahead
    # (azimuth 90), 3 m away in the office and 7.1 m in the lecture room.
    "aachen-office":    {"file": air ("0481688a-18a1-d6b9-ea88-056d539ce8c3", "2e3d11c458cef4fcfabd238c3b289e02",
                                      "air_binaural_office_1_3.wav"),
                         "fadeTail": True},
    "aachen-lecture":   {"file": air ("7e0ce2f1-3ef0-c82a-bf45-c8f8ec06537e", "f8852be5af780f85f4035ef2c04a89ca",
                                      "air_binaural_lecture_1_4.wav"),
                         "fadeTail": True},

    # Detmold. Konzerthaus (set C): a dummy head in the hall with a pair of the
    # loudspeaker orchestra's sources, the front pair 4 m apart or the outer pair 8 m
    # apart (set C has no enhancement system switched in). Brahmssaal and
    # Sommertheater (set A): one source on stage, the dummy head in the audience.
    "detmold-konzerthaus": {"left_file": detmold ("SetC_DenseKH_LSOrchestra/Data/DummyHead/S2R126.wav", "37e1178b94209c258ae803249bc99811"),
                            "right_file": detmold ("SetC_DenseKH_LSOrchestra/Data/DummyHead/S3R126.wav", "4976eaad6462b22435e68fdeecdac6c5")},
    "detmold-konzerthaus-mid": {"left_file": detmold ("SetC_DenseKH_LSOrchestra/Data/DummyHead/S1R285.wav", "5134548da027abb5d76a246857f630cc"),
                                "right_file": detmold ("SetC_DenseKH_LSOrchestra/Data/DummyHead/S4R285.wav", "dc61af0e8d39809412184e70c7deb905")},
    "detmold-brahmssaal": {"file": detmold ("SetA_SingleSources/Data/Brahmssaal/DummyHead/C1S1R5.wav", "9951f172c262a13bad84e3c201d12c1e")},
    "detmold-sommertheater": {"file": detmold ("SetA_SingleSources/Data/SommerTheater/DummyHead/C1S1R3.wav", "380e8df6fa08501667a2ceca3bda9b19")},

    # ARVERIR: row 4, seat 10, with the first violin's loudspeaker (S1, on the
    # listener's left) and the cello's (S4, on the right). The pair faces the omni
    # source S0 at the centre of the stage, which is the origin of the positions.
    "arvedi-auditorium": {"left_array": arvedi ("rirs/rir-S1-A410.wav", "1d7091c2153f6ffba00c484f373b131c"),
                          "right_array": arvedi ("rirs/rir-S4-A410.wav", "b0150eeb1f6f649571180b3318266573"),
                          "positions": arvedi ("pos_receivers.csv", "ebd6388f15a811fa4feefc66e982ebb6"),
                          "receiver": "A410"},

    # ChurchIR: the omni at the left and right positions of row 5 (about 5 m apart,
    # from the arrival times), with the left and right sources.
    "cremona-church": {"left": [church ("SL_ML_OMNI_5.wav", "0e22d406ec2d0415453a650dc0760282"),
                                church ("SL_MR_OMNI_5.wav", "cb0469cb94339ddb40540318756baa8f")],
                       "right": [church ("SR_ML_OMNI_5.wav", "1af1a1b428c0d0cfae916675532a13e3"),
                                 church ("SR_MR_OMNI_5.wav", "3780a31540bb6ccd68432592aeade93c")],
                       "trim": True},

    # WDR: the stereo pairs in the large (CHL) and small (CHS) studios, with the
    # omni source at the centre of the stage (SBC).
    "wdr-large-ortf": {"file": wdr ("CHL_MIC_ORTF_MK4-SBC.wav", "53ceb6d244cfff8b6edb8afe786d6ef8")},
    "wdr-large-ab":   {"file": wdr ("CHL_MIC_LargeAB_MK2-SBC.wav", "5fb7189719e0c6f13f8525e851d2e6a3")},
    "wdr-small-ortf": {"file": wdr ("CHS_MIC_ORTF_MK4-SBC.wav", "2a8f8a3b428ba382105dbe57579d0ae2")},
}


def urls (recipe):
    """Every source file a recipe uses."""
    found = []
    for value in recipe.values():
        if isinstance (value, str) and "://" in value:
            found.append (value)
        elif isinstance (value, list):
            found.extend (value)
    return found


def local (source_dir, url):
    name = url.split ("#")[1].replace ("!", "/").replace ("/", "_") if "#" in url else url.rsplit ("/", 1)[1]
    return source_dir / name


class RemoteFile (io.RawIOBase):
    """A file on a web server, read with HTTP range requests."""

    def __init__ (self, url):
        self.url, self.pos = url, 0
        with urllib.request.urlopen (urllib.request.Request (url, headers={"Range": "bytes=0-0"})) as response:
            self.size = int (response.headers["Content-Range"].split ("/")[1])

    def readable (self):
        return True

    def seekable (self):
        return True

    def tell (self):
        return self.pos

    def seek (self, offset, whence=io.SEEK_SET):
        self.pos = {io.SEEK_SET: 0, io.SEEK_CUR: self.pos, io.SEEK_END: self.size}[whence] + offset
        return self.pos

    def readinto (self, buffer):
        end = min (self.size, self.pos + len (buffer))
        if end <= self.pos:
            return 0
        request = urllib.request.Request (self.url, headers={"Range": f"bytes={self.pos}-{end - 1}"})
        with urllib.request.urlopen (request) as response:
            data = response.read()
        buffer[:len (data)] = data
        self.pos += len (data)
        return len (data)


def unzip (packed, name, archive):
    """One member of a zip, found by its path or, for the audb copies, its file name.
    Some zip64 archives made on macOS (WDR's) give offsets past 4 GB without their top
    bits; the member's header is then a multiple of 4 GB before where it says."""
    members = [m for m in packed.infolist() if name in (m.filename, m.filename.rsplit ("/", 1)[-1])]
    if len (members) != 1:
        sys.exit (f"{archive}: expected one {name}, found {len (members)}")
    info = members[0]
    for wraps in range (4):
        offset = info.header_offset - wraps * 2 ** 32
        if offset < 0:
            break
        packed.fp.seek (offset)
        if packed.fp.read (4) == b"PK\x03\x04":
            info.header_offset = offset
            return packed.read (info)
    sys.exit (f"{archive}: cannot find {name}")


def download (url, target):
    if "#" not in url:
        urllib.request.urlretrieve (url, target)
        return

    archive, name, md5 = url.split ("#")
    outer, *inner = name.split ("!")
    data = unzip (zipfile.ZipFile (io.BufferedReader (RemoteFile (archive), buffer_size=1 << 20)), outer, archive)
    for member in inner:
        data = unzip (zipfile.ZipFile (io.BytesIO (data)), member, outer)
    if hashlib.md5 (data).hexdigest() != md5:
        sys.exit (f"{name}: checksum does not match the one recorded")
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
    return supercardioids (w * gain, x, y)


def decode_array (capsules, positions, receiver):
    """The supercardioid pair from an array of cardioid capsules (ARVERIR). Each capsule
    hears half the pressure plus half the particle velocity along the direction it
    faces, out from the array's centre; a least-squares fit over the capsules gives the
    pressure and velocity, turned so X faces the origin (the centre of the stage) and
    Y is to the listener's left."""
    rows = {row["mic"]: row for row in csv.DictReader (io.StringIO (positions.read_text (encoding="utf-8")), delimiter=";")}
    row = rows[receiver]
    centre = np.array ([float (row[f"C_{k}"]) for k in "xyz"])
    facing = np.array ([float (row[f"x{i}_{k}"]) for i in range (1, len (capsules) + 1) for k in "xyz"]).reshape (-1, 3) - centre
    facing /= np.linalg.norm (facing, axis=1, keepdims=True)
    w, *velocity = np.linalg.pinv (0.5 * np.hstack ([np.ones ((len (facing), 1)), facing])) @ capsules
    front = -centre * [1, 1, 0]
    front /= np.linalg.norm (front)
    left = np.cross ([0, 0, 1], front)
    return supercardioids (w, front @ velocity, left @ velocity)


def supercardioids (pressure, x, y):
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
        channels, trimmed = [ll, lr, rl, rr], recipe.get ("trim", False)
    elif "left_array" in recipe:
        positions = local (source_dir, recipe["positions"])
        (left, rate), (right, other) = (read (local (source_dir, recipe[side]), 8) for side in ("left_array", "right_array"))
        if other != rate:
            sys.exit (f"{space_id}: mixed sample rates")
        channels = decode_array (left, positions, recipe["receiver"]) + decode_array (right, positions, recipe["receiver"])
        trimmed = True
    elif "file" in recipe:
        audio, rate = read (local (source_dir, recipe["file"]), 2)
        channels, trimmed = list (audio), True
    elif "bformat" in recipe:
        audio, rate = read (local (source_dir, recipe["bformat"]), 4)
        channels, trimmed = decode_bformat (audio, rate), True
    elif "left_bformat" in recipe:
        (left, rate), (right, other) = (read (local (source_dir, recipe[side]), 4) for side in ("left_bformat", "right_bformat"))
        if other != rate:
            sys.exit (f"{space_id}: mixed sample rates")
        channels, trimmed = decode_bformat (left, rate) + decode_bformat (right, rate), True
    else:
        (ll, lr), rate = read (local (source_dir, recipe["left_file"]), 2)
        (rl, rr), other = read (local (source_dir, recipe["right_file"]), 2)
        if other != rate:
            sys.exit (f"{space_id}: mixed sample rates")
        channels, trimmed = [ll, lr, rl, rr], True

    n = max (len (c) for c in channels)
    audio = np.stack ([np.pad (c, (0, n - len (c))) for c in channels])

    if trimmed:
        # A noise floor that wanders by more than the margin keeps the whole file;
        # "seconds" then gives where the tail has been seen to reach the floor.
        n = min (tail_end (audio, rate), int (recipe.get ("seconds", 30) * rate))
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
