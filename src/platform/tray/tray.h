// ─────────────────────────────────────────────────────────────
// platform/tray/tray.h — system tray presence contract
//
// The overlay surfaces have no taskbar entry BY DESIGN (they are
// WS_POPUP / layer-shell overlays — decorationless floaters). That
// leaves the app with no visible lifecycle handle: no icon to prove
// it is running, nothing to right-click to close it. On Windows the
// classic answer is a system tray icon with a context menu.
//
//   tray_windows.cpp → Shell_NotifyIconW + popup menu on a
//                      message-only window (same pump trick as the
//                      hotkey sink)
//   tray_none.cpp    → Linux no-op: Wayland layer-shell apps have no
//                      tray story without a StatusNotifierItem D-Bus
//                      bridge, and Linux users already have the D-Bus
//                      actions + pkill
//
// The callbacks run on the GTK main thread, like every other
// platform callback in this project.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <functional>

namespace platform {

// What the tray menu offers. Either callback may be empty; an empty
// one simply disables that menu item's effect.
struct TrayActions {
    std::function<void()> on_show_settings;
    std::function<void()> on_quit;
};

// Adds the tray icon. Returns false when the platform has no tray
// concept (tray_none) or the icon could not be added — never fatal,
// the app works fine without a tray.
bool tray_start(TrayActions actions);

// Removes the icon. Safe to call when the tray was never started.
// If the process exits without this, the icon lingers as a ghost
// until hovered (classic Shell_NotifyIcon quirk), so the app calls
// this on shutdown.
void tray_stop();

} // namespace platform
