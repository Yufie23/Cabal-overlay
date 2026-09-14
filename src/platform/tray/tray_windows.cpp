// ─────────────────────────────────────────────────────────────
// tray_windows.cpp — system tray icon via Shell_NotifyIconW
//
// A message-only window (HWND_MESSAGE, like the hotkey sink) receives
// the icon's mouse events: left double-click opens the settings
// window, right-click raises a popup menu (Open settings / Quit).
// Messages are pumped by GTK's own Win32 main loop on the main
// thread, so the actions can touch GTK widgets directly.
//
// The icon handle is the stock application icon: the exe carries no
// embedded resources yet (no .rc file — adding one later swaps one
// line here). Explorer-restart resilience: the "TaskbarCreated"
// broadcast is re-registered and re-adds the icon.
// ─────────────────────────────────────────────────────────────

#include "platform/tray/tray.h"

#include <glib.h>

#include <windows.h>
#include <shellapi.h>

#include <cwchar>
#include <iterator>
#include <utility>

namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kIconId = 1;
constexpr UINT kCmdSettings = 1;
constexpr UINT kCmdQuit = 2;

HWND g_sink = nullptr;
bool g_icon_added = false;
UINT g_taskbar_created = 0; // broadcast message id, set at start
platform::TrayActions g_actions;

void add_icon() {
    NOTIFYICONDATAW icon {};
    icon.cbSize = sizeof(icon);
    icon.hWnd = g_sink;
    icon.uID = kIconId;
    icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    icon.uCallbackMessage = kTrayMessage;
    icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION); // stock icon for now
    wcsncpy(icon.szTip, L"Cabal Overlay — right-click for options",
            std::size(icon.szTip) - 1);
    icon.szTip[std::size(icon.szTip) - 1] = L'\0'; // wcsncpy may not terminate
    if (Shell_NotifyIconW(NIM_ADD, &icon) != FALSE) g_icon_added = true;
}

void show_menu() {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    AppendMenuW(menu, MF_STRING, kCmdSettings, L"Open settings");
    AppendMenuW(menu, MF_STRING, kCmdQuit, L"Quit");
    POINT cursor;
    GetCursorPos(&cursor);
    // Without SetForegroundWindow the menu does not dismiss on an
    // outside click — documented TrackPopupMenu quirk.
    SetForegroundWindow(g_sink);
    const UINT picked = TrackPopupMenuEx(
        menu,
        TPM_NONOTIFY | TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_RIGHTBUTTON,
        cursor.x, cursor.y, g_sink, nullptr);
    DestroyMenu(menu);
    if (picked == kCmdSettings && g_actions.on_show_settings)
        g_actions.on_show_settings();
    else if (picked == kCmdQuit && g_actions.on_quit)
        g_actions.on_quit();
}

LRESULT CALLBACK tray_wnd_proc(HWND window, UINT message,
                               WPARAM wparam, LPARAM lparam) {
    // Explorer restarted (crash, update): the tray is brand new and
    // ours must be re-added.
    if (message == g_taskbar_created && g_icon_added) {
        g_icon_added = false;
        add_icon();
        return 0;
    }
    if (message == kTrayMessage) {
        if (lparam == WM_RBUTTONUP) { show_menu(); return 0; }
        if (lparam == WM_LBUTTONDBLCLK) {
            if (g_actions.on_show_settings) g_actions.on_show_settings();
            return 0;
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

// Message-only window: a WndProc target for the tray callbacks with
// no visible UI. Created on the GTK main thread (tray_start runs
// there), so no cross-thread concerns.
HWND create_sink_window() {
    WNDCLASSW sink_class {};
    sink_class.lpfnWndProc = tray_wnd_proc;
    sink_class.hInstance = GetModuleHandleW(nullptr);
    sink_class.lpszClassName = L"cabal-overlay-tray-sink";
    if (RegisterClassW(&sink_class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return nullptr;
    return CreateWindowExW(0, sink_class.lpszClassName,
                           L"cabal-overlay-tray-sink", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, nullptr, sink_class.hInstance,
                           nullptr);
}

} // anonymous namespace

namespace platform {

bool tray_start(TrayActions actions) {
    if (g_sink != nullptr) return true; // already running

    g_actions = std::move(actions);
    g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");

    g_sink = create_sink_window();
    if (g_sink == nullptr) {
        g_warning("tray: could not create the sink window (error %lu)",
                  GetLastError());
        return false;
    }
    add_icon();
    if (!g_icon_added) {
        g_warning("tray: Shell_NotifyIconW refused the icon");
        DestroyWindow(g_sink);
        g_sink = nullptr;
        return false;
    }
    g_message("tray: icon added (right-click: settings / quit)");
    return true;
}

void tray_stop() {
    if (g_icon_added) {
        NOTIFYICONDATAW icon {};
        icon.cbSize = sizeof(icon);
        icon.hWnd = g_sink;
        icon.uID = kIconId;
        Shell_NotifyIconW(NIM_DELETE, &icon);
        g_icon_added = false;
    }
    if (g_sink != nullptr) {
        DestroyWindow(g_sink);
        g_sink = nullptr;
    }
    g_actions = {};
}

} // namespace platform
