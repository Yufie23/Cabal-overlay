// ─────────────────────────────────────────────────────────────
// pointer_windows.cpp — Win32 pointer polling
//
// Same contract as pointer_x11.cpp, but strictly simpler: the Win32
// API has no "only over X11 surfaces" blind spot. GetCursorPos and
// GetAsyncKeyState are process-wide and work while ANY window — the
// game included — holds the focus, which is exactly what a dungeon
// clear counter wants.
//
// Edge detection mirrors the X11 backend: poll at ~30 Hz, report
// only PRESS edges of the primary button, so one physical press fires
// the callback exactly once. No threads: the GLib timeout runs on
// the GTK main thread.
//
// GetAsyncKeyState's high bit (0x8000) is the "currently down" state;
// the low bit ("pressed since last call") is useless for polling —
// querying it would consume the edge we are trying to count.
// ─────────────────────────────────────────────────────────────

#include "platform/pointer/pointer.h"

#include <glib.h>

#include <windows.h>

#include <utility>

namespace {

constexpr guint kPollIntervalMs = 33; // ~30 Hz

guint g_poll_source = 0;
std::function<void(int, int, bool)> g_on_click;
bool g_button_was_down = false;

gboolean on_poll(gpointer) {
    POINT point {};
    if (GetCursorPos(&point) == 0)
        return G_SOURCE_CONTINUE; // should not happen; try again next tick

    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (down && !g_button_was_down && g_on_click)
        g_on_click(point.x, point.y,
                   (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
    g_button_was_down = down;
    return G_SOURCE_CONTINUE;
}

} // anonymous namespace

namespace platform {

bool pointer_watch_start(std::function<void(int, int, bool)> on_click) {
    if (g_poll_source != 0) return true; // already running

    g_on_click = std::move(on_click);
    g_poll_source = g_timeout_add(kPollIntervalMs, on_poll, nullptr);
    return true; // no display to open: the API is always available
}

void pointer_watch_stop() {
    if (g_poll_source == 0) return;
    g_source_remove(g_poll_source);
    g_poll_source = 0;
    g_on_click = nullptr;
}

} // namespace platform
