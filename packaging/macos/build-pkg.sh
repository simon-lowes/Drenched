#!/bin/bash
# Builds the macOS installer for the friends beta: one .pkg that puts the VST3, Audio Unit
# and CLAP plug-ins in /Library/Audio/Plug-Ins and the standalone app in /Applications,
# each a component the installer's "Customise" button can leave out.
#
#   packaging/macos/build-pkg.sh <folder with Drenched.vst3, .component, .clap, .app> <version> <output.pkg>
#
# The package is not signed or notarised yet (that needs an Apple Developer ID), so the
# first open needs right-click > Open, or System Settings > Privacy & Security > Open Anyway.
set -euo pipefail

SRC=$1
VERSION=$2
OUT=$3
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# One component package per format: id, the bundle, where it installs.
component()
{
    local id=$1 bundle=$2 dest=$3
    [ -e "$SRC/$bundle" ] || { echo "missing $SRC/$bundle" >&2; exit 1; }
    mkdir -p "$WORK/root-$id"
    cp -R "$SRC/$bundle" "$WORK/root-$id/"
    # Bundles must not be relocated to wherever an older copy happens to be.
    pkgbuild --analyze --root "$WORK/root-$id" "$WORK/$id.plist"
    plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/$id.plist" 2>/dev/null || true
    pkgbuild --root "$WORK/root-$id" --component-plist "$WORK/$id.plist" \
             --identifier "com.drenched.drenched.$id" --version "$VERSION" \
             --install-location "$dest" "$WORK/$id.pkg"
}

component vst3 Drenched.vst3 /Library/Audio/Plug-Ins/VST3
component au Drenched.component /Library/Audio/Plug-Ins/Components
component clap Drenched.clap /Library/Audio/Plug-Ins/CLAP
component app Drenched.app /Applications

sed "s/@VERSION@/$VERSION/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"
mkdir -p "$WORK/resources"
cp "$HERE/welcome.txt" "$WORK/resources/"

productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" \
             --resources "$WORK/resources" "$OUT"

# A quick look that every component made it in.
pkgutil --expand "$OUT" "$WORK/check"
for id in vst3 au clap app; do
    [ -d "$WORK/check/$id.pkg" ] || { echo "the installer is missing $id" >&2; exit 1; }
done
echo "Built $OUT"
