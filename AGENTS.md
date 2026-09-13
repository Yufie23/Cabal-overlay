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

- The addon is a single native Linux C++ binary. Nothing is ever injected
  into the game process.
- Phase 1 (current): native compact overlay (GTK4 + gtk4-layer-shell) powered
  by data extracted from the clan's HTML tracker — see
  `docs/03-native-integration.md`. The WebKitGTK embedding idea
  (`docs/02-tracker-overlay.md`) was rejected; the tracker is a data source,
  not a UI to embed.
- Input observation (dgcheck counter) uses the X11 core protocol over
  XWayland (`platform/pointer_x11.cpp`): polling XQueryPointer/XQueryKeymap
  from a plain X client needs zero privileges (unlike evdev hotkeys). It only
  sees the pointer while it is over X11 surfaces — which is exactly where the
  game's dungeon-end dialog lives. Synthetic-input testing on this machine is
  not possible (XTEST is a no-op under rootless XWayland; uinput devices are
  created but KWin does not route their events), so the press-edge path is
  validated by a real in-game click.
- Game-focus visibility (`platform/game_watch_x11.cpp`) reuses the same X11
  client trick: find the game window via WM_CLASS (Wine sets it to the exe
  name) and poll XGetInputFocus. When a native Wayland window is focused the
  X focus drops to PointerRoot/None, so the overlay hides itself on alt-tab
  and reappears when the game is focused again. Hiding is debounced ~750 ms;
  interactive mode suppresses it (the game is unfocused by definition while
  the user clicks the overlay). Config: `overlay.show_only_when_game_focused`.
- Later phases read game memory from outside via `process_vm_readv()` /
  `/proc/<pid>/mem`; offsets found with PINCE/scanmem. See `docs/01-overlay-estatico.md`
  for the original standalone-widget plan (superseded for phase 1) and the
  roadmap for phases 2-3.

## Conventions

- The vendored HTML tracker (`Prosperity_Task_Tracker.v6.1/`) is a read-only
  upstream reference. Its data (dungeon list, schedules) is extracted into
  `data/` and `config/`; regenerate `data/dungeons.json` when the clan
  publishes a new version. State files use the tracker's JSON schema so
  export/import backups are interchangeable with the clan tool.
