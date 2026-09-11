// ─────────────────────────────────────────────────────────────
// ui.cpp — goals panel rendering
//
// The panel is a vertical GtkBox. Each tracked dungeon becomes a
// "row": a horizontal line (name left, counter right) plus, when the
// task has a goal, a GtkProgressBar underneath. Section headers
// (DAILY, WEEKLY) are emitted only for types that have at least one
// task, so an all-daily list never shows an empty WEEKLY header.
// ─────────────────────────────────────────────────────────────

#include "ui.h"

#include <format>

#include "tasks.h"

namespace {

GtkWidget* make_row(const Task& task) {
    auto* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(row, "goal-row");

    auto* line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    auto* name = gtk_label_new(task.name.c_str());
    gtk_widget_add_css_class(name, "goal-name");
    gtk_label_set_xalign(GTK_LABEL(name), 0.0); // left-aligned
    gtk_widget_set_hexpand(name, TRUE);         // pushes the counter right

    // Goal tasks show "22/30"; plain checkbox tasks show a tick or a
    // dot. Reaching the goal counts as done even if the flag was
    // never set — tracker semantics, same as TaskList::progress.
    const bool done =
        task.completed || (task.goal > 0 && task.count >= task.goal);
    const std::string counter = task.goal > 0
        ? std::format("{}/{}", task.count, task.goal)
        : (done ? "✓" : "·");

    auto* count = gtk_label_new(counter.c_str());
    gtk_widget_add_css_class(count, "goal-count");
    gtk_label_set_xalign(GTK_LABEL(count), 1.0); // right-aligned

    gtk_box_append(GTK_BOX(line), name);
    gtk_box_append(GTK_BOX(line), count);
    gtk_box_append(GTK_BOX(row), line);

    if (task.goal > 0) {
        auto* progress = gtk_progress_bar_new();
        gtk_progress_bar_set_fraction(
            GTK_PROGRESS_BAR(progress),
            static_cast<double>(task.count) / static_cast<double>(task.goal));
        gtk_widget_set_size_request(progress, 240, 6);
        gtk_box_append(GTK_BOX(row), progress);
    }
    return row;
}

// Emits the section header on the first task of this type, so a
// type with zero tasks contributes nothing at all.
void append_section(GtkWidget* panel, const char* title, TaskType type,
                    const AppState& state) {
    bool first = true;
    for (const Task& task : state.tasks.all()) {
        if (task.type != type) continue;
        if (first) {
            auto* header = gtk_label_new(title);
            gtk_widget_add_css_class(header, "goal-section");
            gtk_label_set_xalign(GTK_LABEL(header), 0.0);
            gtk_box_append(GTK_BOX(panel), header);
            first = false;
        }
        gtk_box_append(GTK_BOX(panel), make_row(task));
    }
}

} // anonymous namespace

std::string goals_signature(const AppState& state) {
    std::string signature;
    for (const Task& task : state.tasks.all()) {
        // static_cast<int> on the enum: enum class does not convert
        // to int implicitly (that is the point of the scoped enum),
        // but for a fingerprint string any stable numeric form works.
        signature += std::format("{}:{}:{}:{}:{}:{};", task.id, task.name,
                                 static_cast<int>(task.type), task.count,
                                 task.goal, task.completed);
    }
    return signature;
}

void goals_panel_refresh(GtkWidget* panel, const AppState& state) {
    // Drop every current child, then rebuild from scratch. Widget
    // trees are cheap to create; trying to diff-and-patch GTK nodes
    // would be far more code for zero perceptible gain at this size.
    while (GtkWidget* child = gtk_widget_get_first_child(panel))
        gtk_box_remove(GTK_BOX(panel), child);

    append_section(panel, "DAILY", TaskType::Daily, state);
    append_section(panel, "WEEKLY", TaskType::Weekly, state);
}
