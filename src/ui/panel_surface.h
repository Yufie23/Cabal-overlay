// ─────────────────────────────────────────────────────────────
// panel_surface.h — the goals panel WINDOW assembly
//
// Extracted from main.cpp: everything about constructing the panel
// surface — the preset switcher, the display-toggle buttons, the
// task-row box, the add-task form and the footer — lives here.
// main.cpp keeps the STATE and the action routing; this module only
// assembles widgets and raises callbacks. The widgets themselves
// stay in TU-local globals (same pattern as before the extraction),
// exposed through the accessors below.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "model/dungeons.h"
#include "model/task_presets.h"
#include "platform/overlay/overlay.h"
#include "ui/goals_panel.h"

namespace panel_surface {

// Everything the surface needs from the app, injected once at build.
// Pointers address stable app-owned storage (they outlive the panel);
// the std::functions are the surface's only way back into app logic.
struct PanelCallbacks {
    GoalsActions actions;                      // task action routing
    const std::vector<Dungeon>* dungeons;      // add-form catalog
    const std::vector<TaskPreset>* presets;    // switcher entries
    const std::string* active_list;            // switcher initial selection
    platform::Placement initial_placement;     // where the surface floats

    std::function<void(const std::string& list_id)> on_list_selected;
    std::function<void()> on_names_toggled;    // "Aa" header button
    std::function<void()> on_collapse_toggled; // "▾" header button
    std::function<void()> on_open_settings;
    std::function<void()> on_open_tutorial;
    std::function<void(int margin_x, int margin_y)> on_dragged;
};

// Builds and presents the surface. The caller fills the rows after
// this returns (the row box is empty until the first refresh).
GtkWindow* build(GtkApplication* app, const PanelCallbacks& callbacks);

bool exists();
GtkWindow* window();          // nullptr until build / after destroy
GtkWidget* goals_box();       // refresh target for the task rows
GtkWidget* goals_form();      // visibility follows preset/custom
GtkWidget* add_task_button(); // hidden while a preset is active

// Tears the surface down and nulls every global. Safe when not built.
void destroy();

} // namespace panel_surface
