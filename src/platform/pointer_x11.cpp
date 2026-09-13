// ─────────────────────────────────────────────────────────────
// pointer_x11.cpp — X11 core-protocol pointer polling
//
// A regular X11 client connection (XOpenDisplay) is enough to ask
// the server for the pointer's current state: XQueryPointer returns
// root-window coordinates plus the button mask, XQueryKeymap the
// keyboard bitmap. The game runs under XWayland, so the dungeon
// dialog click happens inside exactly this X11 coordinate space.
//
// We poll at ~30 Hz from a GLib timeout and emit only PRESS edges,
// so one physical press fires the callback exactly once even though
// the button stays down for several polls. No threads: GLib timers
// run on the main loop, same as every other callback.
//
// Blind spot (by design, not a bug): while the pointer is over a
// native Wayland window, XWayland reports no fresh position. We only
// need the click that lands inside the game's X11 window, so the
// blind spot covers precisely the clicks we do not care about.
// ─────────────────────────────────────────────────────────────

#include "pointer.h"

#include <glib.h>

#include <X11/Xlib.h>

#include <utility>

namespace {

constexpr guint kPollIntervalMs = 33; // ~30 Hz

Display* g_display = nullptr;
Window   g_root = None;
guint    g_poll_source = 0;
std::function<void(int, int, bool)> g_on_click;
bool g_button_was_down = false;

bool ctrl_held() {
    // XQueryKeymap returns a 32-byte bitmap, one bit per keycode:
    // keycode n is down when byte n/8 has bit n%8 set. Keycodes are
    // server-wide constants under XWayland's evdev mapping: 37 is
    // Control_L and 109 is Control_R.
    char keymap[32];
    XQueryKeymap(g_display, keymap);
    constexpr int kCtrlL = 37;
    constexpr int kCtrlR = 109;
    return (keymap[kCtrlL / 8] & (1 << (kCtrlL % 8))) != 0 ||
           (keymap[kCtrlR / 8] & (1 << (kCtrlR % 8))) != 0;
}

gboolean on_poll(gpointer) {
    Window root_return, child_return;
    int root_x = 0, root_y = 0, win_x = 0, win_y = 0;
    unsigned int mask = 0;
    // Querying the root window yields ROOT coordinates: absolute
    // screen pixels, immune to any window moving under the pointer.
    if (XQueryPointer(g_display, g_root, &root_return, &child_return,
                      &root_x, &root_y, &win_x, &win_y,
                      &mask) == False)
        return G_SOURCE_CONTINUE; // pointer outside the X11 world

    const bool down = (mask & Button1Mask) != 0;
    if (down && !g_button_was_down && g_on_click)
        g_on_click(root_x, root_y, ctrl_held());
    g_button_was_down = down;
    return G_SOURCE_CONTINUE;
}

} // anonymous namespace

namespace platform {

bool pointer_watch_start(std::function<void(int, int, bool)> on_click) {
    if (g_poll_source != 0) return true; // already running

    g_display = XOpenDisplay(nullptr);
    if (g_display == nullptr) {
        g_warning("autoclick: cannot open the X11 display; pointer "
                  "watching unavailable (game must run under XWayland)");
        return false;
    }
    g_root = DefaultRootWindow(g_display);
    g_on_click = std::move(on_click);
    g_poll_source = g_timeout_add(kPollIntervalMs, on_poll, nullptr);
    return true;
}

void pointer_watch_stop() {
    if (g_poll_source == 0) return;
    g_source_remove(g_poll_source);
    g_poll_source = 0;
    g_on_click = nullptr;
    if (g_display != nullptr) {
        XCloseDisplay(g_display);
        g_display = nullptr;
    }
}

} // namespace platform
