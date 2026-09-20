// ─────────────────────────────────────────────────────────────
// cabal-overlay — entry point
//
// A click-through, always-on-top overlay floating above every window
// (including the game), built from two surfaces:
//
//   1. The bar (bottom-left): local clock + countdowns to the next
//      scheduled game events.
//   2. The goals panel (right edge, vertically centered): tracked
//      dungeon tasks with counters and progress bars.
//
// Controls:
//   - Default mode is CLICK-THROUGH: clicks fall through to the game.
//   - The "toggle-interactive" D-Bus action flips to clickable mode
//     (bind it to a KDE custom shortcut). While interactive, clicking
//     the bar returns to click-through mode.
//   - Close: the "Quit" button in the settings window, the tray icon
//     menu (Windows), the quit D-Bus action, or from a terminal:
//     pkill cabal-overlay
//
// This file only orchestrates: it loads the config, wires GTK
// signals, the D-Bus action and the 1-second timer. Time logic lives
// in clock.*, schedule math in schedule.*, config parsing in
// config.*, task data in tasks./state., panel rendering in ui.*,
// and everything Wayland-specific behind platform/.
// ─────────────────────────────────────────────────────────────

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#ifdef _WIN32
#include <cstdlib>      // std::getenv (APPDATA)
#include <filesystem>   // per-user config path (see kConfigPath)
#endif

#include <gtk/gtk.h>

#include "app/alarms.h"
#include "app/dgcheck.h"
#include "time/clock.h"
#include "app/config.h"
#include "model/dungeons.h"
#include "platform/hotkey/hotkey.h"
#include "platform/game_watch/game_watch.h"
#include "platform/overlay/overlay.h"
#include "platform/pointer/pointer.h"
#include "platform/sound/sound.h"
#include "platform/tray/tray.h"
#include "time/schedule.h"
#include "model/state.h"
#include "model/task_presets.h"
#include "model/tasks.h"
#include "ui/goals_panel.h"
#include "ui/settings_window.h"

