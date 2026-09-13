// ─────────────────────────────────────────────────────────────
// platform/game_watch.h — "is the game the foreground window?"
//
// The overlay floats on the compositor's OVERLAY layer, above
// EVERYTHING — including the browser when the user alt-tabs away
// from the game. That is by design for the game, annoying for
// everything else. This module answers "should the overlay be
// visible at all right now?" so the app can hide itself when the
// answer is no.
//
// The game runs under XWayland, so it lives inside the X11 world
// just like the pointer sensor (pointer.h): a plain X11 client
// connection can find its top-level window (Wine puts the
// executable name in WM_CLASS) and ask the X server which window
// holds the input focus (XGetInputFocus). When a native Wayland
// window is focused instead, the X focus falls back to PointerRoot /
// None — exactly the signal "the user is not in the game".
//
// Same backend-split pattern as overlay.h / pointer.h:
//
//   game_watch_x11.cpp     → X11 core protocol polling (this machine)
//   game_watch_windows.cpp → EnumWindows title match + GetForegroundWindow
// ─────────────────────────────────────────────────────────────
#pragma once

#include <functional>

namespace platform {

// Starts a ~4 Hz poll that finds the game's top-level window and
// watches whether it (or one of its children) holds the X input
// focus. Calls `on_change(true/false)` on the GLib main thread,
// after a short debounce, whenever that changes; also when the game
// window disappears entirely (crash, logout to character select).
//
// The initial state is available synchronously via
// game_has_focus_now() right after this call, so the app can decide
// the windows' starting visibility before presenting them.
//
// Returns false and logs the reason when the X11 display cannot be
// opened. Never fatal: the overlay simply stays always-visible.
bool game_watch_start(std::function<void(bool focused)> on_change);

// The focus state captured by the initial sweep inside
// game_watch_start(). Meaningful only right after a successful start.
bool game_has_focus_now();

// Stops the poll and closes the display. Safe to call when the watch
// is not running.
void game_watch_stop();

} // namespace platform
