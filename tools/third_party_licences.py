#!/usr/bin/env python3
"""Assembles packaging/third-party-licences.txt, the licence texts the installers ship.

Several dependencies (pffft, r8brain-free-src, libmysofa, miniz, CLAP) require their
copyright notice, conditions and disclaimer in documentation shipped with binaries, and
the embedded typefaces require the SIL Open Font Licence. This joins NOTICE.md (the list of
components and recording credits) with each licence's full text, taken from the sources
CMake fetched. Run it after a configure, and commit the result:

    python3 tools/third_party_licences.py [build directory]
"""

import sys
from pathlib import Path

ROOT = Path (__file__).resolve().parent.parent
BUILD = Path (sys.argv[1]) if len (sys.argv) > 1 else ROOT / "build"
DEPS = BUILD / "_deps"
OUT = ROOT / "packaging" / "third-party-licences.txt"

JUCE = DEPS / "juce-src" / "modules"

# What ships in the plug-in, and where its licence text lives. Catch2 (tests only) and
# JUCE itself (used under a commercial licence) are left out, but the third-party code
# JUCE compiles into the plug-in is not: format SDKs, codecs, image and text libraries.
# Some are only in one platform's build (the Audio Unit SDK on macOS); one file covers all.
SHIPPED = [
    ("CLAP", DEPS / "clap_juce_extensions-src" / "clap-libs" / "clap" / "LICENSE"),
    ("clap-helpers", DEPS / "clap_juce_extensions-src" / "clap-libs" / "clap-helpers" / "LICENSE"),
    ("clap-juce-extensions", DEPS / "clap_juce_extensions-src" / "LICENSE.md"),
    ("VST3 SDK (in JUCE)", JUCE / "juce_audio_processors_headless" / "format_types" / "VST3_SDK" / "LICENSE.txt"),
    ("Audio Unit SDK (in JUCE, macOS)", JUCE / "juce_audio_plugin_client" / "AU" / "AudioUnitSDK" / "LICENSE.txt"),
    ("FLAC (in JUCE)", JUCE / "juce_audio_formats" / "codecs" / "flac" / "Flac Licence.txt"),
    ("Ogg (in JUCE)", JUCE / "juce_audio_formats" / "codecs" / "ogg" / "COPYING"),
    ("Vorbis (in JUCE)", JUCE / "juce_audio_formats" / "codecs" / "vorbis" / "COPYING"),
    ("Opus (in JUCE)", JUCE / "juce_audio_formats" / "codecs" / "opus" / "opus" / "COPYING"),
    ("opusfile (in JUCE)", JUCE / "juce_audio_formats" / "codecs" / "opus" / "opusfile" / "COPYING"),
    ("libopusenc (in JUCE)", JUCE / "juce_audio_formats" / "codecs" / "opus" / "libopusenc" / "COPYING"),
    ("zlib (in JUCE)", JUCE / "juce_core" / "zip" / "zlib" / "LICENSE"),
    ("libpng (in JUCE)", JUCE / "juce_graphics" / "image_formats" / "pnglib" / "LICENSE"),
    ("libjpeg, Independent JPEG Group (in JUCE)", JUCE / "juce_graphics" / "image_formats" / "jpglib" / "README"),
    ("libwebp (in JUCE)", JUCE / "juce_graphics" / "image_formats" / "libwebp" / "COPYING"),
    ("HarfBuzz (in JUCE)", JUCE / "juce_graphics" / "fonts" / "harfbuzz" / "COPYING"),
    ("SheenBidi (in JUCE)", JUCE / "juce_graphics" / "unicode" / "sheenbidi" / "LICENSE"),
    ("LunaSVG (in JUCE)", JUCE / "juce_graphics" / "drawables" / "lunasvg" / "LICENSE"),
    ("PlutoVG (in JUCE)", JUCE / "juce_graphics" / "drawables" / "lunasvg" / "plutovg" / "LICENSE"),
    ("pffft", DEPS / "pffft-src" / "LICENSE.txt"),
    ("r8brain-free-src", DEPS / "r8brain-src" / "LICENSE"),
    ("Monocypher", DEPS / "monocypher-src" / "LICENCE.md"),
    ("libmysofa", DEPS / "mysofa-src" / "LICENSE"),
    ("miniz", DEPS / "miniz-src" / "LICENSE"),
    ("Microsoft WebView2 SDK (Windows)", ROOT / "packaging" / "licences" / "WebView2-SDK-LICENSE.txt"),
    ("Places365 category and attribute lists (in the scene model)", ROOT / "library" / "scene" / "LICENSE-places365-lists.txt"),
    ("Typefaces (SIL Open Font Licence 1.1)", ROOT / "source" / "ui" / "fonts" / "LICENSE-OFL.txt"),
]

RULE = "=" * 78


def main():
    missing = [str (p) for _, p in SHIPPED if not p.is_file()]

    if missing:
        sys.exit ("Configure first; missing:\n  " + "\n  ".join (missing))

    parts = ["Drenched: third-party software and recordings", RULE, "",
             (ROOT / "NOTICE.md").read_text (encoding="utf-8").strip(), ""]

    for name, path in SHIPPED:
        parts += [RULE, name, RULE, "", path.read_text (encoding="utf-8", errors="replace").strip(), ""]

    # With a byte-order mark, or Inno Setup's licence page reads it as ANSI (Schäfer, Köln).
    OUT.write_text ("\n".join (parts) + "\n", encoding="utf-8-sig", newline="\n")
    print ("Wrote", OUT.relative_to (ROOT))


if __name__ == "__main__":
    main()