namespace {

constexpr char kApplicationId[] = "dev.cabal.Overlay";

// Where the config lives. On Linux it ships in the working
// directory (the repo layout). On Windows a portable exe cannot
// write its own directory reliably, so the config goes to the
// per-user %APPDATA%\cabal-overlay\, seeded from the defaults that
// ship next to the exe on the first run. Both platforms expose the
// same type so the call sites stay identical.
#ifdef _WIN32
std::string app_data_file(const char* name) {
    const char* appdata = std::getenv("APPDATA");
    if (appdata == nullptr) return name; // last resort: cwd
    const std::filesystem::path target =
        std::filesystem::path{appdata} / "cabal-overlay" / name;
    // First run: nothing to edit yet — copy the shipped defaults
    // (config\overlay.toml next to the exe) into the profile.
    std::error_code error;
    std::filesystem::create_directories(target.parent_path(), error);
    const std::filesystem::path shipped =
        std::filesystem::path{"config"} / name;
    if (!std::filesystem::exists(target) &&
        std::filesystem::exists(shipped))
        std::filesystem::copy_file(shipped, target, error);
    return target.string();
}
const std::string kConfigPath = app_data_file("overlay.toml");
constexpr char kDungeonsPath[] = "data\\dungeons.json";
constexpr char kTaskListsPath[] = "data\\task_lists.json";
#else
const std::string kConfigPath = "config/overlay.toml";
constexpr char kDungeonsPath[] = "data/dungeons.json";
constexpr char kTaskListsPath[] = "data/task_lists.json";
#endif

// ── Module state ─────────────────────────────────────────────
// Two windows, one mode flag, the current config and the config
// file monitor — all owned by the single GtkApplication instance.
// GLib is single-threaded like Node: callbacks (timer, monitor,
// signals) never run concurrently, so no locks are needed.
GtkWindow*    g_window = nullptr;
// Second surface: the goals panel. Created when [panel] visible=true.
// Three layers, top to bottom: g_goals_box (task rows, rebuilt on
// every change), the "add task" toggle button, and the form itself
// — the last two live OUTSIDE the rebuildable box so an open form
// survives row refreshes.
GtkWindow*    g_panel_window = nullptr;
GtkWidget*    g_goals_panel = nullptr;  // styled outer box
GtkWidget*    g_goals_box = nullptr;    // rebuilt task rows
GtkWidget*    g_goals_form = nullptr;   // collapsible add-task form
GoalsActions  g_actions;
bool          g_interactive = false;
AppConfig     g_config;
GFileMonitor* g_config_monitor = nullptr;
AppState      g_state;
std::filesystem::path g_state_path;
std::vector<Dungeon>  g_dungeons;
// Preset task lists (data/task_lists.json) and their runtime state
// (presets.json — deliberately separate from the tracker-schema
// state.json). See model/task_presets.h.
std::vector<TaskPreset> g_presets;
PresetState   g_preset_state;
std::filesystem::path g_presets_path;
// The task list the panel currently shows: a copy of g_state.tasks
// for "custom", or the materialized view of the active preset. Stable
// storage — only its contents are rebuilt per refresh — because panel
// callbacks and the dgcheck counter hold pointers into it.
TaskList      g_active_tasks;
GtkWidget*    g_list_dropdown = nullptr;   // preset switcher (panel header)
GtkWidget*    g_add_task_button = nullptr; // hidden while a preset is active
// Remembers which event occurrences already got their warning.
AlarmTracker  g_alarms;
// The GtkApplication, kept for sending GNotifications from the
// alarm callback (lives as long as the process).
GApplication* g_app = nullptr;
// When the state file exists but is unreadable we keep running with
// an in-memory copy — and refuse to SAVE, so a corrupted file is
// never overwritten by accident.
bool          g_state_writable = true;
// Fingerprint of the task list as last rendered in the panel. The
// tick compares against goals_signature() and only rebuilds the
// panel widgets when a task actually changed.
std::string   g_goals_signature;
// Armed by the "calibrate-zone" action: the next primary click is
// not a dungeon clear but the user showing us WHERE the dialog
// button sits — it becomes the new zone center instead of a bump.
bool          g_calibrating_click = false;
// Game-focus visibility (platform/game_watch.h). The overlay floats
// on the compositor's OVERLAY layer — above EVERYTHING, alt-tab
// included — so when the game loses the input focus we hide both
// surfaces instead of drawing over the desktop.
bool          g_game_watch_active = false;
bool          g_game_focused = false;

// Maps a config section onto the platform Placement struct. Two
// plain overloads (one per config type) instead of a template: three
// lines each, and the compiler type-checks every field. The platform
// contract stays config-agnostic; the conversion happens here, once.
platform::Placement placement_from(const OverlayConfig& overlay) {
    return { .anchor = overlay.anchor, .margin_x = overlay.margin_x,
             .margin_y = overlay.margin_y, .opacity = overlay.opacity };
}

platform::Placement placement_from(const PanelConfig& panel) {
    return { .anchor = panel.anchor, .margin_x = panel.margin_x,
             .margin_y = panel.margin_y, .opacity = panel.opacity };
}

// Persists the state; errors are logged, never fatal (losing a
// save must not kill the overlay mid-game).
void persist_state() {
    if (!g_state_writable) return;
    try {
        save_state(g_state_path, g_state);
    } catch (const std::exception& error) {
        g_warning("could not save state: %s", error.what());
    }
}

void persist_preset_state() {
    try {
        save_preset_state(g_presets_path, g_preset_state);
    } catch (const std::exception& error) {
        g_warning("could not save presets: %s", error.what());
    }
}

// ── Preset lists ───────────────────────────────────────────────
// The panel shows ONE source at a time: "custom" (g_state.tasks, the
// classic free-form list) or a preset template from
// data/task_lists.json. Actions route by the active source so the
// view layer never learns the difference.

TaskPreset* find_preset(const std::string& id) {
    for (TaskPreset& preset : g_presets)
        if (preset.id == id) return &preset;
    return nullptr;
}

bool preset_active() {
    return find_preset(g_preset_state.active_list) != nullptr;
}

// Preset row ids are "p:<list id>:<task name>" — stable across
// refreshes so progress, ghosts and drag-and-drop all resolve.
std::string preset_task_name(const TaskPreset& preset,
                             const std::string& id) {
    const std::string prefix = "p:" + preset.id + ":";
    if (id.starts_with(prefix)) return id.substr(prefix.size());
    return id;
}

// Rebuilds g_active_tasks from the active source: a copy of the
// custom list, or the preset definition plus the user's progress and
// order override. Template edits are tolerated both ways: names the
// override does not know are appended, names the template dropped
// are skipped.
void materialize_active_tasks() {
    TaskPreset* preset = find_preset(g_preset_state.active_list);
    if (preset == nullptr) {
        g_active_tasks = g_state.tasks;
        return;
    }

    TaskList list;
    PresetListProgress& progress = g_preset_state.lists[preset->id];
    std::vector<std::string> order = progress.order;
    for (const PresetTask& task : preset->tasks)
        if (std::find(order.begin(), order.end(), task.name) == order.end())
            order.push_back(task.name);

    for (const std::string& name : order) {
        const PresetTask* def = nullptr;
        for (const PresetTask& task : preset->tasks)
            if (task.name == name) { def = &task; break; }
        if (def == nullptr) continue; // removed from the template since

        Task task;
        task.id = "p:" + preset->id + ":" + def->name;
        task.type = def->type;
        task.name = def->name;
        task.goal = def->goal;
        task.locked = true; // template content: no remove button
        if (auto it = progress.counts.find(name); it != progress.counts.end())
            task.count = it->second;
        if (auto it = progress.completed.find(name); it != progress.completed.end())
            task.completed = it->second;
        list.all().push_back(std::move(task));
    }
    g_active_tasks = std::move(list);
}

// Rebuilds the task rows and syncs panel visibility. The single
// refresh path used by the tick AND by every user action, so both
// stay consistent. An open add-task form keeps the panel on screen
// even with zero tasks.
void refresh_goals_panel() {
    if (g_goals_box == nullptr || g_panel_window == nullptr) return;
    materialize_active_tasks();
    goals_panel_refresh(g_goals_box, g_active_tasks, g_actions, g_dungeons,
                        g_config.panel.short_names, g_config.panel.collapsed);
    // Preset lists are fixed templates: adding is meaningless there.
    const bool preset = preset_active();
    if (g_add_task_button != nullptr)
        gtk_widget_set_visible(g_add_task_button, !preset);
    if (preset && g_goals_form != nullptr)
        gtk_widget_set_visible(g_goals_form, FALSE);
    const bool visible = !g_active_tasks.all().empty() ||
                         gtk_widget_get_visible(g_goals_form);
    gtk_widget_set_visible(GTK_WIDGET(g_panel_window), visible);
    // A surface that comes back from hidden is clickable by
    // default — re-assert click-through on the panel.
    platform::overlay_set_interactive(g_panel_window, g_interactive);
}

// The actions the panel calls when the user touches it. Each one
// mutates the app state, persists, and refreshes — same shape as an
// HTTP mutation endpoint that re-renders the view after writing.
GoalsActions make_goals_actions() {
    GoalsActions actions;
    actions.bump_count = [](const std::string& id, int delta) {
        if (TaskPreset* preset = find_preset(g_preset_state.active_list)) {
            // Mirror of TaskList::bump_count on the preset's progress:
            // clamp to [0, goal], auto-complete when the goal is met.
            PresetListProgress& progress = g_preset_state.lists[preset->id];
            const std::string name = preset_task_name(*preset, id);
            int goal = 0;
            for (const PresetTask& task : preset->tasks)
                if (task.name == name) goal = task.goal;
            int& count = progress.counts[name];
            count += delta;
            if (count < 0) count = 0;
            if (goal > 0) {
                count = std::min(count, goal);
                progress.completed[name] = count >= goal;
            }
            persist_preset_state();
            refresh_goals_panel();
            return;
        }
        // Reaching the goal sets completed in the model; the panel
        // hides done tasks on the next refresh and the periodic reset
        // brings them back. Nothing is deleted here — dailies repeat
        // every day, their resurrection is the daily reset's job.
        g_state.tasks.bump_count(id, delta);
        persist_state();
        refresh_goals_panel();
    };
    actions.set_completed = [](const std::string& id, bool completed) {
        if (TaskPreset* preset = find_preset(g_preset_state.active_list)) {
            g_preset_state.lists[preset->id]
                .completed[preset_task_name(*preset, id)] = completed;
            persist_preset_state();
            refresh_goals_panel();
            return;
        }
        g_state.tasks.set_completed(id, completed);
        persist_state();
        refresh_goals_panel();
    };
    actions.add_task = [](TaskType type, const std::string& name, int goal) {
        if (preset_active()) return; // fixed template: nothing to add
        g_state.tasks.add(type, name, goal);
        persist_state();
        refresh_goals_panel();
    };
    actions.remove_task = [](const std::string& id) {
        if (preset_active()) return; // fixed template: nothing to remove
        g_state.tasks.remove(id);
        persist_state();
        refresh_goals_panel();
    };
    actions.move_task = [](const std::string& id, int delta) {
        if (TaskPreset* preset = find_preset(g_preset_state.active_list)) {
            // Reorder the display-order override with the same slide
            // semantics as TaskList::move (neighbors keep their order).
            PresetListProgress& progress = g_preset_state.lists[preset->id];
            std::vector<std::string> order;
            for (const Task& task : g_active_tasks.all())
                order.push_back(task.name);
            const std::string name = preset_task_name(*preset, id);
            const auto it = std::find(order.begin(), order.end(), name);
            if (it == order.end()) return;
            const std::ptrdiff_t index = std::distance(order.begin(), it);
            const std::ptrdiff_t last =
                static_cast<std::ptrdiff_t>(order.size()) - 1;
            const std::ptrdiff_t target =
                std::clamp(index + static_cast<std::ptrdiff_t>(delta),
                           std::ptrdiff_t{0}, last);
            if (target == index) return;
            if (target < index)
                std::rotate(order.begin() + target, it, it + 1);
            else
                std::rotate(it, it + 1, order.begin() + target + 1);
            progress.order = std::move(order);
            persist_preset_state();
            refresh_goals_panel();
            return;
        }
        g_state.tasks.move(id, delta);
        persist_state();
        refresh_goals_panel();
    };
    return actions;
}

// The full bar text: local clock and next event countdowns, all
// derived from one single clock reading so nothing in the bar can
// disagree with itself. Task progress lives in the goals panel, not
// here — the bar stays short and glanceable.
std::string bar_text() {
    const auto now = std::chrono::system_clock::now();
    return local_clock_text(now) + "   " +
           events_text(g_config.schedules, now, g_config.overlay.max_countdowns);
}

// Persists a dragged position: the anchor becomes "custom" (absolute
// top-left margins) unless it already is, then the margins. Each
// set_config_value rewrites one line and the config monitor applies
// the result — same values, so the surface does not jump.
void save_dragged_placement(const char* section, int margin_x, int margin_y) {
    try {
        const std::string name = section;
        const bool already_custom =
            (name == "overlay" && g_config.overlay.anchor == "custom") ||
            (name == "panel" && g_config.panel.anchor == "custom");
        if (!already_custom)
            set_config_value(kConfigPath, name + ".anchor",
                             std::string("custom"));
        set_config_value(kConfigPath, name + ".margin_x", margin_x);
        set_config_value(kConfigPath, name + ".margin_y", margin_y);
        g_message("%s dragged to custom position (%d, %d)", section,
                  margin_x, margin_y);
    } catch (const std::exception& error) {
        g_warning("could not save dragged position: %s", error.what());
    }
}

// Panel switcher: index 0 is "custom", everything after maps 1:1 to
// g_presets. Persisting is what makes the choice survive restarts.
void on_list_selected(GObject* dropdown, GParamSpec*, gpointer) {
    const guint index = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    std::string id = "custom";
    if (index > 0 && index - 1 < g_presets.size())
        id = g_presets[index - 1].id;
    if (id == g_preset_state.active_list) return;
    g_preset_state.active_list = id;
    persist_preset_state();
    refresh_goals_panel();
}

// Header display toggles: flip the config field optimistically (so
// the rebuild is instant), persist to the TOML, and let the config
// monitor's reload arrive to the same values — idempotent by design.
void flip_panel_toggle(const char* key, bool* field) {
    *field = !*field;
    try {
        set_config_value(kConfigPath, key, *field);
    } catch (const std::exception& error) {
        g_warning("could not write %s: %s", key, error.what());
    }
    refresh_goals_panel();
}

// Builds the whole goals panel surface from the current config:
// window, rows box, add-task form, footer buttons. Called at
// startup and whenever a live reload turns [panel] visible on.
void build_goals_panel(GtkApplication* app) {
    GtkWidget* panel_window = gtk_application_window_new(app);
    g_panel_window = GTK_WINDOW(panel_window);
    platform::overlay_init(g_panel_window);
    // Drag-to-move like the clock bar; a tap on the panel does
    // nothing (unlike the bar, it is not a "done" button).
    platform::overlay_enable_drag(
        g_panel_window,
        [](int margin_x, int margin_y) {
            save_dragged_placement("panel", margin_x, margin_y);
        },
        [] {});

    // Outer styled box; inside it, the task rows live in their
    // own box so rebuilds never touch the form or its toggle.
    g_goals_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(g_goals_panel, "goals-panel");

    // Header row: preset switcher (when presets exist) and the two
    // display toggles — short/full names and collapsed view.
    auto* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    // Preset list switcher: "Custom" is the free-form list from
    // state.json; every other entry is a fixed template from
    // data/task_lists.json. Only built when presets are installed.
    if (!g_presets.empty()) {
        std::vector<const char*> names {"Custom"};
        for (const TaskPreset& preset : g_presets)
            names.push_back(preset.name.c_str()); // g_presets outlives the panel
        names.push_back(nullptr);
        g_list_dropdown = gtk_drop_down_new(
            G_LIST_MODEL(gtk_string_list_new(names.data())), nullptr);
        gtk_widget_add_css_class(g_list_dropdown, "goal-list-switch");
        gtk_widget_set_hexpand(g_list_dropdown, TRUE);
        guint selected = 0;
        for (std::size_t i = 0; i < g_presets.size(); ++i)
            if (g_presets[i].id == g_preset_state.active_list)
                selected = static_cast<guint>(i + 1);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_list_dropdown), selected);
        g_signal_connect(g_list_dropdown, "notify::selected",
                         G_CALLBACK(on_list_selected), nullptr);
        gtk_box_append(GTK_BOX(header), g_list_dropdown);
    }

    auto* names_button = gtk_button_new_with_label("Aa");
    gtk_widget_add_css_class(names_button, "goal-bump");
    gtk_widget_set_tooltip_text(names_button,
                                "Toggle short codes / full names");
    g_signal_connect(names_button, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer) {
                         flip_panel_toggle("panel.short_names",
                                           &g_config.panel.short_names);
                     }), nullptr);
    gtk_box_append(GTK_BOX(header), names_button);

    auto* collapse_button = gtk_button_new_with_label("▾");
    gtk_widget_add_css_class(collapse_button, "goal-bump");
    gtk_widget_set_tooltip_text(collapse_button,
                                "Show only the first 3 tasks / show all");
    g_signal_connect(collapse_button, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer) {
                         flip_panel_toggle("panel.collapsed",
                                           &g_config.panel.collapsed);
                     }), nullptr);
    gtk_box_append(GTK_BOX(header), collapse_button);

    gtk_box_append(GTK_BOX(g_goals_panel), header);

    g_goals_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(g_goals_panel), g_goals_box);

    g_goals_form = goals_add_form_new(g_actions, g_dungeons);

    auto* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    auto* add_task_button = gtk_button_new_with_label("＋ Add task");
    g_add_task_button = add_task_button; // refresh hides it for presets
    gtk_widget_add_css_class(add_task_button, "goal-add-toggle");
    gtk_widget_set_hexpand(add_task_button, TRUE);
    gtk_widget_set_halign(add_task_button, GTK_ALIGN_FILL);
    // Capture-less lambda → plain function pointer, so it can
    // serve as a GTK callback. Reads the global form; no state
    // of its own.
    g_signal_connect(add_task_button, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer) {
                         gtk_widget_set_visible(g_goals_form, TRUE);
                     }), nullptr);

    auto* settings_button = gtk_button_new_with_label("⚙");
    gtk_widget_add_css_class(settings_button, "goal-bump");
    gtk_widget_set_tooltip_text(settings_button, "Open settings");
    g_signal_connect(settings_button, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer) {
                         settings::present(GTK_APPLICATION(g_app),
                                           g_config, kConfigPath);
                     }), nullptr);

    gtk_box_append(GTK_BOX(footer), add_task_button);
    gtk_box_append(GTK_BOX(footer), settings_button);
    gtk_box_append(GTK_BOX(g_goals_panel), footer);
    gtk_box_append(GTK_BOX(g_goals_panel), g_goals_form);

    gtk_window_set_child(g_panel_window, g_goals_panel);
    platform::overlay_apply_placement(g_panel_window,
                                      placement_from(g_config.panel));

    // Initial fill; row visibility is refresh_goals_panel()'s job.
    materialize_active_tasks();
    g_goals_signature = goals_signature(g_active_tasks,
                                        g_config.panel.short_names,
                                        g_config.panel.collapsed);
    refresh_goals_panel();
    gtk_window_present(g_panel_window);
}

