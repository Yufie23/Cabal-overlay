#!/usr/bin/env bash
# install.sh — user-side installer, shipped inside the Linux tarball.
#
# Copies the app into the per-user data directory, drops a launcher
# into ~/.local/bin and registers a desktop menu entry. Everything is
# per-user: no sudo, no system directories.
set -euo pipefail

SRC="$(cd "$(dirname "$0")" && pwd)"
DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/cabal-overlay"
BIN_DIR="$HOME/.local/bin"
APPS_DIR="$HOME/.local/share/applications"

echo ">> installing to $DATA_DIR"
mkdir -p "$DATA_DIR" "$BIN_DIR" "$APPS_DIR"
cp "$SRC/cabal-overlay" "$DATA_DIR/cabal-overlay"
cp -r "$SRC/config" "$DATA_DIR/config"
cp -r "$SRC/data" "$DATA_DIR/data"

# The app resolves config/ and data/ relative to its working directory,
# so the launcher cds into the install directory before starting it.
cat > "$BIN_DIR/cabal-overlay" <<'EOF'
#!/bin/sh
cd "${XDG_DATA_HOME:-$HOME/.local/share}/cabal-overlay" || exit 1
exec ./cabal-overlay "$@"
EOF
chmod +x "$BIN_DIR/cabal-overlay"

sed "s|@BINDIR@|$BIN_DIR|" "$SRC/cabal-overlay.desktop" \
    > "$APPS_DIR/cabal-overlay.desktop"
if command -v update-desktop-database > /dev/null 2>&1; then
    update-desktop-database "$APPS_DIR" > /dev/null 2>&1 || true
fi

cat <<EOF

Installed. Make sure $BIN_DIR is in your PATH, then run:

    cabal-overlay

See the "First-run setup" section in README.md (same directory as this
script) for the interactive-mode hotkey and the DG Check zone capture.
EOF
