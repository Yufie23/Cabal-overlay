// ─────────────────────────────────────────────────────────────
// platform/overlay_wayland.cpp — Wayland backend (wlr-layer-shell)
//
// Works on KDE Plasma (KWin) and wlroots compositors (Sway,
// Hyprland, ...). Does NOT work on GNOME/Mutter, which has no
// layer-shell support — that platform would get its own .cpp.
// ─────────────────────────────────────────────────────────────

#include "overlay.h"

#include <string>

#include <gtk4-layer-shell/gtk4-layer-shell.h>

namespace {

// Position + fade come from the config now, not from hardcoded
// constants. Anchor is a string like "bottom-left": we look for
// each edge keyword independently, so "top", "top-right" or
// "left-bottom" all parse the same way.
void apply_position(GtkWindow* window, const OverlayConfig& config) {
    const std::string& anchor = config.anchor;
    const bool top    = anchor.find("top")    != std::string::npos;
    const bool bottom = anchor.find("bottom") != std::string::npos;
    const bool left   = anchor.find("left")   != std::string::npos;
    const bool right  = anchor.find("right")  != std::string::npos;

    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, top ? TRUE : FALSE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, right ? TRUE : FALSE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, bottom ? TRUE : FALSE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, left ? TRUE : FALSE);

    // Margins only apply to anchored edges; an unanchored edge keeps
    // margin 0 so the bar stays glued to the screen edge it floats at.
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_LEFT, left ? config.margin_x : 0);
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_RIGHT, right ? config.margin_x : 0);
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_TOP, top ? config.margin_y : 0);
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_BOTTOM, bottom ? config.margin_y : 0);
}

} // anonymous namespace

namespace platform {

void overlay_init(GtkWindow* window, const OverlayConfig& config) {
    gtk_layer_init_for_window(window);

    // OVERLAY layer   → drawn above everything, even fullscreen apps.
    // Exclusive zone -1 → "I float on top, don't reserve space for me".
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_exclusive_zone(window, -1);

    // Calls the declaration from overlay.h (defined below in this
    // file); the unqualified name would only find it after its
    // definition, and C++ reads top to bottom.
    platform::overlay_apply_config(window, config);
}

void overlay_apply_config(GtkWindow* window, const OverlayConfig& config) {
    apply_position(window, config);
    // Fades the whole bar (text included). The CSS alpha on the bar
    // background is a separate, independent translucency knob.
    gtk_widget_set_opacity(GTK_WIDGET(window), config.opacity);
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
