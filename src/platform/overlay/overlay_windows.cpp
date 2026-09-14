// ─────────────────────────────────────────────────────────────
// overlay_windows.cpp — always-on-top overlay via Win32 styles
//
// The Win32 equivalent of the layer-shell backend. The GTK window's
// native HWND is turned into a floating overlay with four pieces of
// window-style surgery, applied once at realize time:
//
//   WS_POPUP         → no title bar, no border, no taskbar entry
//   WS_EX_TOPMOST    → floats above normal windows (always-on-top)
//   WS_EX_NOACTIVATE → clicking never steals keyboard focus,
//                       so the game keeps it in every mode
//   WS_EX_TRANSPARENT → click-through: mouse events fall to the
//                       window below (the game). Toggled by
//                       overlay_set_interactive, mirroring the X11
//                       input-region trick on Linux
//   WS_EX_LAYERED    → whole-window fade via
//                       SetLayeredWindowAttributes(LWA_ALPHA),
//                       driven by Placement::opacity
//
// TRANSLUCENCY SCOPE: LWA_ALPHA fades the whole window only. The
// per-pixel CSS alpha the Linux backend gets for free from the
// compositor has no equivalent here without taking over the paint
// pipeline (UpdateLayeredWindow), so bar/panel backgrounds render
// more opaque on Windows. Whole-window opacity (the [overlay]
// opacity knob) works identically on both.
//
// POSITIONING: unlike layer-shell margins (edge distances),
// SetWindowPos takes an absolute top-left corner — the margin math
// resolves HERE, once, so no custom→edge margin re-base is needed
// on this backend.
//
// DRAG: same GtkGestureDrag incremental recurrence as the Wayland
// backend (see the comment there): offsets are measured against the
// current window frame, so `current + offset` tracks the pointer.
// ─────────────────────────────────────────────────────────────

#include "platform/overlay/overlay.h"

#include <gdk/win32/gdkwin32.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// Everything the Win32 backend knows about one overlay surface.
// Lives in GObject data, freed by the destroy notify.
struct Win32Overlay {
    GtkWindow* window;
    platform::Placement placement; // contract type lives in namespace platform
    bool interactive = false;
    bool realized = false;

    // Drag-to-move (same contract as the Wayland backend).
    std::function<void(int, int)> on_moved;
    std::function<void()> on_tap;
    bool converted = false; // placement switched to absolute "custom"
    int abs_x = 0;          // last absolute top-left we positioned at
    int abs_y = 0;
};

void delete_overlay(gpointer data) {
    delete static_cast<Win32Overlay*>(data);
}

Win32Overlay* overlay_of(GtkWindow* window) {
    return static_cast<Win32Overlay*>(
        g_object_get_data(G_OBJECT(window), "cabal-win32-overlay"));
}

HWND hwnd_of(GtkWindow* window) {
    GdkSurface* surface = gtk_native_get_surface(GTK_NATIVE(window));
    if (surface == nullptr) return nullptr; // not realized yet
    return gdk_win32_surface_get_handle(surface);
}

// Measures the widget's natural size — the GTK4 way, same call the
// Wayland backend makes.
void measure(GtkWidget* widget, int* width, int* height) {
    gtk_widget_measure(widget, GTK_ORIENTATION_HORIZONTAL, -1,
                       nullptr, width, nullptr, nullptr);
    gtk_widget_measure(widget, GTK_ORIENTATION_VERTICAL, -1,
                       nullptr, height, nullptr, nullptr);
}

// Which screen edges an anchor string pins the surface to. A single
// edge (or none) leaves the other axis centered by resolve_position.
struct AnchorEdges {
    bool top = false;
    bool bottom = false;
    bool left = false;
    bool right = false;
};

AnchorEdges parse_anchor(const std::string& anchor) {
    AnchorEdges edges;
    edges.top = anchor.find("top") != std::string::npos;
    edges.bottom = anchor.find("bottom") != std::string::npos;
    edges.left = anchor.find("left") != std::string::npos;
    edges.right = anchor.find("right") != std::string::npos;
    return edges;
}

