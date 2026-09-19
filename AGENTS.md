# Project: Cabal Online overlay addon (private server, EP36)

## Hard rule: English-first

All code, identifiers, comments, UI strings, button labels, config keys, and
commit messages MUST be in English, regardless of the language used in chat
with the maintainer. The maintainer speaks Spanish; the codebase does not.

## Hard rule: never commit without asking

The maintainer reviews every change before it enters history. NEVER run
`git commit` (or any git-mutating command) without explicit approval for that
specific commit. Present the change, let them read the code, then ask.

## Hard rule: no "trust me bro" code

No unchecked type punning. In practice:
- No C-style casts `(Type)x` and no `reinterpret_cast` in our own code.
- No blind `void*`/`gpointer` round-trips without a comment stating the
  contract, and only where a C API (GLib/GTK callbacks) forces them.
- The `GTK_LABEL(x)`-style macros ARE allowed: they are runtime-checked casts
  (GObject validates the type and warns on misuse), not blind casts.
- Prefer `std::optional`, `std::variant`, and strong types over "null means
  failure" conventions.

## Environment

- Host OS: CachyOS (Arch-based), Wayland session, KDE Plasma (KWin).
- Game runs inside Bottles (Wine prefix), client is X11 via XWayland, D3D9 via DXVK.
- XIGNCODE3 is present but its kernel driver cannot load under Wine; the
  anti-cheat is presumed degraded. Verify before any memory-reading phase.

## Architecture decisions

- The addon is a single native C++ binary, one build per OS. Nothing is
  ever injected into the game process.
- Phase 1 (current): native compact overlay (GTK4 + gtk4-layer-shell on
  Linux) powered by data extracted from the clan's HTML tracker — see
  `docs/03-native-integration.md`. The WebKitGTK embedding idea
  (`docs/02-tracker-overlay.md`) was rejected; the tracker is a data source,
  not a UI to embed.
- Platform split: `platform/<feature>/` holds one contract header plus its
  backends (e.g. `platform/overlay/overlay.h` + `overlay_wayland.cpp` /
  `overlay_windows.cpp`); CMake picks sources by `if(WIN32)` — nothing
  outside `platform/` (and tiny `#ifdef _WIN32` seams in main.cpp/state.cpp)
  knows which OS it runs on. Windows (10/11) backends:
  `platform/overlay/overlay_windows.cpp`
  (WS_POPUP + WS_EX_TOPMOST/NOACTIVATE/TRANSPARENT/LAYERED style surgery on
  the GTK HWND; click-through = WS_EX_TRANSPARENT toggle; whole-window fade
  via SetLayeredWindowAttributes — per-pixel CSS alpha has no Win32
  equivalent without owning the paint pipeline), `platform/pointer/pointer_windows.cpp`
  (GetCursorPos/GetAsyncKeyState — no X11 blind spot, works globally),
  `platform/game_watch/game_watch_windows.cpp` (EnumWindows by owner-process
  image name — basename contains "cabalmain" — with a title fallback),
  `platform/hotkey/hotkey_windows.cpp` (dual delivery: RegisterHotKey on a
  message-only HWND plus GetAsyncKeyState polling every 50 ms with a
  shared 400 ms cooldown — fullscreen Cabal grabs the keyboard with
  exclusive DirectInput, which kills the message path the same way it
  kills the Win key; the poll reads driver-level state and survives),
  `platform/tray/tray_windows.cpp` (Shell_NotifyIconW + popup menu on a
  message-only sink; the overlay surfaces have no taskbar entry by
  design, so the tray icon IS the app's lifecycle handle — Settings /
  Quit).
  Verified on the first CI Windows build (MinGW 16.2, GTK 4.24):
  `gdk_win32_surface_get_handle` is the correct GTK4 API for the HWND.
- Building for Windows: inside MSYS2 UCRT64 (`pacman -S
  mingw-w64-ucrt-x86_64-gtk4 mingw-w64-ucrt-x86_64-tomlplusplus
  mingw-w64-ucrt-x86_64-nlohmann-json`), same CMakeLists; or cross from
  Linux with `cmake/mingw-w64-x86_64.toolchain.cmake` once a MinGW
  dependency sysroot exists. Windows 10 and 11 are the same target
  (Win32 API set is identical for everything we use).
- Config/state paths: Linux keeps the repo layout (config/, XDG data dir);
  Windows uses %APPDATA%\cabal-overlay\ (config seeded from the exe's
  config\overlay.toml on first run).
- Input observation (dgcheck counter) uses the X11 core protocol over
  XWayland (`platform/pointer/pointer_x11.cpp`): polling XQueryPointer/XQueryKeymap
  from a plain X client needs zero privileges (unlike evdev hotkeys). It only
  sees the pointer while it is over X11 surfaces — which is exactly where the
  game's dungeon-end dialog lives. Synthetic-input testing on this machine is
  not possible (XTEST is a no-op under rootless XWayland; uinput devices are
  created but KWin does not route their events), so the press-edge path is
  validated by a real in-game click.
