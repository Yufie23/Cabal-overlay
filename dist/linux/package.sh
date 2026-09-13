#!/usr/bin/env bash
# package.sh — build a distributable Linux tarball.
#
# Produces dist/out/cabal-overlay-<version>-linux-x86_64.tar.gz
# containing the Release binary, the shipped config/data and
# install.sh (which the user runs from the extracted directory).
#
# The app resolves config/overlay.toml and data/dungeons.json relative
# to its working directory, so the tarball is meant to be installed by
# install.sh — which copies everything to ~/.local/share/cabal-overlay
# and drops a launcher in ~/.local/bin that cds there before exec.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# Parse the version from the project() block — not the first "VERSION"
# token in the file, which belongs to cmake_minimum_required.
VERSION="$(sed -n '/project(cabal-overlay/,/^)/p' "$ROOT/CMakeLists.txt" \
    | sed -n 's/.*VERSION \([0-9][0-9.]*\).*/\1/p' | head -n1)"
BUILD_DIR="$ROOT/build/dist-release"
OUT_DIR="$ROOT/dist/out"
STAGE="$OUT_DIR/stage/cabal-overlay-$VERSION-linux-x86_64"

if [ -z "$VERSION" ]; then
    echo "package.sh: could not parse VERSION from CMakeLists.txt" >&2
    exit 1
fi

echo ">> building cabal-overlay $VERSION (Release)"
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$BUILD_DIR" 2>&1

echo ">> staging $STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$BUILD_DIR/cabal-overlay" "$STAGE/"
cp -r "$ROOT/config" "$STAGE/config"
cp -r "$ROOT/data" "$STAGE/data"
cp "$ROOT/README.md" "$STAGE/README.md"
cp "$(dirname "$0")/install.sh" "$STAGE/install.sh"
cp "$(dirname "$0")/cabal-overlay.desktop" "$STAGE/cabal-overlay.desktop"
chmod +x "$STAGE/install.sh" "$STAGE/cabal-overlay"

ARCHIVE="$OUT_DIR/cabal-overlay-$VERSION-linux-x86_64.tar.gz"
rm -f "$ARCHIVE"
tar -C "$OUT_DIR/stage" -czf "$ARCHIVE" "cabal-overlay-$VERSION-linux-x86_64"
echo ">> wrote $ARCHIVE"
