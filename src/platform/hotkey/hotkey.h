// ─────────────────────────────────────────────────────────────
// platform/hotkey.h — global hotkey contract
//
// The app needs a key combo that works WHILE THE GAME HAS FOCUS.
// Wayland forbids plain clients from snooping global keys (no
// RegisterHotKey equivalent), so the only compositor-independent
// route is reading the kernel input layer directly (/dev/input) —
// the same source the compositor itself reads. We open devices
// read-only and never grab: the game keeps receiving every key.
//
// Each platform gets its own implementation:
//
//   hotkey_linux.cpp       → /dev/input via evdev + GLib fd sources
//   hotkey_windows.cpp   → RegisterHotKey + a message-only window
//
// SECURITY NOTE (Linux evdev backend): this capability is identical
// to what a keylogger uses — reading /dev/input shows every keystroke
// regardless of focus. This app records nothing (the events feed a
// 3-bool state machine and are discarded; verify in the source), but
// the PERMISSION it requires is the sensitive part: membership in
// the "input" group lets ANY process running as the same user read
// the keyboard silently, forever. That is why this backend is opt-in
// via [hotkey] mode = "evdev" and never the default.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <functional>
#include <string>

namespace platform {

// Starts listening for `combo` and calls `on_trigger` (on the GLib
// main thread, like any GTK callback) each time it fires.
//
// Combo format: '+'-separated modifiers plus one key, case
// insensitive: "Shift+Space", "Ctrl+F9", "Alt+P".
// Supported modifiers: Shift, Ctrl, Alt. Keys: A-Z, 0-9, F1-F12,
// Space, Tab, Esc, Enter, arrow keys. Extra modifiers held while
// pressing the key do NOT block the trigger (a game may have Shift
// down for other reasons) — only the required ones are checked.
//
// Returns false and logs the reason (unknown combo name, or no
// readable keyboard device — usually a missing input group
// membership) when the hotkey cannot start. Never fatal: the app
// runs fine without it and the D-Bus action stays available.
bool hotkey_start(const std::string& combo, std::function<void()> on_trigger);

} // namespace platform