- Game-focus visibility (`platform/game_watch/game_watch_x11.cpp`) reuses the same X11
  client trick: find the game window via WM_CLASS (Wine sets it to the exe
  name) and poll XGetInputFocus. When a native Wayland window is focused the
  X focus drops to PointerRoot/None, so the overlay hides itself on alt-tab
  and reappears when the game is focused again. Hiding is debounced ~750 ms;
  interactive mode suppresses it (the game is unfocused by definition while
  the user clicks the overlay). Config: `overlay.show_only_when_game_focused`.
  HARD RULE on both backends: game-not-found reports VISIBLE — a watcher
  that cannot see the client must never make the app invisible and
  unclosable (this was the 0.1.0 Windows testers' showstopper: the
  title-match detection never found the client, and with no tray icon
  there was no way out).
- Later phases read game memory from outside via `process_vm_readv()` /
  `/proc/<pid>/mem`; offsets found with PINCE/scanmem. See `docs/01-overlay-estatico.md`
  for the original standalone-widget plan (superseded for phase 1) and the
  roadmap for phases 2-3.

## Distribution

- `dist/linux/package.sh` builds a Release tarball (binary + shipped
  config/data + install.sh) into `dist/out/`. The app resolves
  config/ and data/ relative to its working directory, so install.sh
  copies the bundle to `~/.local/share/cabal-overlay/` and installs a
  wrapper into `~/.local/bin/` that cds there before exec.
- `dist/windows/build-msys2.sh` (run inside an MSYS2 UCRT64 shell) is
  the supported Windows build: pacman deps, Release build, ntldd-based
  MinGW DLL bundling into a self-contained stage, then the NSIS
  installer (`dist/windows/cabal-overlay.nsi`) when makensis exists.
  Exercised end-to-end by the CI Windows job since: compile, ntldd
  DLL bundling and the NSIS installer all work from a clean MSYS2.
- `README.md` is the user-facing doc (requirements, install,
  first-run setup, uninstall). Keep it in sync with reality.
- `.github/workflows/build.yml`: every push to main builds the Windows
  installer (MSYS2 UCRT64, first real compile of the Win32 backends)
  and the Linux tarball as downloadable artifacts; pushing a `v*` tag
  attaches both to a GitHub Release. This is the supported way to ship
  binaries — local packaging scripts are for development.

## Windows smoke testing (no Windows PC needed)

The game runs under Bottles (UMU prefix) on this machine, so the real
Windows CI artifact can be smoke-tested under Wine locally:

1. Download the bundle: `gh run download <run-id> -n cabal-overlay-windows`
2. Stage it INSIDE the prefix (the Bottles flatpak sandbox cannot see
   host /tmp or ~): copy to `<prefix>/drive_c/overlay-smoke/`.
   Prefix: `~/.var/app/com.usebottles.bottles/data/bottles/umu/prefixes/56cf5bc8-…/`
   Runner: `~/.var/…/data/bottles/runners/soda-11.0-3/bin/wine`
3. Kill the native overlay first (same D-Bus name dev.cabal.Overlay —
   single-instance), then run:
   `GDK_BACKEND=win32 WINEPREFIX=<prefix> <runner>/wine 'C:\overlay-smoke\cabal-overlay.exe'`
   (the GDK_BACKEND pin is also hard-coded in main.cpp for _WIN32, so
   newer builds do not need it)
4. Verify VISUALLY (a running process is not enough — the invisible-
   window bugs of 0.1.1 passed that bar): `spectacle -b -n -o shot.png`.
   Wine-side window forensics: `WINEDEBUG=+win` traces every
   CreateWindow/SetWindowPos/ShowWindow — that is how the WS_VISIBLE
   strip, the custom-anchor centering and the GDK geometry stomps were
   found. KWin window list: `qdbus org.kde.KWin /KWin supportInformation`.
5. Cleanup: kill by `ps -eo pid,comm | awk '$2=="cabal-overlay.e"'`
   (NEVER `pkill -f` — the pattern matches your own shell's command
   line), remove overlay-smoke from the prefix, restart the native
   overlay.

Known Wine-only quirks (NOT bugs in our code): garbled glyphs (the
prefix lacks the fonts; real Windows uses Segoe UI) and keyboard-layout
registry warnings.

## Conventions

- The vendored HTML tracker (`Prosperity_Task_Tracker.v6.1/`) is a read-only
  upstream reference. Its data (dungeon list, schedules) is extracted into
  `data/` and `config/`; regenerate `data/dungeons.json` when the clan
  publishes a new version. State files use the tracker's JSON schema so
  export/import backups are interchangeable with the clan tool.
