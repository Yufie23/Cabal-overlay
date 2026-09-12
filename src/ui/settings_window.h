// ─────────────────────────────────────────────────────────────
// settings_window.h — the in-app settings editor
//
// Presents a plain native window (NOT a layer-shell surface: it must
// take focus, accept typing and show in the taskbar) with one tab
// per config section. Controls are populated from a config snapshot
// and every change writes straight back to the TOML through
// set_config_value — the app's existing file monitor then applies it
// live. There is no Apply button: the file IS the state.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <string>

#include <gtk/gtk.h>

#include "app/config.h"

namespace settings {

// Shows the singleton settings window (creating it on first call),
// populated from `config`. `config_path` is the TOML the controls
// write to. The snapshot is taken at creation time: hand-edits made
// while the window is open are not reflected until it reopens.
void present(GtkApplication* app, const AppConfig& config,
             const std::string& config_path);

} // namespace settings
