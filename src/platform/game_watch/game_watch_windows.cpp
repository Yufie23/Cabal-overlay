// ─────────────────────────────────────────────────────────────
// game_watch_windows.cpp — "is the game the foreground window?"
//
// Win32 equivalent of game_watch_x11.cpp. Where the X11 backend
// asks the X server for the input focus, here the question is
// direct: GetForegroundWindow() returns the single HWND with
// keyboard focus across the whole desktop.
//
// Finding the game window, two passes over EnumWindows, most precise
// first:
//
//   1. Process name: QueryFullProcessImageNameW on the window's owner
//      PID, basename contains "cabalmain" (survives any custom window
//      title the server launcher sets — the Wine WM_CLASS trick from
//      the X11 backend has no Win32 equivalent, this is it).
//   2. Title contains "cabal" (case-insensitive): fallback for
//      renamed executables.
//
// The search re-runs on every poll while unknown, so starting the
// overlay before the game is fine.
//
// Hiding is debounced (~3 polls) like the X11 backend: transient
// focus flickers (alt-tab previews, taskbar peeks) must not strobe
// the overlay. Gaining focus reports immediately. "Game not found"
// reports VISIBLE (see on_poll): an overlay hidden by a broken
// detector is an unclosable process.
// ─────────────────────────────────────────────────────────────

#include "platform/game_watch/game_watch.h"

#include <glib.h>

#include <windows.h>

#include <cwctype>
#include <iterator>
#include <string_view>
#include <utility>

namespace {

constexpr guint kPollIntervalMs = 250;
constexpr int kHideDebounceMisses = 3; // ~750 ms unfocused before hiding

HWND g_game_window = nullptr; // NULL until the game window is found
guint g_poll_source = 0;
std::function<void(bool)> g_on_change;
bool g_focused = false; // last REPORTED state
int g_misses = 0;       // consecutive unfocused polls while visible

bool contains_nocase(std::wstring_view haystack, std::wstring_view needle) {
    if (needle.empty() || haystack.size() < needle.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (std::towlower(haystack[i + j]) != needle[j]) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// Does this window belong to the game PROCESS? Asking the owning
// process for its image name is far more precise than the window
// title: a browser tab titled "Cabal Online forum" is not the game.
bool window_belongs_to_game(HWND window) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == 0) return false;
    HANDLE process =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) return false;
    wchar_t path[MAX_PATH] = {};
    DWORD size = static_cast<DWORD>(std::size(path));
    bool match = false;
    if (QueryFullProcessImageNameW(process, 0, path, &size) != 0) {
        const std::wstring_view full(path);
        const std::size_t sep = full.find_last_of(L"\\/");
        const std::wstring_view basename =
            full.substr(sep == std::wstring_view::npos ? 0 : sep + 1);
        match = contains_nocase(basename, L"cabalmain");
    }
    CloseHandle(process);
    return match;
}

// EnumWindows callback. Matches by process name (pass 1) or by title
// (pass 2, `by_title`); keeps the first hit and stops.
struct FindContext {
    HWND result = nullptr;
    bool by_title = false;
};

BOOL CALLBACK on_enum_window(HWND window, LPARAM context_ptr) {
    // Contract: LPARAM carries a FindContext out-param address.
    auto* context = reinterpret_cast<FindContext*>(context_ptr);
    if (!IsWindowVisible(window)) return TRUE;

    if (!context->by_title) {
        if (window_belongs_to_game(window)) {
            context->result = window;
            return FALSE;
        }
        return TRUE;
    }
    wchar_t title[256] = {};
    const int length = GetWindowTextLengthW(window);
    if (length <= 0 || length >= static_cast<int>(std::size(title)))
        return TRUE;
    GetWindowTextW(window, title, static_cast<int>(std::size(title)));
    if (contains_nocase(title, L"cabal")) {
        context->result = window;
        return FALSE;
    }
    return TRUE;
}

// Scans top-level windows for the game. Re-run on every poll while
// the handle is unknown: the overlay may start before the game.
HWND find_game_window() {
    FindContext process_pass;
    EnumWindows(on_enum_window,
                reinterpret_cast<LPARAM>(&process_pass)); // contract: out-param through LPARAM
    if (process_pass.result != nullptr) return process_pass.result;

    FindContext title_pass { .by_title = true };
    EnumWindows(on_enum_window,
                reinterpret_cast<LPARAM>(&title_pass)); // contract: out-param through LPARAM
    return title_pass.result;
}

// One focus sweep. Out: whether the game window exists right now.
// Returns: the game window is the foreground window.
bool sweep_focus(bool* game_running) {
    if (g_game_window == nullptr || !IsWindow(g_game_window))
        g_game_window = find_game_window();
    if (g_game_window == nullptr) {
        *game_running = false;
        return false;
    }
    if (!IsWindowVisible(g_game_window)) {
        *game_running = true; // minimized to tray, not closed
        return false;
    }
    *game_running = true;
    return GetForegroundWindow() == g_game_window;
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
    // "Game not found" reports VISIBLE, on purpose: when we cannot
    // see the game we cannot know what the user is doing, and an
    // overlay hidden by a broken watcher is an unclosable process.
    if (focused || !game_running) {
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

    g_on_change = std::move(on_change);

    // Initial synchronous sweep: the app reads the result through
    // game_has_focus_now() to set starting visibility before the
    // windows are presented, so there is no flash of a visible
    // overlay over the desktop. "Game not found" means visible (see
    // on_poll): starting before the game does is the normal case.
    bool game_running = false;
    const bool focused = sweep_focus(&game_running);
    g_focused = focused || !game_running;

    g_poll_source = g_timeout_add(kPollIntervalMs, on_poll, nullptr);
    return true; // no display to open: the API is always available
}

bool game_has_focus_now() {
    return g_focused;
}

void game_watch_stop() {
    if (g_poll_source == 0) return;
    g_source_remove(g_poll_source);
    g_poll_source = 0;
    g_on_change = nullptr;
    g_game_window = nullptr;
    g_focused = false;
    g_misses = 0;
}

} // namespace platform
