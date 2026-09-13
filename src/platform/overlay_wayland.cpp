// ─────────────────────────────────────────────────────────────
// overlay_wayland.cpp — Wayland backend (wlr-layer-shell)
//
// Works on KDE Plasma (KWin) and wlroots compositors (Sway,
// Hyprland, ...). Does NOT work on GNOME/Mutter, which has no
// layer-shell support — that platform would get its own .cpp.
// ─────────────────────────────────────────────────────────────

#include "overlay.h"

#include <functional>
#include <string>
#include <utility>

#include <gtk4-layer-shell/gtk4-layer-shell.h>

namespace {

// Anchor is a string like "bottom-left": we look for each edge
// keyword independently, so "top", "top-right" or "left-bottom"
// all parse the same way. Anchoring only ONE horizontal edge (e.g.
// "right") makes the compositor center the surface along the other
// axis — that is how the goals panel floats centered on the right.
// "custom" is the absolute scheme: anchored top-left, margins are
// the x/y offset from the screen corner.
struct AnchorEdges {
    bool top;
    bool bottom;
    bool left;
    bool right;
};

AnchorEdges parse_anchor(const std::string& anchor) {
    if (anchor == "custom")
        return { .top = true, .bottom = false, .left = true, .right = false };
    return {
        .top    = anchor.find("top")    != std::string::npos,
        .bottom = anchor.find("bottom") != std::string::npos,
        .left   = anchor.find("left")   != std::string::npos,
        .right  = anchor.find("right")  != std::string::npos,
    };
}

void apply_position(GtkWindow* window, const platform::Placement& placement) {
    const AnchorEdges edges = parse_anchor(placement.anchor);

    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, edges.top ? TRUE : FALSE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, edges.right ? TRUE : FALSE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, edges.bottom ? TRUE : FALSE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, edges.left ? TRUE : FALSE);

    // Margins only apply to anchored edges; an unanchored edge keeps
    // margin 0 so the surface stays glued to the screen edge it floats at.
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_LEFT, edges.left ? placement.margin_x : 0);
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_RIGHT, edges.right ? placement.margin_x : 0);
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_TOP, edges.top ? placement.margin_y : 0);
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_BOTTOM, edges.bottom ? placement.margin_y : 0);
}

// ── Drag-to-move ─────────────────────────────────────────────
// Wayland gives clients no global pointer coordinates: the drag
// gesture reports offsets relative to the widget's CURRENT frame —
// and that frame moves with the surface while we drag it.
//
// The offsets are cumulative from drag-begin but measured against the
// latest frame, so each event already "knows" how far the pointer is
// from the surface's current position. The correct recurrence is
// therefore INCREMENTAL (current + offset), which lands exactly on
// "pointer minus the grabbed point" every event.
//
// The naive `start_margin + offset` instead feeds the accumulated
// movement back into the measurement: the error compounds on every
// event until the margins fly off-screen and the compositor clamps
// them (observed: flicker, surface ending at y = 0).

struct DragState {
    GtkWindow* window;
    std::function<void(int, int)> on_moved;
    std::function<void()> on_tap;
    platform::Placement current; // last known placement (kept in sync)
    bool converted = false;      // already switched to "custom"
};

void delete_drag_state(gpointer data) {
    delete static_cast<DragState*>(data);
}

DragState* drag_state_of(GtkWindow* window) {
    return static_cast<DragState*>(
        g_object_get_data(G_OBJECT(window), "cabal-drag"));
}

// Measures the widget's natural size and the geometry of the monitor
// it is realized on (monitor 0 as the not-yet-realized fallback).
void measure_on_monitor(GtkWidget* widget, int* width, int* height,
                        GdkRectangle* geo) {
    // gtk_widget_measure is the GTK4 way to learn a widget's natural
    // size before/without relying on allocations.
    gtk_widget_measure(widget, GTK_ORIENTATION_HORIZONTAL, -1,
                       nullptr, width, nullptr, nullptr);
    gtk_widget_measure(widget, GTK_ORIENTATION_VERTICAL, -1,
                       nullptr, height, nullptr, nullptr);

    GdkDisplay* display = gtk_widget_get_display(widget);
    GdkSurface* surface = gtk_native_get_surface(GTK_NATIVE(widget));
    // No gdk_display_get_primary_monitor in GTK4; the monitor the
    // surface lives on is the right one anyway.
    GdkMonitor* monitor = nullptr;
    bool owned = false; // whether `monitor` holds a reference to drop
    if (surface != nullptr) {
        monitor = gdk_display_get_monitor_at_surface(display, surface);
    } else {
        monitor = GDK_MONITOR(
            g_list_model_get_item(gdk_display_get_monitors(display), 0));
        owned = true; // g_list_model_get_item returns a new ref
    }
    *geo = { 0, 0, 0, 0 };
    if (monitor != nullptr) {
        gdk_monitor_get_geometry(monitor, geo);
        if (owned) g_object_unref(monitor);
    }
}