// Resolves the placement into an absolute top-left corner against
// the primary screen size. "custom" margins ARE the absolute corner
// — and "custom" contains no edge keyword, so without the explicit
// check it would silently fall through to CENTERING (confirmed in a
// Wine trace: the panel landed at (screen-size)/2 instead of its
// configured position).
void resolve_position(const Win32Overlay* state, int* x, int* y) {
    if (state->placement.anchor == "custom") {
        *x = state->placement.margin_x;
        *y = state->placement.margin_y;
        return;
    }
    const int screen_w = GetSystemMetrics(SM_CXSCREEN);
    const int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int width = 0;
    int height = 0;
    measure(GTK_WIDGET(state->window), &width, &height);

    const AnchorEdges edges = parse_anchor(state->placement.anchor);
    const int mx = state->placement.margin_x;
    const int my = state->placement.margin_y;
    *x = edges.left   ? mx
       : edges.right  ? screen_w - mx - width
       : (screen_w - width) / 2;
    *y = edges.top    ? my
       : edges.bottom ? screen_h - my - height
       : (screen_h - height) / 2;
}

// Re-styles the HWND into an overlay window. Idempotent: safe on
// every realize, harmless if called twice with the same flags.
void apply_window_styles(Win32Overlay* state) {
    const HWND hwnd = hwnd_of(state->window);
    if (hwnd == nullptr) return;

    // WS_POPUP replaces the decorated frame wholesale, but WS_VISIBLE
    // must survive: a style write that drops it hides the window even
    // when everything else is right (this exact bug shipped in 0.1.1).
    const LONG_PTR current = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | (current & WS_VISIBLE));
    LONG_PTR exstyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED;
    if (!state->interactive) exstyle |= WS_EX_TRANSPARENT;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exstyle);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void apply_position(Win32Overlay* state) {
    const HWND hwnd = hwnd_of(state->window);
    if (hwnd == nullptr) return;

    int width = 0;
    int height = 0;
    measure(GTK_WIDGET(state->window), &width, &height);
    resolve_position(state, &state->abs_x, &state->abs_y);
    SetWindowPos(hwnd, HWND_TOPMOST, state->abs_x, state->abs_y,
                 width, height, SWP_NOACTIVATE);

    const auto alpha = static_cast<BYTE>(std::clamp(
        static_cast<int>(state->placement.opacity * 255.0), 0, 255));
    SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
}

void on_realize(GtkWidget*, gpointer state_ptr) {
    auto* state = static_cast<Win32Overlay*>(state_ptr);
    state->realized = true;
    apply_window_styles(state);
    apply_position(state);
}

// GDK re-applies its own geometry whenever the toplevel re-lays out
// (content changes — the clock label ticks every second — or at
// present time), stomping our SetWindowPos placement. Re-applying on
// default size changes keeps the overlay where the config put it.
void on_size_changed(GtkWidget*, GParamSpec*, gpointer state_ptr) {
    auto* state = static_cast<Win32Overlay*>(state_ptr);
    if (state->realized) apply_position(state);
}

// ── Drag-to-move ─────────────────────────────────────────────
// See the block comment in overlay_wayland.cpp for the full
// explanation of the incremental recurrence; it applies verbatim
// here (Win32 offsets are also relative to the moving frame).

void convert_to_custom(Win32Overlay* state) {
    int width = 0;
    int height = 0;
    measure(GTK_WIDGET(state->window), &width, &height);
    resolve_position(state, &state->abs_x, &state->abs_y);
    state->placement.anchor = "custom";
    state->placement.margin_x = state->abs_x;
    state->placement.margin_y = state->abs_y;
    state->converted = true;
}

