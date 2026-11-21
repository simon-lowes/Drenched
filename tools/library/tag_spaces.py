#!/usr/bin/env python3
"""Search words for every space, worked out when the library is built (optional, uses Jev).

Run beside build_library.py:  TYPESAFE_API_KEY=... python3 tools/library/tag_spaces.py
With no TYPESAFE_API_KEY it does nothing and library.json is left as it is. Any failed call
leaves that entry's words unchanged. Jev judges; this code decides: words with p >= KEEP go
into the entry's `words` field, words between REVIEW and KEEP go to a review file for S.
The plugin never calls Jev: the words ship as static JSON. Entry ids are never touched.
"""
import json, math, os, sys, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "library" / "library.json"
REVIEW_FILE = ROOT / "library" / "search-words-review.json"
API = "https://api.typesafe.ai/v1/systemone"
KEEP, KEEP_MATERIAL, REVIEW = 0.75, 0.85, 0.4

MATERIALS = ["stone", "wood", "brick", "concrete", "glass", "tile", "metal", "carpet", "plaster"]
CHARACTER = ["intimate", "vast", "airy", "dark", "bright", "warm", "boomy", "tight", "lush", "eerie",
             "natural", "dry", "wet", "spacious", "small", "huge", "cold", "smooth"]
USES = ["vocals", "choir", "strings", "piano", "drums", "snare", "guitar", "dialogue", "adr", "foley",
        "podcast", "orchestra", "organ", "ambient"]
SYNONYMS = ["cathedral", "basilica", "abbey", "cloister", "church", "warehouse", "stairwell", "bunker",
            "bathroom", "kitchen", "studio", "auditorium", "theatre", "library", "corridor", "tunnel"]


def sound_words(e):
    """Code, not Jev, turns the numbers into words (Jev is weak with numbers)."""
    b = [x for x in (e.get("bands") or []) if x]
    out = []
    if b:
        mid = sum(b[2:6]) / max(1, len(b[2:6]))
        out.append("decay %.1f s, %s" % (mid, "very long" if mid > 3 else "long" if mid > 1.6 else "medium" if mid > 0.7 else "short"))
        if len(b) >= 8 and mid:
            tilt = (b[6] + b[7]) / 2 / mid
            out.append("darker than most" if tilt < 0.45 else "brighter than most" if tilt > 0.7 else "even top end")
    try:
        v = float(e["W"]) * float(e["D"]) * float(e["H"])
        out.append("about %d cubic metres, %s" % (round(v, -1), "huge" if v > 20000 else "big" if v > 2000 else "medium" if v > 200 else "small"))
    except (KeyError, TypeError, ValueError):
        pass
    return "; ".join(out)


def questions():
    q = {}
    for w in MATERIALS:
        q["mat_" + w] = {"type": "noul", "instructions": "A place of the type in `place`, like the one named in `name`, typically has %s surfaces." % w}
    for w in CHARACTER + USES + SYNONYMS:
        q["w_" + w] = {"type": "noul", "instructions": "Would someone searching a reverb library for \"%s\" want this space?" % w}
    return q


def ask(entry, key):
    state = {k: entry.get(k, "") for k in ("name", "place", "rec", "kind")}
    state["sound"] = sound_words(entry)
    body = json.dumps({"model": os.environ.get("JEV_MODEL") or "jev-latest", "state": state, "questions": questions()}).encode()
    req = urllib.request.Request(API, data=body, headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def decide(answers):
    keep, review = [], []
    for name, a in (answers or {}).items():
        p = (a or {}).get("noul") if isinstance(a, dict) else None
        if not isinstance(p, (int, float)):
            continue
        word = name.split("_", 1)[1]
        bar = KEEP_MATERIAL if name.startswith("mat_") else KEEP
        if p >= bar:
            keep.append(word)
        elif p >= REVIEW:
            review.append([word, round(p, 2)])
    return sorted(keep), review


def main():
    key = os.environ.get("TYPESAFE_API_KEY")
    if not key:
        print("TYPESAFE_API_KEY not set: search words left as they are.")
        return 0
    lib = json.loads(MANIFEST.read_text())
    review = {}
    for e in lib:
        try:
            res = ask(e, key)
        except Exception as err:  # any failure keeps today's entry
            print("skip %s: %s" % (e.get("id"), err), file=sys.stderr)
            continue
        keep, mid = decide(res.get("answers", res))
        e["words"] = " ".join(keep)
        if mid:
            review[e["id"]] = mid
    MANIFEST.write_text(json.dumps(lib, indent=1, ensure_ascii=False) + "\n")
    REVIEW_FILE.write_text(json.dumps(review, indent=1) + "\n")
    print("wrote words for %d spaces; review band in %s" % (len(lib), REVIEW_FILE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
