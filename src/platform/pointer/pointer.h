// ─────────────────────────────────────────────────────────────
// platform/pointer.h — pointer observation contract
//
// The dgcheck feature needs to know "the user pressed the primary
// button at (x, y) with CTRL held". On Linux/X11 the game runs under
// XWayland, so a plain X11 client connection sees exactly the events
// the game window produces — the dialog click happens INSIDE that
// X11 world, which is precisely where we can observe it.
//
// This is NOT input snooping: no /dev/input access, no extra groups,
// no global key capture. XQueryPointer asks the X server for the
// current pointer state, the same call any tooltip or drag-and-drop
// implementation uses. While the pointer is over a native Wayland
// window we simply get "unchanged" — nothing leaks from other apps.
//
// Backend split (same pattern as overlay.h / hotkey.h):
//
//   pointer_x11.cpp          → X11 core protocol polling (Linux/XWayland)
//   pointer_windows.cpp      → GetCursorPos + GetAsyncKeyState polling
// ─────────────────────────────────────────────────────────────
#pragma once

#include <functional>

namespace platform {

// Starts a ~30 Hz poll of the pointer and calls `on_click` (on the
// GLib main thread, like any GTK callback) on every PRESS edge of
// the primary button, with the absolute screen position and whether
// CTRL was held at that moment. The callback runs at most once per
// physical press; release edges are not reported.
//
// Returns false and logs the reason when the X11 display cannot be
// opened. Never fatal: the overlay runs fine without it.
bool pointer_watch_start(std::function<void(int x, int y, bool ctrl)> on_click);

// Stops the poll and closes the display. Safe to call when the watch
// is not running.
void pointer_watch_stop();

} // namespace platform
