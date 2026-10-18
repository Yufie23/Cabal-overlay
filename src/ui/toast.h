// ─────────────────────────────────────────────────────────────
// toast.h — small always-on-top notification surface (Windows)
//
// GNotification has no working backend on Windows (the IToast backend
// is too new and unreliable under Wine), so every notification we
// send is invisible there. This is the replacement: a third overlay
// surface, top-center, with title + body and a 4-second lifetime.
// It reuses the same platform::overlay contract as the bar and the
// goals panel, so no new Win32 code is needed.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <string>

#include <gtk/gtk.h>

namespace toast {

// Shows a toast with `title` and `body` for ~4 seconds (then it
// fades and destroys itself). Safe to call from any GLib callback;
// multiple toasts replace each other rather than stacking.
void show(GtkApplication* app, const std::string& title,
          const std::string& body);

} // namespace toast