// Brings the panel surface in line with [panel] visible: builds it
// when the config enables the panel and it does not exist yet, and
// destroys it when the config disables it. Every consumer of these
// globals already nullptr-checks, so tearing the window down is safe.
void sync_goals_panel(GtkApplication* app) {
    if (g_config.panel.visible && g_panel_window == nullptr) {
        build_goals_panel(app);
    } else if (!g_config.panel.visible && g_panel_window != nullptr) {
        gtk_window_destroy(g_panel_window);
        g_panel_window = nullptr;
        g_goals_panel = nullptr;
        g_goals_box = nullptr;
        g_goals_form = nullptr;
    }
}

// The platform kept the surface in place when an anchor switched
// from "custom" to an edge by re-basing the margins; mirror that into
// the TOML so the file describes the screen. The extra reload this
// write triggers is a no-op (the values now match).
void persist_adjusted_margins(const std::string& section,
                              const platform::Placement& requested,
                              const platform::Placement& applied) {
    if (applied.margin_x == requested.margin_x &&
        applied.margin_y == requested.margin_y)
        return;
    try {
        set_config_value(kConfigPath, section + ".margin_x", applied.margin_x);
        set_config_value(kConfigPath, section + ".margin_y", applied.margin_y);
        g_message("%s margins re-based for anchor \"%s\": (%d, %d)",
                  section.c_str(), applied.anchor.c_str(), applied.margin_x,
                  applied.margin_y);
    } catch (const std::exception& error) {
        g_warning("could not persist adjusted margins: %s", error.what());
    }
}

