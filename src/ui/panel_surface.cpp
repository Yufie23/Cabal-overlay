// ─────────────────────────────────────────────────────────────
// panel_surface.cpp — panel window assembly, extracted verbatim
// (modulo dependency injection) from main.cpp's build_goals_panel.
// ─────────────────────────────────────────────────────────────

#include "panel_surface.h"

#include <vector>

namespace {

// The surface's widgets, owned by this TU. Every consumer outside
// reaches them through the accessors, so teardown can null them
// safely.
GtkWindow* g_window = nullptr;
GtkWidget* g_outer = nullptr;       // styled outer box
GtkWidget* g_goals_box = nullptr;   // rebuilt task rows
GtkWidget* g_goals_form = nullptr;  // collapsible add-task form
GtkWidget* g_list_dropdown = nullptr;
GtkWidget* g_add_task_button = nullptr;
panel_surface::PanelCallbacks g_callbacks;

// Dropdown selection changed: index 0 is "custom", everything after
// maps 1:1 to the presets vector. The app decides what the choice
// means (persist + refresh) — the surface only reports it.
void on_switcher_selected(GObject* dropdown, GParamSpec*, gpointer) {
    const guint index = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    std::string id = "custom";
    if (index > 0 && index - 1 < g_callbacks.presets->size())
        id = (*g_callbacks.presets)[index - 1].id;
    if (id != *g_callbacks.active_list && g_callbacks.on_list_selected)
        g_callbacks.on_list_selected(id);
}

GtkWidget* make_header() {
    auto* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    // Preset list switcher: "Custom" is the free-form list from
    // state.json; every other entry is a fixed template from
    // data/task_lists.json. Only built when presets are installed.
    if (!g_callbacks.presets->empty()) {
        std::vector<const char*> names {"Custom"};
        for (const TaskPreset& preset : *g_callbacks.presets)
            names.push_back(preset.name.c_str()); // presets outlive the panel
        names.push_back(nullptr);
        g_list_dropdown = gtk_drop_down_new(
            G_LIST_MODEL(gtk_string_list_new(names.data())), nullptr);
        gtk_widget_add_css_class(g_list_dropdown, "goal-list-switch");
        gtk_widget_set_hexpand(g_list_dropdown, TRUE);
        guint selected = 0;
        for (std::size_t i = 0; i < g_callbacks.presets->size(); ++i)
            if ((*g_callbacks.presets)[i].id == *g_callbacks.active_list)
                selected = static_cast<guint>(i + 1);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_list_dropdown), selected);
        g_signal_connect(g_list_dropdown, "notify::selected",
                         G_CALLBACK(on_switcher_selected), nullptr);
        gtk_box_append(GTK_BOX(header), g_list_dropdown);
    }

    auto* names_button = gtk_button_new_with_label("Aa");
    gtk_widget_add_css_class(names_button, "goal-bump");
    gtk_widget_set_tooltip_text(names_button,
                                "Toggle short codes / full names");
    g_signal_connect(names_button, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer) {
                         if (g_callbacks.on_names_toggled)
                             g_callbacks.on_names_toggled();
                     }), nullptr);
    gtk_box_append(GTK_BOX(header), names_button);

    auto* collapse_button = gtk_button_new_with_label("▾");
    gtk_widget_add_css_class(collapse_button, "goal-bump");
    gtk_widget_set_tooltip_text(collapse_button,
                                "Show only the first 3 tasks / show all");
    g_signal_connect(collapse_button, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer) {
                         if (g_callbacks.on_collapse_toggled)
                             g_callbacks.on_collapse_toggled();
                     }), nullptr);
    gtk_box_append(GTK_BOX(header), collapse_button);
    return header;
}

GtkWidget* make_footer() {
    auto* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    g_add_task_button = gtk_button_new_with_label("＋ Add task");
    gtk_widget_add_css_class(g_add_task_button, "goal-add-toggle");
    gtk_widget_set_hexpand(g_add_task_button, TRUE);
    gtk_widget_set_halign(g_add_task_button, GTK_ALIGN_FILL);
    // Capture-less lambda → plain function pointer, so it can
    // serve as a GTK callback. Reads the module-global form.
    g_signal_connect(g_add_task_button, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer) {
                         gtk_widget_set_visible(g_goals_form, TRUE);
                     }), nullptr);

    auto* settings_button = gtk_button_new_with_label("⚙");
    gtk_widget_add_css_class(settings_button, "goal-bump");
    gtk_widget_set_tooltip_text(settings_button, "Open settings");
    g_signal_connect(settings_button, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer) {
                         if (g_callbacks.on_open_settings)
                             g_callbacks.on_open_settings();
                     }), nullptr);

    auto* help_button = gtk_button_new_with_label("?");
    gtk_widget_add_css_class(help_button, "goal-bump");
    gtk_widget_set_tooltip_text(help_button, "Quick tour (tutorial)");
    g_signal_connect(help_button, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer) {
                         if (g_callbacks.on_open_tutorial)
                             g_callbacks.on_open_tutorial();
                     }), nullptr);

    gtk_box_append(GTK_BOX(footer), g_add_task_button);
    gtk_box_append(GTK_BOX(footer), settings_button);
    gtk_box_append(GTK_BOX(footer), help_button);
    return footer;
}

} // anonymous namespace

namespace panel_surface {

GtkWindow* build(GtkApplication* app, const PanelCallbacks& callbacks) {
    g_callbacks = callbacks;

    GtkWidget* panel_window = gtk_application_window_new(app);
    g_window = GTK_WINDOW(panel_window);
    platform::overlay_init(g_window);
    // Drag-to-move like the clock bar; a tap on the panel does
    // nothing (unlike the bar, it is not a "done" button).
    platform::overlay_enable_drag(
        g_window,
        [](int margin_x, int margin_y) {
            if (g_callbacks.on_dragged)
                g_callbacks.on_dragged(margin_x, margin_y);
        },
        [] {});

    // Outer styled box; inside it, the task rows live in their
    // own box so rebuilds never touch the form or its toggle.
    g_outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(g_outer, "goals-panel");

    gtk_box_append(GTK_BOX(g_outer), make_header());

    g_goals_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(g_outer), g_goals_box);

    g_goals_form = goals_add_form_new(g_callbacks.actions,
                                      *g_callbacks.dungeons);

    gtk_box_append(GTK_BOX(g_outer), make_footer());
    gtk_box_append(GTK_BOX(g_outer), g_goals_form);

    gtk_window_set_child(g_window, g_outer);
    platform::overlay_apply_placement(g_window,
                                      g_callbacks.initial_placement);
    gtk_window_present(g_window);
    return g_window;
}

bool exists() { return g_window != nullptr; }
GtkWindow* window() { return g_window; }
GtkWidget* goals_box() { return g_goals_box; }
GtkWidget* goals_form() { return g_goals_form; }
GtkWidget* add_task_button() { return g_add_task_button; }

void destroy() {
    if (g_window != nullptr) gtk_window_destroy(g_window);
    g_window = nullptr;
    g_outer = nullptr;
    g_goals_box = nullptr;
    g_goals_form = nullptr;
    g_list_dropdown = nullptr;
    g_add_task_button = nullptr;
}

} // namespace panel_surface
