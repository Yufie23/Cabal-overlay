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
//   - Close from a terminal: pkill cabal-overlay
//
// This file only orchestrates: it loads the config, wires GTK
// signals, the D-Bus action and the 1-second timer. Time logic lives
// in clock.*, schedule math in schedule.*, config parsing in
// config.*, task data in tasks./state., panel rendering in ui.*,
// and everything Wayland-specific behind platform/.
// ─────────────────────────────────────────────────────────────

#include <chrono>
#include <exception>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "clock.h"
#include "config.h"
#include "dungeons.h"
#include "platform/overlay.h"
#include "schedule.h"
#include "state.h"
#include "tasks.h"
#include "ui.h"

namespace {

constexpr char kApplicationId[] = "dev.cabal.Overlay";
constexpr char kConfigPath[]    = "config/overlay.toml";
constexpr char kDungeonsPath[]  = "data/dungeons.json";

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
// When the state file exists but is unreadable we keep running with
// an in-memory copy — and refuse to SAVE, so a corrupted file is
// never overwritten by accident.
bool          g_state_writable = true;
// Fingerprint of the task list as last rendered in the panel. The
// tick compares against goals_signature() and only rebuilds the
// panel widgets when a task actually changed.
std::string   g_goals_signature;

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

// Rebuilds the task rows and syncs panel visibility. The single
// refresh path used by the tick AND by every user action, so both
// stay consistent. An open add-task form keeps the panel on screen
// even with zero tasks.
void refresh_goals_panel() {
    if (g_goals_box == nullptr || g_panel_window == nullptr) return;
    goals_panel_refresh(g_goals_box, g_state, g_actions);
    const bool visible = !g_state.tasks.all().empty() ||
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
        g_state.tasks.bump_count(id, delta);
        persist_state();
        refresh_goals_panel();
    };
    actions.set_completed = [](const std::string& id, bool completed) {
        g_state.tasks.set_completed(id, completed);
        persist_state();
        refresh_goals_panel();
    };
    actions.add_task = [](TaskType type, const std::string& name, int goal) {
        g_state.tasks.add(type, name, goal);
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

// Live config reload: the file watcher calls this whenever the
// TOML changes. Unlike startup, a broken file here is NOT fatal:
// we log the error and keep the previous working config (the bar
// keeps showing the last good state).
void reload_config() {
    try {
        g_config = load_config(kConfigPath);
        platform::overlay_apply_placement(g_window, placement_from(g_config.overlay));
        if (g_panel_window != nullptr)
            platform::overlay_apply_placement(g_panel_window,
                                              placement_from(g_config.panel));
        g_message("config reloaded from %s", kConfigPath);
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

void set_interactive(bool enabled) {
    g_interactive = enabled;
    platform::overlay_set_interactive(g_window, enabled);
    if (g_panel_window != nullptr)
        platform::overlay_set_interactive(g_panel_window, enabled);
}

// D-Bus action handler. Signature fixed by GAction: the action
// itself, an optional parameter variant (unused here), user data.
void on_toggle_interactive(GSimpleAction*, GVariant*, gpointer) {
    set_interactive(!g_interactive);
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

    auto* label = GTK_LABEL(label_ptr);
    const std::string text = bar_text();
    gtk_label_set_text(label, text.c_str());

    // Refresh the goals panel only when the task list changed since
    // the last render (new task, counter bump, reset wiped the list).
    // The signature comparison is the React "key" idea: a cheap check
    // per second, a full rebuild only on mismatch.
    if (g_goals_box != nullptr) {
        const std::string signature = goals_signature(g_state);
        if (signature != g_goals_signature) {
            g_goals_signature = signature;
            refresh_goals_panel();
        }
    }
    return G_SOURCE_CONTINUE;
}

void on_bar_clicked(GtkGestureClick*, gint, gdouble, gdouble, gpointer) {
    // Clicks only reach the bar while interactive, so a click here
    // means "I'm done": hand the mouse back to the game.
    set_interactive(false);
}

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
    auto* config_file = g_file_new_for_path(kConfigPath);
    g_config_monitor = g_file_monitor_file(config_file, G_FILE_MONITOR_NONE,
                                           nullptr, nullptr);
    g_object_unref(config_file);
    if (g_config_monitor != nullptr)
        g_signal_connect(g_config_monitor, "changed",
                         G_CALLBACK(on_config_file_changed), nullptr);
    else
        g_warning("could not watch config file %s", kConfigPath);

    GtkWidget* bar = gtk_label_new(bar_text().c_str());
    gtk_widget_add_css_class(bar, "overlay-bar");

    auto* click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_bar_clicked), app);
    gtk_widget_add_controller(bar, GTK_EVENT_CONTROLLER(click));

    gtk_window_set_child(GTK_WINDOW(window), bar);

    // Second surface: the goals panel, anchored to one vertical edge
    // only, which makes the compositor center it. Same three platform
    // calls as the bar — this file never learns what layer-shell is.
    if (g_config.panel.visible) {
        GtkWidget* panel_window = gtk_application_window_new(app);
        g_panel_window = GTK_WINDOW(panel_window);
        platform::overlay_init(g_panel_window);

        // Outer styled box; inside it, the task rows live in their
        // own box so rebuilds never touch the form or its toggle.
        g_goals_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_add_css_class(g_goals_panel, "goals-panel");
        g_goals_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_box_append(GTK_BOX(g_goals_panel), g_goals_box);

        g_goals_form = goals_add_form_new(g_actions, g_dungeons);

        auto* add_task_button = gtk_button_new_with_label("＋ Add task");
        gtk_widget_add_css_class(add_task_button, "goal-add-toggle");
        // Capture-less lambda → plain function pointer, so it can
        // serve as a GTK callback. Reads the global form; no state
        // of its own.
        g_signal_connect(add_task_button, "clicked",
                         G_CALLBACK(+[](GtkButton*, gpointer) {
                             gtk_widget_set_visible(g_goals_form, TRUE);
                         }), nullptr);

        gtk_box_append(GTK_BOX(g_goals_panel), add_task_button);
        gtk_box_append(GTK_BOX(g_goals_panel), g_goals_form);

        gtk_window_set_child(g_panel_window, g_goals_panel);
        platform::overlay_apply_placement(g_panel_window,
                                          placement_from(g_config.panel));

        // Initial fill; visibility is refresh_goals_panel()'s job.
        g_goals_signature = goals_signature(g_state);
        refresh_goals_panel();
        gtk_window_present(g_panel_window);
    }

    gtk_window_present(GTK_WINDOW(window));

    // Start in click-through mode: the game keeps the mouse.
    set_interactive(false);

    // Catch up on resets that happened while the app was closed.
    if (apply_resets(g_state, std::chrono::system_clock::now()))
        persist_state();

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

    // GtkApplication gives us the GLib main loop, a unique D-Bus name
    // (dev.cabal.Overlay) and single-instance behavior for free.
    auto* app = gtk_application_new(kApplicationId, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);

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
    };
    g_action_map_add_action_entries(G_ACTION_MAP(app), actions, G_N_ELEMENTS(actions), app);

    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