// Converts the stored placement to absolute "custom" coordinates and
// applies them. Needs the window size and the monitor geometry — the
// margin math of the current anchor reversed.
void convert_to_custom(DragState* state) {
    int width = 0;
    int height = 0;
    GdkRectangle geo { 0, 0, 0, 0 };
    measure_on_monitor(GTK_WIDGET(state->window), &width, &height, &geo);

    const AnchorEdges edges = parse_anchor(state->current.anchor);
    const int mx = state->current.margin_x;
    const int my = state->current.margin_y;
    // Unanchored axis = compositor-centered; margin plays no role.
    const int abs_x = edges.left   ? mx
                    : edges.right  ? geo.width - mx - width
                    : (geo.width - width) / 2;
    const int abs_y = edges.top    ? my
                    : edges.bottom ? geo.height - my - height
                    : (geo.height - height) / 2;

    state->current.anchor = "custom";
    state->current.margin_x = abs_x;
    state->current.margin_y = abs_y;
    state->converted = true;
    apply_position(state->window, state->current);
}

void on_drag_begin(GtkGestureDrag*, double, double, gpointer state_ptr) {
    auto* state = static_cast<DragState*>(state_ptr);
    if (!state->converted) convert_to_custom(state);
    // Dim as a light "I'm being dragged" hint; the position itself
    // already follows the pointer live (see the block comment above).
    gtk_widget_set_opacity(GTK_WIDGET(state->window),
                           state->current.opacity * 0.8);
}

void on_drag_update(GtkGestureDrag*, double offset_x, double offset_y,
                    gpointer state_ptr) {
    auto* state = static_cast<DragState*>(state_ptr);
    // Incremental, NOT from start: the offset is cumulative but
    // measured against the surface's CURRENT position (it moved with
    // the last event), so adding it to the current margins tracks the
    // pointer exactly. See the block comment above the struct.
    state->current.margin_x += static_cast<int>(offset_x);
    state->current.margin_y += static_cast<int>(offset_y);
    apply_position(state->window, state->current);
}

void on_drag_end(GtkGestureDrag*, double offset_x, double offset_y,
                 gpointer state_ptr) {
    auto* state = static_cast<DragState*>(state_ptr);
    // Restore the configured opacity regardless of how the drag ended.
    gtk_widget_set_opacity(GTK_WIDGET(state->window),
                           state->current.opacity);
    // A press-and-release under ~3 px is a tap, not a move — the app
    // decides what a tap means (the clock bar uses it to exit
    // interactive mode).
    if (std::abs(offset_x) < 3.0 && std::abs(offset_y) < 3.0) {
        if (state->on_tap) state->on_tap();
        return;
    }
    // The surface already followed the pointer live during the
    // gesture; just report where it ended up.
    if (state->on_moved) state->on_moved(state->current.margin_x,
                                         state->current.margin_y);
}

} // anonymous namespace

namespace platform {

void overlay_init(GtkWindow* window) {
    gtk_layer_init_for_window(window);

    // OVERLAY layer     → drawn above everything, even fullscreen apps.
    // Exclusive zone -1 → "I float on top, don't reserve space for me".
    // Placement is deliberately NOT applied here: the caller decides
    // where each surface goes (and can re-apply it on config reload).
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_exclusive_zone(window, -1);
}

Placement overlay_apply_placement(GtkWindow* window, Placement placement) {
    if (DragState* state = drag_state_of(window)) {
        const bool was_custom = state->current.anchor == "custom";
        if (was_custom && placement.anchor != "custom") {
            // Margins stored under "custom" are ABSOLUTE coordinates
            // from the screen's top-left corner. Reinterpreted raw as
            // edge distances they fling the surface across the screen
            // (a panel dragged to x=1276 lands on the LEFT half under
            // "bottom-right" — the anchors look "swapped"). Convert
            // per-axis so the surface stays exactly where it is; the
            // caller may persist the returned placement.
            int width = 0;
            int height = 0;
            GdkRectangle geo { 0, 0, 0, 0 };
            measure_on_monitor(GTK_WIDGET(window), &width, &height, &geo);
            const int abs_x = state->current.margin_x;
            const int abs_y = state->current.margin_y;
            const AnchorEdges edges = parse_anchor(placement.anchor);
            if (edges.left)   placement.margin_x = abs_x;
            if (edges.right)  placement.margin_x = geo.width - abs_x - width;
            if (edges.top)    placement.margin_y = abs_y;
            if (edges.bottom) placement.margin_y = geo.height - abs_y - height;
            // An anchor missing an axis is compositor-centered there;
            // the margin is ignored, so it needs no conversion.
        }
        // Keep the drag state in sync so a drag starts from the
        // CURRENT position even after a live config reload moved the
        // surface; `converted` re-arms whenever the surface is not
        // currently custom (convert_to_custom flips it).
        state->current = placement;
        state->converted = placement.anchor == "custom";
    }

    apply_position(window, placement);
    // Fades the whole surface (text included). The CSS alpha on the
    // background is a separate, independent translucency knob.
    gtk_widget_set_opacity(GTK_WIDGET(window), placement.opacity);
    return placement;
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

void overlay_enable_drag(GtkWindow* window,
                         std::function<void(int, int)> on_moved,
                         std::function<void()> on_tap) {
    auto* state = new DragState{ window, std::move(on_moved),
                                 std::move(on_tap), {}, false };
    g_object_set_data_full(G_OBJECT(window), "cabal-drag", state,
                           delete_drag_state);

    auto* gesture = gtk_gesture_drag_new();
    g_signal_connect(gesture, "drag-begin", G_CALLBACK(on_drag_begin), state);
    g_signal_connect(gesture, "drag-update", G_CALLBACK(on_drag_update), state);
    g_signal_connect(gesture, "drag-end", G_CALLBACK(on_drag_end), state);
    gtk_widget_add_controller(GTK_WIDGET(window), GTK_EVENT_CONTROLLER(gesture));
}

} // namespace platform
