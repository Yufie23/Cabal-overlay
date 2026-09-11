// ─────────────────────────────────────────────────────────────
// platform/overlay.h — the platform abstraction
//
// This header is the CONTRACT between the portable app code and
// whatever the host OS needs to make an overlay work. The app only
// ever calls these functions; each platform provides its own .cpp
// implementing them:
//
//   overlay_wayland.cpp  → wlr-layer-shell (KDE Plasma, wlroots)
//   overlay_windows.cpp  → WS_EX_LAYERED + WS_EX_TRANSPARENT + topmost
//                          (not written yet — the door is open)
//   (a GNOME/Mutter backend would need a different trick, e.g. an
//    always-on-top borderless window, since Mutter has no layer-shell)
//
// Note the types: the contract speaks C++ (bool, our own structs),
// not C (gboolean). Conversions to C APIs happen inside each platform
// .cpp, at the boundary — never in app code.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <gtk/gtk.h>

#include "config.h"

namespace platform {

    // Turns `window` into an always-on-top overlay floating above other
    // windows (including fullscreen apps), positioned/styled per `config`.
    // Must be called before the window is presented.
    void overlay_init(GtkWindow* window, const OverlayConfig& config);

    // Re-applies position and opacity from `config`. Used both by
    // overlay_init and by live config reload (edit the TOML, watch the
    // bar move without restarting the app).
    void overlay_apply_config(GtkWindow* window, const OverlayConfig& config);

    // enabled = false → click-through: pointer input falls to the app
    // below (the game). enabled = true → the overlay is clickable.
    void overlay_set_interactive(GtkWindow* window, bool enabled);

} // namespace platform