// Defined below, next to set_interactive; reload_config needs them
// to start/stop the watcher and re-apply visibility when the config
// toggles them.
void sync_game_watch();
void apply_overlay_visibility();

// Live config reload: the file watcher calls this whenever the
// TOML changes. Unlike startup, a broken file here is NOT fatal:
// we log the error and keep the previous working config (the bar
// keeps showing the last good state).
void reload_config() {
    try {
        g_config = load_config(kConfigPath);
        const auto bar = placement_from(g_config.overlay);
        persist_adjusted_margins(
            "overlay", bar, platform::overlay_apply_placement(g_window, bar));
        // Create/destroy the panel surface as [panel] visible demands;
        // a just-built panel already got its placement in build().
        sync_goals_panel(gtk_window_get_application(g_window));
        if (g_panel_window != nullptr) {
            const auto panel = placement_from(g_config.panel);
            persist_adjusted_margins(
                "panel", panel,
                platform::overlay_apply_placement(g_panel_window, panel));
        }
        g_message("config reloaded from %s", kConfigPath.c_str());
        sync_game_watch();
        apply_overlay_visibility();
    } catch (const std::exception& error) {
        g_warning("config reload failed, keeping previous config: %s",
                  error.what());
    }
}

// GFileMonitor callback (the C++ equivalent of fs.watch from Node).
// A single save can surface as several events (changed, done-hint,
// or created when the editor saves atomically via rename), so we
// reload on all of them — parsing is microseconds, and reloading
// twice in a row is idempotent.
void on_config_file_changed(GFileMonitor*, GFile*, GFile*,
                            GFileMonitorEvent event, gpointer) {
    if (event == G_FILE_MONITOR_EVENT_DELETED) return;
    reload_config();
}

