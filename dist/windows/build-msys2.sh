#!/usr/bin/env bash
# build-msys2.sh — build the Windows release and bundle the installer.
#
# Run this INSIDE an MSYS2 UCRT64 shell (https://www.msys2.org), from
# the repository root:
#
#     dist/windows/build-msys2.sh
#
# It installs the build dependencies with pacman, compiles the overlay
# with MinGW, copies every required MinGW DLL next to the exe (so the
# zip/installer is self-contained, no GTK installation needed on the
# target machine) and, if makensis is available, builds the installer.
set -euo pipefail

if [[ "${MSYSTEM:-}" != *"UCRT64"* && "${MSYSTEM:-}" != *"MINGW64"* ]]; then
    echo "build-msys2.sh: run this inside the MSYS2 UCRT64 shell" >&2
    echo "(Start Menu → MSYS2 → MSYS2 UCRT64; your prompt should say 'UCRT64')." >&2
    exit 1
fi

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

# Parse the version from the project() block — not the first "VERSION"
# token in the file, which belongs to cmake_minimum_required.
VERSION="$(sed -n '/project(cabal-overlay/,/^)/p' CMakeLists.txt \
    | sed -n 's/.*VERSION \([0-9][0-9.]*\).*/\1/p' | head -n1)"
BUILD_DIR="$ROOT/build-win"
STAGE="$ROOT/dist/out/windows"
INSTALLER="cabal-overlay-$VERSION-setup.exe"

echo ">> installing build dependencies (pacman)"
pacman -S --needed --noconfirm \
    mingw-w64-ucrt-x86_64-gcc \
    mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-gtk4 \
    mingw-w64-ucrt-x86_64-tomlplusplus \
    mingw-w64-ucrt-x86_64-nlohmann-json

# ntldd lists the DLL dependencies of a PE binary with full paths,
# like ldd on Linux. Optional but makes bundling reliable.
if ! command -v ntldd > /dev/null 2>&1; then
    pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-ntldd || true
fi

echo ">> building cabal-overlay $VERSION (Release)"
cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$BUILD_DIR" 2>&1

echo ">> staging $STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$BUILD_DIR/cabal-overlay.exe" "$STAGE/"
cp -r config "$STAGE/config"
cp -r data "$STAGE/data"
cp README.md "$STAGE/README.md"

# Bundle every MinGW DLL the exe (transitively) needs. Windows system
# DLLs (ntdll, kernel32, ...) live in C:\Windows and must NOT be copied.
echo ">> collecting MinGW DLLs"
if command -v ntldd > /dev/null 2>&1; then
    ntldd -R "$STAGE/cabal-overlay.exe" | grep -Ei '(ucrt64|mingw64)\\bin\\' \
        | sed -E 's/^.* => (.*) \(0x[0-9a-fA-F]+\)$/\1/' \
        | sort -u > /tmp/cabal-overlay-dlls.txt || true
else
    ldd "$STAGE/cabal-overlay.exe" | grep -E '/(ucrt64|mingw64)/bin/' \
        | awk '{print $3}' | sort -u > /tmp/cabal-overlay-dlls.txt || true
fi

COPIED=0
while IFS= read -r dll; do
    [ -n "$dll" ] || continue
    # ntldd prints Windows paths (C:\...\ucrt64\bin\...), convert and copy.
    unix="$(cygpath -u "$dll" 2>/dev/null || echo "$dll")"
    if [ -f "$unix" ]; then
        cp "$unix" "$STAGE/"
        COPIED=$((COPIED + 1))
    else
        echo "!! dependency not found, skipped: $dll" >&2
    fi
done < /tmp/cabal-overlay-dlls.txt
echo ">> bundled $COPIED DLLs"

if command -v makensis > /dev/null 2>&1; then
    echo ">> building installer"
    cd "$ROOT/dist/windows"
    makensis -DVERSION="$VERSION" -DSTAGE="../out/windows" \
        -OUTFILE="$ROOT/dist/out/$INSTALLER" cabal-overlay.nsi
    echo ">> wrote dist/out/$INSTALLER"
else
    echo ">> makensis not found — skipping the installer."
    echo "   Install it with: pacman -S nsis"
    echo "   The raw bundle (zip it and share) is in: $STAGE"
fi
