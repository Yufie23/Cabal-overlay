// ─────────────────────────────────────────────────────────────
// settings_window.cpp — tabbed editor over the TOML
//
// Layout: one GtkNotebook page per config section (Bar, Panel,
// Alarms, Hotkey), each a two-column grid of label + control rows.
//
// Control wiring follows the established payload pattern: every
// control carries a SettingPayload {config path, dotted key}
// attached with g_object_set_data_full, and one trampoline per
// widget type reads the control's value and writes it back. A failed
// write is logged as a warning and swallowed — a settings typo must
// never take the overlay down mid-game.
// ─────────────────────────────────────────────────────────────

#include "settings_window.h"

#include <exception>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace {

GtkWindow* g_window = nullptr; // the singleton
// The app behind the singleton, kept so widgets built here can fire
// application-level GActions (e.g. the dgcheck calibrate action)
// without threading pointers through every factory.
GtkApplication* g_app = nullptr;

// ── Payload + trampolines ────────────────────────────────────

struct SettingPayload {
    std::string config_path;
    std::string key;
};

void delete_setting_payload(gpointer data) {
    delete static_cast<SettingPayload*>(data);
}

void attach_setting(GtkWidget* widget, std::string config_path,
                    std::string key) {
    g_object_set_data_full(G_OBJECT(widget), "cabal-setting",
                           new SettingPayload{std::move(config_path),
                                              std::move(key)},
                           delete_setting_payload);
}

const SettingPayload& setting_of(GtkWidget* widget) {
    // Contract: "cabal-setting" was attached by attach_setting and
    // lives as long as the widget.
    return *static_cast<const SettingPayload*>(
        g_object_get_data(G_OBJECT(widget), "cabal-setting"));
}

// Every trampoline funnels through here: write, and on failure log
// the exact key and reason instead of crashing the overlay.
void apply_setting(GtkWidget* widget, const ConfigValue& value) {
    const SettingPayload& setting = setting_of(widget);
    try {
        set_config_value(setting.config_path, setting.key, value);
    } catch (const std::exception& error) {
        g_warning("setting %s failed: %s", setting.key.c_str(),
                  error.what());
    }
}

void on_switch_applied(GObject* switch_widget, GParamSpec*, gpointer) {
    apply_setting(GTK_WIDGET(switch_widget),
                  gtk_switch_get_active(GTK_SWITCH(switch_widget)) == TRUE);
}

void on_spin_applied(GObject* spin, GParamSpec*, gpointer) {
    apply_setting(GTK_WIDGET(spin),
                  gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin)));
}

void on_scale_applied(GObject* scale, GParamSpec*, gpointer) {
    apply_setting(GTK_WIDGET(scale),
                  gtk_range_get_value(GTK_RANGE(scale)));
}

void on_dropdown_applied(GObject* dropdown, GParamSpec*, gpointer user_data) {
    // user_data: NULL-terminated array of option strings, static for
    // the app's lifetime (see make_dropdown).
    const auto* const* options = static_cast<const char* const*>(user_data);
    const guint index =
        gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    if (options[index] == nullptr) return; // defensive; should not happen
    apply_setting(GTK_WIDGET(dropdown), std::string(options[index]));
}

void on_entry_applied(GtkEditable* entry, gpointer) {
    const char* text = gtk_editable_get_text(entry);
    apply_setting(GTK_WIDGET(entry), std::string(text != nullptr ? text : ""));
}

// ── Control factories (initial value set BEFORE connecting, so
//    populating the window never fires writes) ────────────────

GtkWidget* make_switch(const std::string& path, const char* key, bool initial) {
    auto* widget = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(widget), initial);
    attach_setting(widget, path, key);
    g_signal_connect(widget, "notify::active",
                     G_CALLBACK(on_switch_applied), nullptr);
    return widget;
}

GtkWidget* make_spin(const std::string& path, const char* key, int initial,
                     int min, int max) {
    auto* widget = gtk_spin_button_new_with_range(min, max, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), initial);
    attach_setting(widget, path, key);
    g_signal_connect(widget, "notify::value",
                     G_CALLBACK(on_spin_applied), nullptr);
    return widget;
}

GtkWidget* make_scale(const std::string& path, const char* key, double initial,
                      double min, double max, int digits) {
    auto* widget = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                            min, max, 0.05);
    gtk_scale_set_digits(GTK_SCALE(widget), digits);
    gtk_range_set_value(GTK_RANGE(widget), initial);
    attach_setting(widget, path, key);
    g_signal_connect(widget, "notify::value",
                     G_CALLBACK(on_scale_applied), nullptr);
    return widget;
}

// The options arrays are non-const pointers to const text: the
// strings never change, and the non-const pointer level lets the
// array decay to gpointer for g_signal_connect without a cast.
GtkWidget* make_dropdown(const std::string& path, const char* key,
                         const char** options, int n_options,
                         int initial_index) {
    std::vector<const char*> names(options, options + n_options);
    names.push_back(nullptr);
    auto* widget =
        gtk_drop_down_new(G_LIST_MODEL(gtk_string_list_new(names.data())),
                          nullptr);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(widget), initial_index);
    attach_setting(widget, path, key);
    g_signal_connect(widget, "notify::selected",
                     G_CALLBACK(on_dropdown_applied), options);
    return widget;
}