void on_drag_begin(GtkGestureDrag*, double, double, gpointer state_ptr) {
    auto* state = static_cast<Win32Overlay*>(state_ptr);
    if (!state->converted) convert_to_custom(state);
    gtk_widget_set_opacity(GTK_WIDGET(state->window),
                           state->placement.opacity * 0.8);
}

void on_drag_update(GtkGestureDrag*, double offset_x, double offset_y,
                    gpointer state_ptr) {
    auto* state = static_cast<Win32Overlay*>(state_ptr);
    // Incremental, NOT from start: the offset is cumulative but
    // measured against the window's CURRENT position.
    state->placement.margin_x += static_cast<int>(offset_x);
    state->placement.margin_y += static_cast<int>(offset_y);
    apply_position(state);
}

void on_drag_end(GtkGestureDrag*, double offset_x, double offset_y,
                 gpointer state_ptr) {
    auto* state = static_cast<Win32Overlay*>(state_ptr);
    gtk_widget_set_opacity(GTK_WIDGET(state->window),
                           state->placement.opacity);
    if (std::abs(offset_x) < 3.0 && std::abs(offset_y) < 3.0) {
        if (state->on_tap) state->on_tap();
        return;
    }
    if (state->on_moved)
        state->on_moved(state->placement.margin_x,
                        state->placement.margin_y);
}

} // anonymous namespace

namespace platform {

void overlay_init(GtkWindow* window) {
    auto* state = new Win32Overlay{ window, {}, false, false,
                                    nullptr, nullptr, false, 0, 0 };
    g_object_set_data_full(G_OBJECT(window), "cabal-win32-overlay", state,
                           delete_overlay);
    // The HWND does not exist until the window is realized; the
    // style surgery and first positioning happen then.
    g_signal_connect(window, "realize", G_CALLBACK(on_realize), state);
    // GDK stomps our placement on every relayout (see on_size_changed);
    // re-apply after each size change and after present.
    g_signal_connect(window, "notify::default-width",
                     G_CALLBACK(on_size_changed), state);
    g_signal_connect(window, "notify::default-height",
                     G_CALLBACK(on_size_changed), state);
}

Placement overlay_apply_placement(GtkWindow* window, Placement placement) {
    Win32Overlay* state = overlay_of(window);
    if (state == nullptr) return placement; // defensive; init ran first
    state->placement = placement;
    // Re-allow converting on the next drag whenever the surface is
    // not currently custom (a config edit may re-anchor it).
    state->converted = placement.anchor == "custom";
    if (state->realized) apply_position(state);
    return placement;
}

void overlay_set_interactive(GtkWindow* window, bool enabled) {
    Win32Overlay* state = overlay_of(window);
    if (state == nullptr) return;
    state->interactive = enabled;
    if (!state->realized) return; // applied by on_realize

    // WS_EX_TRANSPARENT is the click-through switch: with it, mouse
    // events fall through to the window below; without it, this
    // window receives them normally.
    const HWND hwnd = hwnd_of(window);
    LONG_PTR exstyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (enabled) exstyle &= ~WS_EX_TRANSPARENT;
    else         exstyle |= WS_EX_TRANSPARENT;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exstyle);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void overlay_enable_drag(GtkWindow* window,
                         std::function<void(int, int)> on_moved,
                         std::function<void()> on_tap) {
    Win32Overlay* state = overlay_of(window);
    if (state == nullptr) return;
    state->on_moved = std::move(on_moved);
    state->on_tap = std::move(on_tap);

    auto* gesture = gtk_gesture_drag_new();
    g_signal_connect(gesture, "drag-begin", G_CALLBACK(on_drag_begin), state);
    g_signal_connect(gesture, "drag-update", G_CALLBACK(on_drag_update), state);
    g_signal_connect(gesture, "drag-end", G_CALLBACK(on_drag_end), state);
    gtk_widget_add_controller(GTK_WIDGET(window), GTK_EVENT_CONTROLLER(gesture));
}

} // namespace platform