// ── Game-focus visibility ────────────────────────────────────
// The overlay only makes sense ON TOP of the game. The watcher
// reports whether the game window holds the X input focus; whenever
// it does not (alt-tab, desktop, game closed) both surfaces hide.
// Interactive mode suppresses hiding on purpose: while the user
// clicks the overlay the game is unfocused BY DEFINITION, and hiding
// the very thing being clicked would be absurd.
void apply_overlay_visibility() {
    const bool visible =
        !g_game_watch_active || g_game_focused || g_interactive;
    gtk_widget_set_visible(GTK_WIDGET(g_window), visible);
    if (g_panel_window != nullptr)
        gtk_widget_set_visible(GTK_WIDGET(g_panel_window), visible);
}

void on_game_focus_change(bool focused) {
    g_game_focused = focused;
    apply_overlay_visibility();
}

// Starts or stops the watcher to match the current config, so the
// settings switch applies live through the usual TOML round-trip.
void sync_game_watch() {
    const bool want = g_config.overlay.show_only_when_game_focused;
    if (want && !g_game_watch_active) {
        if (platform::game_watch_start(on_game_focus_change)) {
            g_game_watch_active = true;
            g_game_focused = platform::game_has_focus_now();
            g_message("game watch: overlay shows only while the game "
                      "holds focus (initially %s)",
                      g_game_focused ? "focused" : "unfocused");
        }
    } else if (!want && g_game_watch_active) {
        platform::game_watch_stop();
        g_game_watch_active = false;
        apply_overlay_visibility(); // nothing hides the overlay anymore
    }
}

void set_interactive(bool enabled) {
    g_interactive = enabled;
    g_message("interactive mode: %s",
              enabled ? "ON (overlay clickable)" : "OFF (click-through)");
    platform::overlay_set_interactive(g_window, enabled);
    if (g_panel_window != nullptr)
        platform::overlay_set_interactive(g_panel_window, enabled);
    // Leaving interactive mode re-exposes the focus rule: if the
    // game is not focused, the overlay hides now.
    apply_overlay_visibility();
}

// D-Bus action handler. Signature fixed by GAction: the action
// itself, an optional parameter variant (unused here), user data.
void on_toggle_interactive(GSimpleAction*, GVariant*, gpointer) {
    set_interactive(!g_interactive);
}

// Same entry point as the panel's gear button, exported over D-Bus
// so a desktop shortcut can open the settings too.
void on_open_settings(GSimpleAction*, GVariant*, gpointer) {
    settings::present(GTK_APPLICATION(g_app), g_config, kConfigPath);
}

// Alarm sound, platform backend in platform/sound_*.cpp (canberra
// on Linux, PlaySound on Windows). Never blocks the tick: both
// backends enqueue the sample and return.
void play_alarm_sound() {
    if (g_config.alarms.volume <= 0) return; // silence is a valid preference
    platform::play_alarm_bell(g_config.alarms.volume);
}

// Delivery side of the alarm: a desktop notification through
// GNotification (GLib, not GTK — composes fine with the app's own
// windows). The id replaces the previous alarm notification with the
// same event instead of stacking duplicates in the notification tray.
void send_alarm_notification(const ScheduleEvent& event, int minutes) {
    auto* notification = g_notification_new(event.name.c_str());
    const std::string body = std::format("starts in {} min", minutes);
    g_notification_set_body(notification, body.c_str());
    const std::string id = "cabal-alarm-" + event.id;
    g_application_send_notification(g_app, id.c_str(), notification);
    g_object_unref(notification);
    play_alarm_sound();
    g_message("alarm: %s in %d min", event.name.c_str(), minutes);
}

// Simple replacement-style notification for app-level feedback (no
// sound — this is not an alarm, just an acknowledgement).
void send_info_notification(const char* id, const char* title,
                            const std::string& body) {
    auto* notification = g_notification_new(title);
    g_notification_set_body(notification, body.c_str());
    g_application_send_notification(g_app, id, notification);
    g_object_unref(notification);
}

