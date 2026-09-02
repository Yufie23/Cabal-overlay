# Project: Cabal Online overlay addon (private server, EP36)

## Hard rule: English-first

All code, identifiers, comments, UI strings, button labels, config keys, and
commit messages MUST be in English, regardless of the language used in chat
with the maintainer. The maintainer speaks Spanish; the codebase does not.

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