GtkWidget* make_entry(const std::string& path, const char* key,
                      const std::string& initial) {
    auto* widget = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(widget), initial.c_str());
    attach_setting(widget, path, key);
    g_signal_connect(widget, "changed", G_CALLBACK(on_entry_applied), nullptr);
    return widget;
}

// ── Page scaffolding ─────────────────────────────────────────

GtkWidget* make_page_grid() {
    auto* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_widget_set_margin_start(grid, 16);
    gtk_widget_set_margin_end(grid, 16);
    gtk_widget_set_margin_top(grid, 16);
    gtk_widget_set_margin_bottom(grid, 16);
    return grid;
}

void add_row(GtkGrid* grid, int row, const char* label, GtkWidget* control) {
    auto* caption = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(caption), 0.0);
    gtk_widget_set_hexpand(control, TRUE);
    gtk_grid_attach(grid, caption, 0, row, 1, 1);
    gtk_grid_attach(grid, control, 1, row, 1, 1);
}

int index_of(const char* const* options, int n, const std::string& value) {
    for (int i = 0; i < n; ++i)
        if (value == options[i]) return i;
    return 0; // unknown value: show the first option, change nothing
}

// ── One builder per config section ───────────────────────────

GtkWidget* build_bar_page(const AppConfig& config,
                          const std::string& path) {
    auto* grid = GTK_GRID(make_page_grid());
    // "custom" = free position (drag the window, or set margins as
    // absolute x/y from the top-left corner — margins go up to 4000
    // so 4K screens are reachable, not just this 1080p one).
    static const char* kAnchors[] = {
        "top-left", "top-right", "bottom-left", "bottom-right", "custom",
    };
    int row = 0;
    add_row(grid, row++, "Position",
            make_dropdown(path, "overlay.anchor", kAnchors, 5,
                          index_of(kAnchors, 5, config.overlay.anchor)));
    add_row(grid, row++, "Horizontal margin",
            make_spin(path, "overlay.margin_x", config.overlay.margin_x,
                      0, 4000));
    add_row(grid, row++, "Vertical margin",
            make_spin(path, "overlay.margin_y", config.overlay.margin_y,
                      0, 4000));
    add_row(grid, row++, "Opacity",
            make_scale(path, "overlay.opacity", config.overlay.opacity,
                       0.1, 1.0, 2));
    add_row(grid, row++, "Max countdowns",
            make_spin(path, "overlay.max_countdowns",
                      config.overlay.max_countdowns, 1, 5));
    add_row(grid, row++, "Only over the game (hide on alt-tab)",
            make_switch(path, "overlay.show_only_when_game_focused",
                        config.overlay.show_only_when_game_focused));
    return GTK_WIDGET(grid);
}

GtkWidget* build_panel_page(const AppConfig& config,
                            const std::string& path) {
    auto* grid = GTK_GRID(make_page_grid());
    static const char* kAnchors[] = {
        "left", "right", "top-left", "top-right", "bottom-left",
        "bottom-right", "custom",
    };
    int row = 0;
    add_row(grid, row++, "Show goals panel",
            make_switch(path, "panel.visible", config.panel.visible));
    add_row(grid, row++, "Position",
            make_dropdown(path, "panel.anchor", kAnchors, 7,
                          index_of(kAnchors, 7, config.panel.anchor)));
    add_row(grid, row++, "Horizontal margin",
            make_spin(path, "panel.margin_x", config.panel.margin_x,
                      0, 4000));
    add_row(grid, row++, "Opacity",
            make_scale(path, "panel.opacity", config.panel.opacity,
                       0.1, 1.0, 2));
    return GTK_WIDGET(grid);
}

GtkWidget* build_alarms_page(const AppConfig& config,
                             const std::string& path) {
    auto* grid = GTK_GRID(make_page_grid());
    const AlarmsConfig& alarms = config.alarms;
    int row = 0;
    add_row(grid, row++, "Alarms enabled",
            make_switch(path, "alarms.enabled", alarms.enabled));
    add_row(grid, row++, "Volume",
            make_scale(path, "alarms.volume", alarms.volume, 0, 100, 0));
    add_row(grid, row++, "Warn before (min)",
            make_spin(path, "alarms.warn_before_min", alarms.warn_before_min,
                      1, 120));
    add_row(grid, row++, "Daily reset",
            make_switch(path, "alarms.daily", alarms.daily));
    add_row(grid, row++, "Weekly reset",
            make_switch(path, "alarms.weekly", alarms.weekly));
    add_row(grid, row++, "Guild Dungeon",
            make_switch(path, "alarms.gdg", alarms.gdg));
    add_row(grid, row++, "World Boss",
            make_switch(path, "alarms.world_boss", alarms.world_boss));
    add_row(grid, row++, "Nation War",
            make_switch(path, "alarms.nation_war", alarms.nation_war));
    return GTK_WIDGET(grid);
}