// Sensor → app logic. Runs on the GLib main thread (the pointer poll
// is a GLib timer), so it touches the same globals every other
// callback uses — no locking, same as the tick.
void on_pointer_click(int x, int y, bool ctrl) {
    if (g_calibrating_click) {
        g_calibrating_click = false;
        // The clicked spot is the CENTER of the zone, not its corner:
        // the user clicks the dialog button repeatedly, and those
        // clicks scatter by a few pixels around one point — a corner-
        // anchored rectangle would put most of them outside its
        // top/left edges. The configured size defines the rectangle
        // around the click; coordinates may go negative near screen
        // edges, the hit test handles that fine.
        try {
            const int half_w = g_config.dgcheck.width / 2;
            const int half_h = g_config.dgcheck.height / 2;
            set_config_value(kConfigPath, "dgcheck.x", x - half_w);
            set_config_value(kConfigPath, "dgcheck.y", y - half_h);
            set_config_value(kConfigPath, "dgcheck.enabled", true);
            const std::string body =
                std::format("zone centered at ({}, {})", x, y);
            send_info_notification("cabal-dgcheck-calibrated",
                                   "Click zone captured", body);
            g_message("dgcheck: zone captured at %d,%d (center)", x, y);
        } catch (const std::exception& error) {
            g_warning("dgcheck: could not save captured zone: %s",
                      error.what());
        }
        return;
    }

    if (!dgcheck::counts_click(g_config.dgcheck, x, y, ctrl)) {
        // Diagnostic for the armed-but-missing case: a CTRL+click
        // while the feature is on is a deliberate counter attempt —
        // one line beats guessing whether the click reached the app.
        if (ctrl && g_config.dgcheck.enabled)
            g_message("dgcheck: ctrl+click at %d,%d is outside zone "
                      "[%d..%d) x [%d..%d)", x, y,
                      g_config.dgcheck.x,
                      g_config.dgcheck.x + g_config.dgcheck.width,
                      g_config.dgcheck.y,
                      g_config.dgcheck.y + g_config.dgcheck.height);
        return; // a normal game click, outside the zone
    }
    materialize_active_tasks();
    const std::string id = dgcheck::target_task_id(g_active_tasks);
    if (id.empty()) {
        g_message("dgcheck: click in zone but no task tracked");
        return;
    }
    g_actions.bump_count(id, +1);
    g_message("dgcheck: +1 on task %s", id.c_str());
}

// D-Bus action handler: arm one-shot zone capture. The settings
// window button and any desktop shortcut can both trigger it.
void on_calibrate_zone(GSimpleAction*, GVariant*, gpointer) {
    g_calibrating_click = true;
    g_message("dgcheck: waiting for the next click to capture the "
              "dialog position");
}

// Graceful shutdown, also exported over D-Bus so scripts and desktop
// shortcuts can quit the overlay (pkill stays the fallback).
void on_quit(GSimpleAction*, GVariant*, gpointer) {
    g_application_quit(g_app);
}

// g_timeout_add callback. GLib timers expect this exact signature:
// returning G_SOURCE_CONTINUE re-arms the timer for another second;
// returning G_SOURCE_REMOVE would stop it.
gboolean on_tick(gpointer label_ptr) {
    const auto now = std::chrono::system_clock::now();
    // Reset detection is cheap (two date string comparisons) and
    // runs every tick: the reset fires exactly at server midnight
    // even if the app was running, and on the next tick after a
    // restart if it was closed.
    if (apply_resets(g_state, now)) {
        g_message("server reset detected, tasks cleared");
        persist_state();
    }
    if (apply_preset_resets(g_preset_state, g_presets, now)) {
        g_message("server reset detected, preset progress cleared");
        persist_preset_state();
    }

    // Warning-window alarms: fires a notification once per event
    // occurrence when it enters the configured warn-before window.
    g_alarms.check(g_config.schedules, g_config.alarms, now,
                   send_alarm_notification);

    auto* label = GTK_LABEL(label_ptr);
    const std::string text = bar_text();
    gtk_label_set_text(label, text.c_str());

    // Refresh the goals panel only when the task list changed since
    // the last render (new task, counter bump, reset wiped the list).
    // The signature comparison is the React "key" idea: a cheap check
    // per second, a full rebuild only on mismatch.
    if (g_goals_box != nullptr) {
        materialize_active_tasks();
        const std::string signature =
            goals_signature(g_active_tasks, g_config.panel.short_names,
                            g_config.panel.collapsed);
        if (signature != g_goals_signature) {
            g_goals_signature = signature;
            refresh_goals_panel();
        }
    }
    return G_SOURCE_CONTINUE;
}

// Handled by the drag gesture's tap detection (overlay_enable_drag):
// a press-and-release without movement on the bar means "I'm done" —
// hand the mouse back to the game. Dragging the bar moves it instead.

