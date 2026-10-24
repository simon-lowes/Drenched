#!/bin/bash
# Builds packaging/user-guide.html, the user guide the installers ship, from
# docs/user-guide.md: one self-contained page (styles inlined). Needs pandoc.
# Run it after editing the guide, and commit the result.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
pandoc "$ROOT/docs/user-guide.md" --standalone --embed-resources \
       --css "$ROOT/packaging/user-guide.css" --columns=1000 \
       --toc --toc-depth=1 --shift-heading-level-by=-1 \
       -o "$ROOT/packaging/user-guide.html"
echo "Wrote packaging/user-guide.html"
