// ─────────────────────────────────────────────────────────────
// cabal-overlay — entry point
//
// Milestone: a click-through, always-on-top bar anchored to the
// top-right corner of the screen, rendering above every window
// (including the game), showing live Cabal server time.
//
// Prototype control: CLICK the bar to quit the application.
// ─────────────────────────────────────────────────────────────

#include <chrono>
#include <format>
#include <string>

#include <gtk/gtk.h>
#include <gtk4-layer-shell/gtk4-layer-shell.h>

// Anonymous namespace: everything inside is private to this file,
// like `static` at file scope. Nothing here can be referenced
// (or clash) from other translation units.
namespace {

// The game server runs on Europe/Berlin time (CET/CEST). The IANA
// timezone database handles daylight-saving switches for us.
constexpr char kServerTimezone[] = "Europe/Berlin";
constexpr char kApplicationId[]  = "dev.cabal.Overlay";

// Text shown in the bar. zoned_time converts the absolute system
// clock ("now") into wall-clock time at the server's location.
std::string server_time_text() {
    const auto* zone = std::chrono::locate_zone(kServerTimezone);
    const std::chrono::zoned_time server_now{zone, std::chrono::system_clock::now()};
    return std::format("Server {:%H:%M:%S}", server_now);
}

// g_timeout_add callback. GLib timers expect this exact signature:
// returning G_SOURCE_CONTINUE re-arms the timer for another second;
// returning G_SOURCE_REMOVE would stop it.
gboolean on_tick(gpointer label_ptr) {
    auto* label = GTK_LABEL(label_ptr);
    const std::string text = server_time_text();
    gtk_label_set_text(label, text.c_str());
    return G_SOURCE_CONTINUE;
}

void on_bar_clicked(GtkGestureClick*, gint, gdouble, gdouble, gpointer app_ptr) {
    g_application_quit(G_APPLICATION(app_ptr));
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

    // ── Layer-shell setup: what turns a plain window into an overlay.
    // OVERLAY layer  → drawn above everything, even fullscreen apps.
    // Anchors        → pinned to the top-right corner, with margins.
    // Exclusive zone -1 → "I float on top, don't reserve space for me".
    // Keyboard NONE  → we never steal key focus from the game.
    gtk_layer_init_for_window(GTK_WINDOW(window));
    gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, 60);
    gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, 20);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(window), -1);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(window), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);

    GtkWidget* bar = gtk_label_new(server_time_text().c_str());
    gtk_widget_add_css_class(bar, "overlay-bar");

    // Prototype convenience: click the bar to quit. Real input
    // handling (D-Bus toggle, expand/collapse) comes later.
    auto* click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_bar_clicked), app);
    gtk_widget_add_controller(bar, GTK_EVENT_CONTROLLER(click));

    gtk_window_set_child(GTK_WINDOW(window), bar);
    gtk_window_present(GTK_WINDOW(window));

    // Tick once per second to refresh the clock.
    g_timeout_add(1000, on_tick, bar);
}

} // namespace

int main(int argc, char* argv[]) {
    // GtkApplication gives us the GLib main loop, a unique D-Bus name
    // (dev.cabal.Overlay — the same one later phases will expose the
    // show/hide API on) and single-instance behavior for free.
    auto* app = gtk_application_new(kApplicationId, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);

    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
