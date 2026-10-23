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

# What ships in the plug-in, and where its licence text lives. Catch2 (tests only) and
# JUCE (used under a commercial licence) are left out.
SHIPPED = [
    ("CLAP", DEPS / "clap_juce_extensions-src" / "clap-libs" / "clap" / "LICENSE"),
    ("clap-juce-extensions", DEPS / "clap_juce_extensions-src" / "LICENSE.md"),
    ("pffft", DEPS / "pffft-src" / "LICENSE.txt"),
    ("r8brain-free-src", DEPS / "r8brain-src" / "LICENSE"),
    ("Monocypher", DEPS / "monocypher-src" / "LICENCE.md"),
    ("libmysofa", DEPS / "mysofa-src" / "LICENSE"),
    ("miniz", DEPS / "miniz-src" / "LICENSE"),
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
