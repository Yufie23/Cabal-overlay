// ─────────────────────────────────────────────────────────────
// platform/overlay_wayland.cpp — Wayland backend (wlr-layer-shell)
//
// Works on KDE Plasma (KWin) and wlroots compositors (Sway,
// Hyprland, ...). Does NOT work on GNOME/Mutter, which has no
// layer-shell support — that platform would get its own .cpp.
// ─────────────────────────────────────────────────────────────

#include "overlay.h"

#include <gtk4-layer-shell/gtk4-layer-shell.h>

namespace platform {

void overlay_init(GtkWindow* window) {
    // OVERLAY layer   → drawn above everything, even fullscreen apps.
    // Anchors         → pinned to the bottom-left corner, with margins.
    // Exclusive zone -1 → "I float on top, don't reserve space for me".
    gtk_layer_init_for_window(window);
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_BOTTOM, 35);
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_LEFT, 1);
    gtk_layer_set_exclusive_zone(window, -1);
}

void overlay_set_interactive(GtkWindow* window, bool enabled) {
    GdkSurface* surface = gtk_native_get_surface(GTK_NATIVE(window));
    if (surface == nullptr) return; // Window not realized yet.

    // Wayland rule: a surface receives pointer input only inside its
    // input region. Empty region = clicks fall through to the game;
    // default region (nullptr) = the whole surface is clickable.
    // Keyboard focus follows the same flag: an overlay that ignores
    // the mouse must not steal key events from the game either.
    if (enabled) {
        gdk_surface_set_input_region(surface, nullptr);
        gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    } else {
        cairo_region_t* empty = cairo_region_create();
        gdk_surface_set_input_region(surface, empty);
        cairo_region_destroy(empty); // The surface keeps its own copy.
        gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    }
}

} // namespace platform
