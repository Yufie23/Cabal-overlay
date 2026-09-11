// ─────────────────────────────────────────────────────────────
// cabal-overlay — entry point
//
// A click-through, always-on-top bar floating above every window
// (including the game), showing the local clock plus live
// countdowns to the next scheduled game events.
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
// config.*, and everything Wayland-specific behind platform/.
// ─────────────────────────────────────────────────────────────

#include <chrono>
#include <exception>
#include <string>

#include <gtk/gtk.h>

#include "clock.h"
#include "config.h"
#include "platform/overlay.h"
#include "schedule.h"

namespace {

constexpr char kApplicationId[] = "dev.cabal.Overlay";
constexpr char kConfigPath[]    = "config/overlay.toml";

// ── Module state ─────────────────────────────────────────────
// One window, one mode flag, the current config and the config
// file monitor — all owned by the single GtkApplication instance.
// GLib is single-threaded like Node: callbacks (timer, monitor,
// signals) never run concurrently, so no locks are needed.
GtkWindow*    g_window = nullptr;
bool          g_interactive = false;
AppConfig     g_config;
GFileMonitor* g_config_monitor = nullptr;

// The full bar text: clock on the left, next event countdowns on
// the right, all derived from one single clock reading so nothing
// in the bar can disagree with itself.
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
        platform::overlay_apply_config(g_window, g_config.overlay);
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
    auto* label = GTK_LABEL(label_ptr);
    const std::string text = bar_text();
    gtk_label_set_text(label, text.c_str());
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

    // All Wayland-specific setup is one call behind the platform
    // contract; this file does not know what layer-shell is.
    platform::overlay_init(g_window, g_config.overlay);

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
    gtk_window_present(GTK_WINDOW(window));

    // Start in click-through mode: the game keeps the mouse.
    set_interactive(false);

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
