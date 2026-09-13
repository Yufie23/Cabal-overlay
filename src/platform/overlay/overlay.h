// ─────────────────────────────────────────────────────────────
// platform/overlay.h — the platform abstraction
//
// This header is the CONTRACT between the portable app code and
// whatever the host OS needs to make an overlay work. The app only
// ever calls these functions; each platform provides its own .cpp
// implementing them:
//
//   overlay_wayland.cpp    → wlr-layer-shell (KDE Plasma, wlroots)
//   overlay_windows.cpp    → WS_EX_TOPMOST + WS_EX_NOACTIVATE +
//                           WS_EX_TRANSPARENT + WS_EX_LAYERED
//   (a GNOME/Mutter backend would need a different trick, e.g. an
//    always-on-top borderless window, since Mutter has no layer-shell)
//
// The contract knows nothing about config files or app structs: it
// speaks in Placement values only, so the platform layer never has
// to change when the app's configuration model does. Every window
// (clock bar, goals panel, future expanded panel) goes through the
// same three calls.
//
// Note the types: the contract speaks C++ (bool, our own structs),
// not C (gboolean). Conversions to C APIs happen inside each
// platform .cpp, at the boundary — never in app code.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <functional>
#include <string>

#include <gtk/gtk.h>

namespace platform {

// Where a surface floats on screen and how visible it is.
// `anchor` holds edge keywords ("right", "bottom-left", ...);
// anchoring to a single horizontal edge (e.g. just "right") lets
// the compositor center the surface along the other axis.
// The special value "custom" pins the surface to an absolute
// position: anchored top-left, margins = x/y from the screen corner
// (set by dragging the window, or by hand).
struct Placement {
    std::string anchor;
    int margin_x = 0;
    int margin_y = 0;
    double opacity = 1.0;
};

// Turns `window` into an always-on-top overlay floating above other
// windows (including fullscreen apps). Must be called before the
// window is presented. Placement is NOT applied here — call
// overlay_apply_placement once per window.
void overlay_init(GtkWindow* window);

// Positions and fades a window. Used at startup and by live config
// reload (edit the TOML, watch the surface move without restarting).
// Returns the placement actually applied: when the surface was on a
// "custom" anchor and the new one is an edge, the absolute margins
// are converted to edge distances so the surface does not jump — the
// caller may persist the result so the file matches the screen.
Placement overlay_apply_placement(GtkWindow* window, Placement placement);

// enabled = false → click-through: pointer input falls to the app
// below (the game). enabled = true → the overlay is clickable.
void overlay_set_interactive(GtkWindow* window, bool enabled);

// Enables drag-to-move (effective only while the surface is
// interactive — in click-through mode the events go to the game).
// At drag start the surface switches to "custom" positioning
// (anchored top-left, margins = absolute position), follows the
// pointer live, and `on_moved` reports the final margins so the app
// persists them. `on_tap` (press-and-release under ~3 px) lets the
// app distinguish "user clicked the window" from "user moved it" —
// an empty function is fine for windows without click behavior.
void overlay_enable_drag(GtkWindow* window,
                         std::function<void(int margin_x, int margin_y)> on_moved,
                         std::function<void()> on_tap);

} // namespace platform
