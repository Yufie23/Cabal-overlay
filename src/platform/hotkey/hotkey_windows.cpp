// ─────────────────────────────────────────────────────────────
// hotkey_windows.cpp — global hotkey via RegisterHotKey AND
// GetAsyncKeyState polling
//
// Two delivery paths for one combo, because fullscreen Cabal kills
// the first: DirectInput's exclusive keyboard grab suppresses the
// whole window-message path (the same reason the Win key and
// Ctrl+Shift+Esc die in game), and RegisterHotKey rides that path.
//
//   RegisterHotKey   → fast path, works while the game is windowed
//                      or another app holds focus.
//   GetAsyncKeyState → polls the driver-level key state table, which
//                      an exclusive DirectInput grab CANNOT suppress
//                      (the game itself reads from there). A GLib
//                      timer checks the combo every 50 ms and fires
//                      on the press edge. Same API family the
//                      dgcheck pointer sensor already uses.
//
// Both share one fire() with a 400 ms cooldown: when the message
// path IS alive (game windowed, desktop focused) one physical press
// can arrive twice, and a double toggle cancels itself out.
//
// Anti-cheat posture: no hooks, no injection, no synthetic input —
// nothing macro-shaped for XIGNCODE3 to look at.
//
// WM_HOTKEY arrives as a window message, so a message-only window
// (HWND_MESSAGE: invisible, no taskbar entry, purely a message
// sink) is created to receive it. Its WndProc runs on the GTK main
// thread — the message pump IS the main loop — so the trigger
// callback can be invoked directly, exactly like a GLib callback.
//
// On Linux the config's HotkeyMode picks between D-Bus (external),
// evdev and nothing; on Windows "external" has no meaning (no
// D-Bus), so the app calls hotkey_start for any mode except
// Disabled — the combo string is read identically in both cases.
// ─────────────────────────────────────────────────────────────

#include "platform/hotkey/hotkey.h"

#include <glib.h>

#include <windows.h>

#include <cctype>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

// What to hold + what to press, in Win32 terms.
struct Combo {
    unsigned modifiers = 0; // MOD_SHIFT / MOD_CONTROL / MOD_ALT
    int virtual_key = 0;    // VK_*
};

Combo g_combo;
std::function<void()> g_on_trigger;
HWND g_sink = nullptr;       // message-only window receiving WM_HOTKEY
constexpr UINT kHotkeyId = 1;
guint g_poll_source = 0;     // GetAsyncKeyState timer
bool g_poll_was_down = false;
std::chrono::steady_clock::time_point g_last_fire;

// Both delivery paths converge here. When the message path is alive
// one physical press can arrive twice (WM_HOTKEY + poll edge), and a
// double toggle cancels itself — the cooldown collapses duplicates.
void fire() {
    const auto now = std::chrono::steady_clock::now();
    if (now - g_last_fire < std::chrono::milliseconds(400)) return;
    g_last_fire = now;
    if (g_on_trigger) g_on_trigger();
}

