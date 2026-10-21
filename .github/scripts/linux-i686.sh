#!/usr/bin/env bash
# Builds and tests the 32-bit Linux (i686) plug-ins inside an i386 Debian
# container, where every library JUCE needs exists in 32-bit form:
#   docker run --rm --platform linux/386 -v "$PWD:/src" -w /src debian:bookworm \
#     bash .github/scripts/linux-i686.sh build-i686
# pluginval only ships 64-bit, so a 32-bit copy is built from source to load
# the 32-bit plug-in (pluginval is GPL, used here as a test tool only; it is
# never linked into or shipped with Drenched).
set -euo pipefail

BUILD_DIR="${1:-build-i686}"
PLUGINVAL_TAG="v1.0.4"
PLUGINVAL_DIR="${PLUGINVAL_DIR:-pluginval-i686}"

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends build-essential cmake ninja-build git ca-certificates curl \
    pkg-config file libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
    libxcomposite-dev libxext-dev libfreetype-dev libfontconfig1-dev libcurl4-openssl-dev \
    libgtk-3-dev libwebkit2gtk-4.1-dev ladspa-sdk xvfb xauth

git config --global --add safe.directory '*'

cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure

# Hosts find the binary in Contents/<uname -m>-linux. JUCE names the folder
# i386-linux, but a 32-bit system reports i686, so the bundle carries both.
VST3="$BUILD_DIR/source/plugin/Drenched_artefacts/Release/VST3/Drenched.vst3"
rm -rf "$VST3/Contents/i686-linux"
cp -R "$VST3/Contents/i386-linux" "$VST3/Contents/i686-linux"
file "$VST3/Contents/"*/Drenched.so

if [ ! -x "$PLUGINVAL_DIR"/build/pluginval_artefacts/Release/pluginval ]; then
    rm -rf "$PLUGINVAL_DIR"
    git clone --depth 1 --branch "$PLUGINVAL_TAG" --recurse-submodules --shallow-submodules \
        https://github.com/Tracktion/pluginval.git "$PLUGINVAL_DIR"
    cmake -S "$PLUGINVAL_DIR" -B "$PLUGINVAL_DIR"/build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$PLUGINVAL_DIR"/build --parallel
fi

file "$PLUGINVAL_DIR"/build/pluginval_artefacts/Release/pluginval
# The container runs on a 64-bit kernel, which reports x86_64; setarch makes
# it report i686, as a 32-bit system would.
setarch i686 xvfb-run -a "$PLUGINVAL_DIR"/build/pluginval_artefacts/Release/pluginval --strictness-level 10 \
    --skip-gui-tests --validate-in-process --validate "$VST3"