// GTK styling works with CSS, same idea as the web tracker but
// applied to native widgets instead of DOM elements.
void apply_css() {
    auto* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, R"css(
        window { background-color: transparent; }
        .overlay-bar {
            background-color: alpha(black, 0.5);
            color: #ffd24d;
            font-family: monospace;
            font-size: 14px;
            padding: 6px 14px;
            border-radius: 8px;
            border: 1px solid alpha(#ffd24d, 0.4);
        }
        .goals-panel {
            background-color: alpha(black, 0.5);
            color: #ffd24d;
            padding: 10px 12px;
            border-radius: 8px;
            border: 1px solid alpha(#ffd24d, 0.4);
            min-width: 240px;
        }
        .goal-section {
            font-family: monospace;
            font-size: 11px;
            font-weight: bold;
            color: alpha(#ffd24d, 0.75);
            margin-top: 4px;
        }
        .goal-section:first-child { margin-top: 0; }
        .goal-more-hint {
            font-family: monospace;
            font-size: 11px;
            color: alpha(#ffd24d, 0.45);
        }
        .goal-name {
            font-family: monospace;
            font-size: 12px;
        }
        .goal-count {
            font-family: monospace;
            font-size: 12px;
            color: white;
        }
        .goals-panel progressbar trough {
            background-color: alpha(white, 0.15);
            border-radius: 3px;
            min-height: 6px;
        }
        .goals-panel progressbar progress {
            background-color: #ffd24d;
            border-radius: 3px;
            min-height: 6px;
        }
        .goal-bump {
            font-family: monospace;
            font-size: 11px;
            padding: 0 8px;
            min-height: 18px;
            background-color: alpha(white, 0.08);
            color: #ffd24d;
            border-radius: 4px;
        }
        .goal-bump:hover { background-color: alpha(#ffd24d, 0.25); }
        .goal-add-toggle {
            font-family: monospace;
            font-size: 11px;
            padding: 2px 8px;
            background-color: alpha(#ffd24d, 0.12);
            color: #ffd24d;
            border-radius: 4px;
        }
        .goal-add-toggle:hover { background-color: alpha(#ffd24d, 0.25); }
        .goal-form { border-top: 1px solid alpha(#ffd24d, 0.25); padding-top: 6px; }
        .goal-form-title {
            font-family: monospace;
            font-size: 11px;
            font-weight: bold;
            color: alpha(#ffd24d, 0.75);
        }
        .goal-form-entry, .goal-form-spin, .goal-form-add, .goal-form button {
            font-family: monospace;
            font-size: 12px;
        }
        .goal-form-add {
            background-color: alpha(#ffd24d, 0.25);
            color: #ffd24d;
            border-radius: 4px;
        }
    )css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider); // The display keeps its own reference now.
}

void on_activate(GtkApplication* app, gpointer) {
    g_app = G_APPLICATION(app);

    // Preset task lists: optional like the dungeon catalog — without
    // them the panel switcher simply never appears.
    try {
        g_presets = load_task_presets(kTaskListsPath, g_dungeons);
    } catch (const std::exception& error) {
        g_warning("preset lists unavailable (%s)", error.what());
    }
    try {
        g_presets_path = default_presets_path();
        g_preset_state = load_preset_state(g_presets_path);
        // A remembered active list that no longer exists in the file
        // falls back to custom instead of showing nothing.
        if (g_preset_state.active_list != "custom" &&
            find_preset(g_preset_state.active_list) == nullptr)
            g_preset_state.active_list = "custom";
    } catch (const std::exception& error) {
        g_warning("preset state unreadable (%s); starting fresh",
                  error.what());
    }

    apply_css();
    GtkWidget* window = gtk_application_window_new(app);
    g_window = GTK_WINDOW(window);

    // All Wayland-specific setup is two calls behind the platform
    // contract; this file does not know what layer-shell is.
    platform::overlay_init(g_window);
    platform::overlay_apply_placement(g_window, placement_from(g_config.overlay));

    // fs.watch, C edition: the overlay re-reads its config whenever
    // the TOML changes, so editing the file moves/restyles the bar
    // without restarting. Kept for the app's whole lifetime.
    auto* config_file = g_file_new_for_path(kConfigPath.c_str());
    g_config_monitor = g_file_monitor_file(config_file, G_FILE_MONITOR_NONE,
                                           nullptr, nullptr);
    g_object_unref(config_file);
    if (g_config_monitor != nullptr)
        g_signal_connect(g_config_monitor, "changed",
                         G_CALLBACK(on_config_file_changed), nullptr);
    else
        g_warning("could not watch config file %s", kConfigPath.c_str());

    GtkWidget* bar = gtk_label_new(bar_text().c_str());
    gtk_widget_add_css_class(bar, "overlay-bar");
    gtk_window_set_child(GTK_WINDOW(window), bar);

    // Drag-to-move: while interactive, grabbing the bar moves the
    // surface (persisted as an anchor = "custom" position); a plain
    // tap means "done, give the mouse back to the game".
    platform::overlay_enable_drag(
        g_window,
        [](int margin_x, int margin_y) {
            save_dragged_placement("overlay", margin_x, margin_y);
        },
        [] { set_interactive(false); });

    // Second surface: the goals panel, anchored to one vertical edge
    // only, which makes the compositor center it. Same three platform
    // calls as the bar — this file never learns what layer-shell is.
    sync_goals_panel(app);

    gtk_window_present(GTK_WINDOW(window));

    // Start in click-through mode: the game keeps the mouse.
    set_interactive(false);

    // Hide both surfaces whenever the game is not the focused
    // window. The initial synchronous sweep inside game_watch_start
    // means a not-running game never flashes the overlay over the
    // desktop even for a frame: everything above already ran within
    // this single main-loop tick.
    sync_game_watch();
    apply_overlay_visibility();

    // Global combo handling (see [hotkey] in the TOML and
    // docs/04-hotkey-modes.md). On Linux the default is External: the
    // desktop calls our D-Bus action; the app needs no special
    // permissions. On Windows there is no D-Bus action to bind, so
    // any mode except Disabled falls back to the built-in
    // RegisterHotKey combo.
    auto toggle = [] { set_interactive(!g_interactive); };
    switch (g_config.hotkey.mode) {
    case HotkeyMode::Evdev:
        // Reads /dev/input directly: works on any compositor while
        // the game holds focus. Requires input group membership —
        // the user opted in via the config, having read the warning.
        platform::hotkey_start(g_config.hotkey.combo, toggle);
        break;
#ifdef _WIN32
    case HotkeyMode::External:
        // No D-Bus on Windows: "external" degrades to the built-in
        // combo instead of doing nothing (the least surprising
        // behavior for a config that shipped from Linux).
        platform::hotkey_start(g_config.hotkey.combo, toggle);
        break;
#else
    case HotkeyMode::External:
        g_message("hotkey: external mode — bind a desktop shortcut to the "
                  "D-Bus action, e.g. KDE: System Settings → Shortcuts → "
                  "Custom Shortcuts → New → Command/URL: gdbus call --session "
                  "--dest dev.cabal.Overlay --object-path /dev/cabal/Overlay "
                  "--method org.gtk.Actions.Activate toggle-interactive [] {}");
        break;
#endif
    case HotkeyMode::Disabled:
        g_message("hotkey: disabled (no global combo)");
        break;
    }

    // System tray presence (Windows only; a no-op on Linux, see
    // platform/tray/tray.h): the overlay surfaces have no taskbar
    // entry by design, so the tray icon is the app's lifecycle
    // handle — right-click for Settings / Quit.
    platform::tray_start(platform::TrayActions{
        .on_show_settings =
            [] { settings::present(GTK_APPLICATION(g_app), g_config,
                                   kConfigPath); },
        .on_quit = [] { g_application_quit(g_app); },
    });

    // Catch up on resets that happened while the app was closed.
    if (apply_resets(g_state, std::chrono::system_clock::now()))
        persist_state();
    if (apply_preset_resets(g_preset_state, g_presets,
                            std::chrono::system_clock::now()))
        persist_preset_state();

    // Pointer sensor for the dgcheck counter. Started unconditionally
    // (it is a zero-privilege X11 client): whether clicks COUNT is the
    // config's [dgcheck] decision, made on every click, so toggling
    // the zone in the settings needs no restart. Calibration also
    // requires the watch even while the feature is disabled.
    if (platform::pointer_watch_start(on_pointer_click))
        g_message("dgcheck: pointer watch active (zone %dx%d at %d,%d, "
                  "ctrl required: %s)", g_config.dgcheck.width,
                  g_config.dgcheck.height, g_config.dgcheck.x,
                  g_config.dgcheck.y,
                  g_config.dgcheck.require_ctrl ? "yes" : "no");

    // Tick once per second to refresh clocks and countdowns.
    g_timeout_add(1000, on_tick, bar);
}

} // namespace

int main(int argc, char* argv[]) {
    // Config first: if the TOML is broken we fail loudly before
    // opening any window. try/catch is how C++ reports recoverable
    // failures that must cross many layers — load_config throws,
    // the one place that knows what to do about it (here) catches.
    try {
        g_config = load_config(kConfigPath);
    } catch (const std::exception& error) {
        g_printerr("cabal-overlay: %s\n", error.what());
        return 1;
    }

    // State second. A MISSING state file is fine (first run); a
    // MALFORMED one keeps us running in memory but disables saving,
    // so the damaged file stays on disk for manual repair.
    g_state_path = default_state_path();
    try {
        g_state = load_state(g_state_path);
    } catch (const std::exception& error) {
        g_state_writable = false;
        g_warning("state file unreadable (%s); running in memory, "
                  "will not save", error.what());
    }

    // Dungeon catalog: optional. Without it the add-task form simply
    // offers no dropdown suggestions and every name is custom.
    try {
        g_dungeons = load_dungeons(kDungeonsPath);
    } catch (const std::exception& error) {
        g_warning("dungeon catalog unavailable (%s)", error.what());
    }

    // The panel's action callbacks, wired before activation builds
    // any widget that references them.
    g_actions = make_goals_actions();

    // Under Wine with a Wayland session, GTK4's backend probing tries
    // wayland first and hard-fails instead of falling through to
    // win32 ("Failed to open display"). Pin the backend: on Windows
    // only win32 ever makes sense. Must run before GTK opens any
    // display (i.e. before gtk_application_new runs its activate).
#ifdef _WIN32
    gdk_set_allowed_backends("win32");
#endif

    // GtkApplication gives us the GLib main loop, a unique D-Bus name
    // (dev.cabal.Overlay) and single-instance behavior for free.
    auto* app = gtk_application_new(kApplicationId, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    // Shell_NotifyIcon icons ghost until hovered if the process dies
    // without NIM_DELETE — remove ours on the way out.
    g_signal_connect(app, "shutdown",
                     G_CALLBACK(+[](GtkApplication*, gpointer) {
                         platform::tray_stop();
                     }),
                     nullptr);

    // GAction entries registered on the app are automatically exported
    // over D-Bus (interface org.gtk.Actions). This is the public remote
    // control of the overlay; KDE custom shortcuts call it:
    //   gdbus call --session --dest dev.cabal.Overlay --object-path /dev/cabal/Overlay --method org.gtk.Actions.Activate toggle-interactive [] {}
    const GActionEntry actions[] = {
        {
            .name           = "toggle-interactive",
            .activate       = on_toggle_interactive,
            .parameter_type = nullptr,
            .state          = nullptr,
            .change_state   = nullptr,
            .padding        = {0, 0, 0},
        },
        {
            .name           = "open-settings",
            .activate       = on_open_settings,
            .parameter_type = nullptr,
            .state          = nullptr,
            .change_state   = nullptr,
            .padding        = {0, 0, 0},
        },
        {
            .name           = "calibrate-zone",
            .activate       = on_calibrate_zone,
            .parameter_type = nullptr,
            .state          = nullptr,
            .change_state   = nullptr,
            .padding        = {0, 0, 0},
        },
        {
            .name           = "quit",
            .activate       = on_quit,
            .parameter_type = nullptr,
            .state          = nullptr,
            .change_state   = nullptr,
            .padding        = {0, 0, 0},
        },
    };
    g_action_map_add_action_entries(G_ACTION_MAP(app), actions, G_N_ELEMENTS(actions), app);

    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
