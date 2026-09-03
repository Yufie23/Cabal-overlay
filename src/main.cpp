// ─────────────────────────────────────────────────────────────
// cabal-overlay — entry point
//
// A click-through, always-on-top bar floating above every window
// (including the game), showing Cabal server time and local time.
//
// Controls:
//   - Default mode is CLICK-THROUGH: clicks fall through to the game.
//   - The "toggle-interactive" D-Bus action flips to clickable mode
//     (bind it to a KDE custom shortcut). While interactive, clicking
//     the bar returns to click-through mode.
//   - Close from a terminal: pkill cabal-overlay
//
// This file only orchestrates: it wires GTK signals, the D-Bus
// action and the 1-second timer. Time logic lives in clock.*, and
// everything Wayland-specific lives in platform/overlay_wayland.cpp
// behind the platform:: contract (platform/overlay.h).
// ─────────────────────────────────────────────────────────────

#include <gtk/gtk.h>

#include "clock.h"
#include "platform/overlay.h"

namespace {

constexpr char kApplicationId[] = "dev.cabal.Overlay";

// ── Module state ─────────────────────────────────────────────
// Exactly one window and one mode flag, owned by the single
// GtkApplication instance. If the app ever grows more windows,
// this becomes a small class.
GtkWindow* g_window = nullptr;
bool       g_interactive = false;

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
    const std::string text = clock_text();
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
            background-color: alpha(black, 0.75);
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
    platform::overlay_init(g_window);

    GtkWidget* bar = gtk_label_new(clock_text().c_str());
    gtk_widget_add_css_class(bar, "overlay-bar");

    auto* click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_bar_clicked), app);
    gtk_widget_add_controller(bar, GTK_EVENT_CONTROLLER(click));

    gtk_window_set_child(GTK_WINDOW(window), bar);
    gtk_window_present(GTK_WINDOW(window));

    // Start in click-through mode: the game keeps the mouse.
    set_interactive(false);

    // Tick once per second to refresh the clock.
    g_timeout_add(1000, on_tick, bar);
}

} // namespace

int main(int argc, char* argv[]) {
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