GtkWidget* build_dgcheck_page(const AppConfig& config,
                                const std::string& path) {
    auto* grid = GTK_GRID(make_page_grid());
    const DgcheckConfig& zone = config.dgcheck;
    int row = 0;
    add_row(grid, row++, "DG check enabled",
            make_switch(path, "dgcheck.enabled", zone.enabled));
    add_row(grid, row++, "Require CTRL",
            make_switch(path, "dgcheck.require_ctrl", zone.require_ctrl));
    add_row(grid, row++, "Zone width",
            make_spin(path, "dgcheck.width", zone.width, 16, 2000));
    add_row(grid, row++, "Zone height",
            make_spin(path, "dgcheck.height", zone.height, 16, 1200));

    // Snapshot of the captured zone. The numbers refresh when this
    // window reopens; calibration itself is one click anywhere on
    // screen, reported back through a desktop notification.
    const std::string zone_text = std::format("Captured zone: ({}, {}) — {}×{}",
                                              zone.x, zone.y, zone.width, zone.height);
    auto* zone_label = gtk_label_new(zone_text.c_str());
    gtk_label_set_xalign(GTK_LABEL(zone_label), 0.0);
    gtk_grid_attach(grid, zone_label, 0, row, 2, 1);
    ++row;

    auto* calibrate = gtk_button_new_with_label("Capture click zone…");
    gtk_widget_set_tooltip_text(
        calibrate, "Arms capture: the next primary click anywhere on "
                   "screen becomes the zone center and enables the "
                   "DG check");
    g_signal_connect(calibrate, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer) {
                         g_action_group_activate_action(
                             G_ACTION_GROUP(g_app), "calibrate-zone", nullptr);
                     }), nullptr);
    gtk_grid_attach(grid, calibrate, 0, row, 2, 1);
    return GTK_WIDGET(grid);
}

GtkWidget* build_hotkey_page(const AppConfig& config,
                             const std::string& path) {
    auto* grid = GTK_GRID(make_page_grid());
    static const char* kModes[] = {"external", "evdev", "disabled"};
    int row = 0;
    add_row(grid, row++, "Mode",
            make_dropdown(path, "hotkey.mode", kModes, 3,
                          index_of(kModes, 3,
                                   config.hotkey.mode == HotkeyMode::External
                                       ? "external"
                                       : config.hotkey.mode == HotkeyMode::Evdev
                                             ? "evdev"
                                             : "disabled")));
    add_row(grid, row++, "Combo (evdev mode)",
            make_entry(path, "hotkey.combo", config.hotkey.combo));
    return GTK_WIDGET(grid);
}

} // anonymous namespace

namespace settings {

void present(GtkApplication* app, const AppConfig& config,
             const std::string& config_path) {
    if (g_window != nullptr) {
        gtk_window_present(g_window); // singleton: focus, don't duplicate
        return;
    }

    g_app = app;
    g_window = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(g_window, "Cabal Overlay Settings");
    gtk_window_set_default_size(g_window, 460, 380);

    auto* notebook = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_bar_page(config, config_path),
                             gtk_label_new("Bar"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_panel_page(config, config_path),
                             gtk_label_new("Panel"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_alarms_page(config, config_path),
                             gtk_label_new("Alarms"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_hotkey_page(config, config_path),
                             gtk_label_new("Hotkey"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_dgcheck_page(config, config_path),
                             gtk_label_new("DG Check"));

    // Bottom bar: actions that affect the whole app rather than one
    // config section. Quit goes through the D-Bus-exported GAction so
    // there is exactly one shutdown path, however it is triggered.
    auto* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(root), notebook);
    auto* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(footer, 16);
    gtk_widget_set_margin_end(footer, 16);
    gtk_widget_set_margin_top(footer, 8);
    gtk_widget_set_margin_bottom(footer, 8);
    auto* quit_button = gtk_button_new_with_label("Quit overlay");
    gtk_widget_set_tooltip_text(quit_button, "Shut down the overlay "
                                              "(also: D-Bus action quit)");
    g_signal_connect(quit_button, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer) {
                         g_action_group_activate_action(G_ACTION_GROUP(g_app),
                                                        "quit", nullptr);
                     }), nullptr);
    gtk_box_append(GTK_BOX(footer), quit_button);
    gtk_box_append(GTK_BOX(root), footer);
    gtk_window_set_child(g_window, root);

    // Drop the singleton when the window closes so the next open
    // re-populates from a fresh config snapshot.
    g_signal_connect(g_window, "close-request",
                     G_CALLBACK(+[](GtkWindow*, gpointer) -> gboolean {
                         g_window = nullptr;
                         return FALSE; // let the default close proceed
                     }), nullptr);

    gtk_window_present(g_window);
}

} // namespace settings