// Polls the driver-level key state: GetAsyncKeyState reflects the
// physical key even while a fullscreen game eats the message path
// (DirectInput exclusive grabs cannot suppress the state table).
// Fires on the press edge only; the key must fully release before
// the combo can trigger again.
gboolean on_poll(gpointer) {
    const bool key_down =
        (GetAsyncKeyState(g_combo.virtual_key) & 0x8000) != 0;
    const bool shift_ok =
        (g_combo.modifiers & MOD_SHIFT) == 0 ||
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool ctrl_ok =
        (g_combo.modifiers & MOD_CONTROL) == 0 ||
        (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt_ok =
        (g_combo.modifiers & MOD_ALT) == 0 ||
        (GetAsyncKeyState(VK_MENU) & 0x8000) != 0; // VK_MENU = Alt
    const bool down = key_down && shift_ok && ctrl_ok && alt_ok;
    if (down && !g_poll_was_down) fire();
    g_poll_was_down = down;
    return G_SOURCE_CONTINUE;
}

std::string lower(std::string text) {
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// "space" → VK_SPACE, "f9" → VK_F9, "p" → 'P'. Returns 0 for
// anything unmapped — callers fail loudly instead of registering a
// combo that can never fire.
int lookup_key(const std::string& name) {
    static const std::pair<const char*, int> kNamedKeys[] = {
        {"space", VK_SPACE},
        {"tab",   VK_TAB},
        {"esc",   VK_ESCAPE},
        {"enter", VK_RETURN},
        {"up",    VK_UP},
        {"down",  VK_DOWN},
        {"left",  VK_LEFT},
        {"right", VK_RIGHT},
    };
    for (const auto& [text, code] : kNamedKeys)
        if (name == text) return code;

    if (name.size() == 1) {
        const char c = name[0];
        if (c >= 'a' && c <= 'z') return 'A' + (c - 'a');
        if (c >= '0' && c <= '9') return '0' + (c - '0');
        return 0;
    }

    // F1-F12: 'f' followed by digits.
    if (name.size() >= 2 && name[0] == 'f') {
        const int n = std::stoi(name.substr(1)); // throws on garbage
        if (n >= 1 && n <= 12) return VK_F1 + (n - 1);
    }
    return 0;
}

// "Shift+Space" → {modifiers: MOD_SHIFT, virtual_key: VK_SPACE}.
// Throws std::runtime_error on a malformed combo.
Combo parse_combo(const std::string& text) {
    Combo combo;
    std::size_t start = 0;
    while (true) {
        const std::size_t plus = text.find('+', start);
        const std::string part =
            lower(text.substr(start, plus == std::string::npos
                                       ? std::string::npos
                                       : plus - start));
        // Trim spaces around each part ("Shift + Space").
        const std::size_t begin = part.find_first_not_of(' ');
        const std::size_t end = part.find_last_not_of(' ');
        if (begin == std::string::npos)
            throw std::runtime_error("empty part in combo: '" + text + "'");
        const std::string token = part.substr(begin, end - begin + 1);

        if (token == "shift") combo.modifiers |= MOD_SHIFT;
        else if (token == "ctrl" || token == "control")
            combo.modifiers |= MOD_CONTROL;
        else if (token == "alt") combo.modifiers |= MOD_ALT;
        else if (combo.virtual_key == 0)
            combo.virtual_key = lookup_key(token);
        else
            throw std::runtime_error(
                "combo has more than one plain key: '" + text + "'");

        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    if (combo.virtual_key == 0)
        throw std::runtime_error("combo has no key: '" + text + "'");
    return combo;
}

LRESULT CALLBACK sink_wnd_proc(HWND window, UINT message,
                               WPARAM wparam, LPARAM lparam) {
    if (message == WM_HOTKEY && wparam == kHotkeyId) fire();
    return DefWindowProcW(window, message, wparam, lparam);
}

// Message-only window: a WndProc target for WM_HOTKEY without any
// visible UI. Created on the GTK main thread (hotkey_start runs
// there), so no cross-thread concerns.
HWND create_sink_window() {
    WNDCLASSW sink_class {};
    sink_class.lpfnWndProc = sink_wnd_proc;
    sink_class.hInstance = GetModuleHandleW(nullptr);
    sink_class.lpszClassName = L"cabal-overlay-hotkey-sink";
    if (RegisterClassW(&sink_class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return nullptr;
    return CreateWindowExW(0, sink_class.lpszClassName,
                           L"cabal-overlay-hotkey-sink", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, nullptr, sink_class.hInstance,
                           nullptr);
}

} // anonymous namespace

namespace platform {

bool hotkey_start(const std::string& combo_text,
                  std::function<void()> on_trigger) {
    try {
        g_combo = parse_combo(combo_text);
    } catch (const std::exception& error) {
        g_warning("hotkey: %s", error.what());
        return false;
    }
    g_on_trigger = std::move(on_trigger);

    // Path 1: RegisterHotKey (message path — fast when it works).
    bool registered = false;
    g_sink = create_sink_window();
    if (g_sink == nullptr) {
        g_warning("hotkey: could not create the message sink window "
                  "(error %lu)", GetLastError());
    } else if (RegisterHotKey(g_sink, kHotkeyId, g_combo.modifiers,
                              g_combo.virtual_key) == 0) {
        g_warning("hotkey: RegisterHotKey failed for '%s' (error %lu) — "
                  "the combo is probably taken by another app",
                  combo_text.c_str(), GetLastError());
    } else {
        registered = true;
    }

    // Path 2: async poll (driver state — survives fullscreen grabs).
    // Started even when path 1 failed: it is a complete fallback.
    g_poll_source = g_timeout_add(50, on_poll, nullptr);

    g_message("hotkey: '%s' active (register: %s, poll: yes)",
              combo_text.c_str(), registered ? "yes" : "no");
    return true;
}

void hotkey_stop() {
    if (g_poll_source != 0) {
        g_source_remove(g_poll_source);
        g_poll_source = 0;
    }
    if (g_sink != nullptr) {
        UnregisterHotKey(g_sink, kHotkeyId);
        DestroyWindow(g_sink);
        g_sink = nullptr;
    }
    g_on_trigger = nullptr;
}

} // namespace platform
