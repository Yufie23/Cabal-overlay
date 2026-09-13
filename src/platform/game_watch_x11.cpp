// ─────────────────────────────────────────────────────────────
// game_watch_x11.cpp — X11 game-window + input-focus polling
//
// Two X11 primitives do all the work:
//
//   XQueryTree + XGetClassHint  → find the game's top-level window.
//                                 Wine sets WM_CLASS to the executable
//                                 name, so "cabal" in the class/name
//                                 identifies CabalMain.exe reliably.
//   XGetInputFocus              → which X window holds keyboard focus.
//                                 The game only exists inside X11
//                                 (XWayland), so when anything native
//                                 Wayland is focused the X focus is
//                                 PointerRoot/None → "not in game".
//
// The window query can hit a destroyed window (game restart, crash),
// which raises an X error; the default handler would abort the whole
// app, so a silent one is installed. It is process-global Xlib state
// — acceptable here because this app has exactly two X11 consumers
// (this module and pointer_x11.cpp) and both anticipate errors.
//
// No threads: a GLib timeout on the main loop, same as pointer_x11.
// Hiding is debounced (~3 polls) so transient focus flickers (window
// shadows of alt-tab, quick taskbar peeks) do not strobe the overlay;
// gaining focus is reported immediately.
// ─────────────────────────────────────────────────────────────

#include "game_watch.h"

#include <glib.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h> // XClassHint / XGetClassHint

#include <cctype>
#include <cstdint>
#include <string_view>
#include <utility>

namespace {

constexpr guint kPollIntervalMs = 250;
constexpr int kHideDebounceMisses = 3; // ~750 ms unfocused before hiding

Display* g_display = nullptr;
Window g_root = None;
Window g_game_window = None; // None until the game window is found
guint g_poll_source = 0;
std::function<void(bool)> g_on_change;
bool g_focused = false; // last REPORTED state
int g_misses = 0;       // consecutive unfocused polls while visible

int ignore_x_error(Display*, XErrorEvent*) { return 0; }

bool contains_nocase(std::string_view haystack, std::string_view needle) {
    if (needle.empty() || haystack.size() < needle.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            const auto a = static_cast<unsigned char>(haystack[i + j]);
            const auto b = static_cast<unsigned char>(needle[j]);
            if (std::tolower(a) != std::tolower(b)) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool window_matches_game(Window window) {
    // Wine identifies its windows with the executable name in
    // WM_CLASS (instance and class); the window title is a fallback
    // for launchers that predate the main client window.
    XClassHint hint { nullptr, nullptr };
    if (XGetClassHint(g_display, window, &hint) != 0) {
        const bool hit = contains_nocase(hint.res_name != nullptr
                                             ? hint.res_name
                                             : "",
                                         "cabal") ||
                         contains_nocase(hint.res_class != nullptr
                                             ? hint.res_class
                                             : "",
                                         "cabal");
        if (hint.res_name != nullptr) XFree(hint.res_name);
        if (hint.res_class != nullptr) XFree(hint.res_class);
        if (hit) return true;
    }
    char* name = nullptr;
    if (XFetchName(g_display, window, &name) != 0 && name != nullptr) {
        const bool hit = contains_nocase(name, "cabal");
        XFree(name);
        if (hit) return true;
    }
    return false;
}

// Scans the root window's viewable top-level children for the game.
// Re-run on every poll while the game window is unknown: the overlay
// may well start before the game does.
Window find_game_window() {
    Window root = None;
    Window parent = None;
    Window* children = nullptr;
    unsigned int count = 0;
    if (XQueryTree(g_display, g_root, &root, &parent, &children, &count) == 0)
        return None;
    Window found = None;
    for (unsigned int i = 0; i < count && found == None; ++i) {
        XWindowAttributes attributes;
        if (XGetWindowAttributes(g_display, children[i], &attributes) != 0 &&
            attributes.map_state == IsViewable &&
            window_matches_game(children[i]))
            found = children[i];
    }
    if (children != nullptr) XFree(children);
    return found;
}

// The focused window may be a CHILD of the game window (Wine input
// widgets), so walk up the tree: focused == game, or one of its
// descendants, counts as "in game". Stops at the top level (parent
// == root): any other top-level window means some other X app holds
// the focus.
bool focused_is_in_game(Window focused) {
    Window current = focused;
    for (int depth = 0; depth < 32 && current != None; ++depth) {
        if (current == g_game_window) return true;
        Window root = None;
        Window parent = None;
        Window* children = nullptr;
        unsigned int count = 0;
        if (XQueryTree(g_display, current, &root, &parent, &children,
                       &count) == 0)
            return false;
        if (children != nullptr) XFree(children);
        if (parent == root) return false;
        current = parent;
    }
    return false;
}

// One focus sweep. Out: whether the game window exists at all right
// now (running and viewable). Returns: the game holds the X focus.
bool sweep_focus(bool* game_running) {
    if (g_game_window == None)
        g_game_window = find_game_window();
    if (g_game_window == None) {
        *game_running = false;
        return false;
    }
    XWindowAttributes attributes;
    if (XGetWindowAttributes(g_display, g_game_window, &attributes) == 0) {
        g_game_window = None; // destroyed: restart the search next sweep
        *game_running = false;
        return false;
    }
    if (attributes.map_state != IsViewable) {
        *game_running = true; // minimized to tray, not closed
        return false;
    }
    Window focused = None;
    int revert_to = RevertToNone;
    XGetInputFocus(g_display, &focused, &revert_to);
    *game_running = true;
    return focused_is_in_game(focused);
}

void set_reported_focus(bool focused) {
    if (focused == g_focused) return;
    g_focused = focused;
    g_message("game watch: game %s", focused ? "focused" : "unfocused");
    if (g_on_change) g_on_change(focused);
}

gboolean on_poll(gpointer) {
    bool game_running = false;
    const bool focused = sweep_focus(&game_running);
    if (focused) {
        g_misses = 0;
        set_reported_focus(true);
    } else if (g_focused && ++g_misses >= kHideDebounceMisses) {
        g_misses = 0;
        set_reported_focus(false);
    }
    return G_SOURCE_CONTINUE;
}

} // anonymous namespace

namespace platform {

bool game_watch_start(std::function<void(bool)> on_change) {
    if (g_poll_source != 0) return true; // already running

    g_display = XOpenDisplay(nullptr);
    if (g_display == nullptr) {
        g_warning("game watch: cannot open the X11 display; overlay "
                  "stays always-visible (game must run under XWayland)");
        return false;
    }
    // Destroyed-window queries must not kill the app (see file header).
    XSetErrorHandler(ignore_x_error);

    g_root = DefaultRootWindow(g_display);
    g_on_change = std::move(on_change);

    // Initial synchronous sweep: the app reads the result through
    // game_has_focus_now() to set starting visibility before the
    // windows are presented, so there is no flash of a visible
    // overlay over the desktop.
    bool game_running = false;
    g_focused = sweep_focus(&game_running);

    g_poll_source = g_timeout_add(kPollIntervalMs, on_poll, nullptr);
    return true;
}

bool game_has_focus_now() {
    return g_focused;
}

void game_watch_stop() {
    if (g_poll_source == 0) return;
    g_source_remove(g_poll_source);
    g_poll_source = 0;
    g_on_change = nullptr;
    g_game_window = None;
    g_focused = false;
    g_misses = 0;
    if (g_display != nullptr) {
        XCloseDisplay(g_display);
        g_display = nullptr;
    }
}

} // namespace platform
