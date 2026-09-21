# Cabal Overlay

A small native addon for Cabal Online (private server, EP36) that floats
game-related information on top of your screen: server/local clocks,
countdowns to scheduled events, dungeon daily-task tracking with progress
bars, and sound alarms before events start.

**It is not a cheat.** The overlay is a completely separate program: it is
never injected into the game, never reads the game's memory, and never
sends input to it. The only things it observes are your own screen
position and key state (for its own click counter) and which window
currently has focus (to hide itself when you alt-tab away from the game).

**New here? Read the [FAQ](FAQ.md)** — it covers the Windows SmartScreen
warning, antivirus false positives, and what this tool does and does
not do.

## Features

- **Bar** (bottom-left): local clock + countdown to the next scheduled
  game events.
- **Goals panel** (right edge): tracked dungeon tasks with click counters
  and progress bars. Completed goals fade out automatically.
- **Preset task lists**: the panel switcher picks between your free-form
  "Custom" list and fixed templates from `data/task_lists.json` (a clan
  ships its own dailies/weeklies). Presets can be reordered and their
  progress is tracked, but their content is read-only. Names in the
  template resolve smartly against the catalog (short codes, partial
  names, small typos — unique matches only).
- **Panel display toggles**: quick header buttons flip rows between
  short codes and full names, and between the full list and a
  collapsed "first 3 + … N more" view for long lists.
- **First-run quick tour**: a paged wizard explains everything once
  (reopenable anytime from the "?" button in the panel).
- **Update check**: one async GitHub API call at startup; if a newer
  release exists you get a notification with a Download button.
  Downloading and installing stays manual (disable with
  `[updates] check = false`).
- **Alarms**: a warning chime N minutes before scheduled events
  (configurable per event, volume supported on Linux).
- **DG Check**: auto-increments a dungeon counter when you Ctrl+click the
  in-game dungeon-clear dialog — you calibrate the dialog position once
  in Settings.
- **Click-through by default**: the overlay never steals clicks from the
  game. A hotkey (or a desktop shortcut on Linux) toggles "interactive"
  mode to drag windows, change settings, or add tasks.
- **Game-focus aware**: the overlay hides itself when the game loses
  focus (alt-tab) and comes back when you return.
- **Settings window**: alarms, colors, opacity, positions, DG Check zone —
  everything editable in-app, no hand-editing of config files needed.

## Requirements

| Platform | Supported |
|---|---|
| Windows 10 / 11 (64-bit) | Yes (first release pending on-device test) |
| Linux, Wayland, KDE Plasma | Yes (developed and tested here) |
| Linux, Wayland, wlroots compositors (Sway, Hyprland, ...) | Should work (untested) |
| Linux, X11 session | **No** — the overlay needs the Wayland layer-shell protocol |
| Linux, GNOME (Wayland or X11) | **No** — GNOME does not implement layer-shell |
| macOS | No |

### Linux runtime dependencies

GTK 4, gtk4-layer-shell, libcanberra and libX11, plus a C++20 standard
library. Package names:

```sh
# Arch / CachyOS
sudo pacman -S gtk4 gtk4-layer-shell libcanberra libx11 tomlplusplus nlohmann-json
# Ubuntu / Debian (24.04+ / trixie+)
sudo apt install libgtk-4-1 libgtk4-layer-shell0 libcanberra0 libx11-6
# Fedora
sudo dnf install gtk4 gtk4-layer-shell libcanberra libX11
```

## Install

### Windows

Run `cabal-overlay-<version>-setup.exe` and start the overlay from the
Start Menu or the desktop shortcut. Everything (including the GTK
runtime DLLs) is bundled; no separate installs needed.

The app lives in the **system tray** (no taskbar icon — the overlay
surfaces are borderless floaters by design): right-click the tray icon
for *Open settings* and *Quit*, double-click it to open the settings.

Your settings live in `%APPDATA%\cabal-overlay\` and are kept across
updates.

### Linux

Extract the tarball and run the installer inside it:

```sh
tar -xzf cabal-overlay-<version>-linux-x86_64.tar.gz
cd cabal-overlay-<version>-linux-x86_64
./install.sh
```

This copies the app to `~/.local/share/cabal-overlay/`, puts a
`cabal-overlay` launcher in `~/.local/bin/` (make sure that directory is
in your `PATH`) and adds a menu entry. Run it with:

```sh
cabal-overlay
```

## First-run setup

1. **Interactive-mode toggle.** Default mode is click-through. To make
   the overlay clickable (drag windows, open settings):
   - **Windows**: the built-in global hotkey is `Shift+Space`
     (change it in Settings → Hotkey). Note the game cannot use the same
     combo.
   - **KDE (Wayland)**: System Settings → Apps & Windows → Shortcuts →
     Custom Shortcuts → New → Command: `gdbus call --session --dest
     dev.cabal.Overlay --object-path /dev/cabal/Overlay --method
     org.gtk.Actions.Activate toggle-interactive [] {}` and bind your
     preferred combo.
   - **Other compositors**: any tool that can invoke that D-Bus command
     works. An advanced alternative is Settings → Hotkey → evdev mode
     (built-in global hotkey) — it works on any compositor but reads the
     kernel input layer, the same permission a keylogger needs; the app
     records nothing, but use it only if you are comfortable with that.
2. **DG Check zone**: open the overlay's settings (Settings button), go to
   the **DG Check** tab and click *Capture click zone…*, then click the
   center of the dungeon-clear dialog in game (click-through is
   suspended for that one click). After that, Ctrl+clicking that dialog
   counts one run of the first tracked goal.

## Uninstall

- **Windows**: uninstall from Settings → Apps, or the Start Menu folder's
  uninstaller. Your settings in `%APPDATA%\cabal-overlay\` are removed
  too (the installer does not touch them; delete the folder manually if
  you want a clean slate).
- **Linux**: `rm -rf ~/.local/share/cabal-overlay ~/.local/bin/cabal-overlay
  ~/.local/share/applications/cabal-overlay.desktop`

## Building from source

```sh
# Linux (native)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Windows: inside an MSYS2 UCRT64 shell — also builds the installer
dist/windows/build-msys2.sh

# Linux: produce the release tarball (dist/out/)
dist/linux/package.sh
```

Cross-compiling for Windows from Linux is possible with
`cmake/mingw-w64-x86_64.toolchain.cmake` once a MinGW sysroot with the
GTK dependencies exists; the MSYS2 route above is the supported one.

## Data and configuration

- Linux config ships with the app (`config/overlay.toml` in the install
  directory); task/state data is `~/.local/share/cabal-overlay/state.json`
  and preset-list progress is `presets.json` next to it.
- Windows config: `%APPDATA%\cabal-overlay\overlay.toml` (seeded from the
  shipped defaults on first run); state lives next to it.
- Schedules (including the Guild Dungeon times) are plain
  `[[schedule]]` entries in `overlay.toml` — every clan sets its own
  without recompiling.
- Preset task lists live in `data/task_lists.json`: copy a block,
  rename it, list your dungeons (`type` daily/weekly, `goal` optional —
  the catalog's maxRuns is used when omitted).

The dungeon catalog (`data/dungeons.json`) is extracted from the clan's
Prosperity Task Tracker, so exported state backups stay interchangeable
with the web tool.
