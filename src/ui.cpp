// ─────────────────────────────────────────────────────────────
// ui.cpp — goals panel and add-task form rendering
//
// The goals box is a vertical GtkBox. Each tracked dungeon becomes a
// "row": a horizontal line (name left, counter right, bump buttons)
// plus, when the task has a goal, a GtkProgressBar underneath.
// Section headers (DAILY, WEEKLY) are emitted only for types that
// have at least one task.
//
// GTK signals force C-style callbacks with gpointer user data. The
// contract for every payload below: it is heap-allocated here,
// attached to its widget with g_object_set_data_full, and deleted by
// the GDestroyNotify when the widget is destroyed — so a payload
// never outlives its button, and panel rebuilds cannot dangle.
// ─────────────────────────────────────────────────────────────

#include "ui.h"

#include <cmath>
#include <format>
#include <map>

#include "tasks.h"

namespace {

// ── Button payloads ──────────────────────────────────────────
// Each payload carries its own copy of the actions plus the data
// the callback needs, so the trampoline below needs no globals.

struct BumpPayload {
    GoalsActions actions;
    std::string task_id;
    int delta;
};

struct TogglePayload {
    GoalsActions actions;
    std::string task_id;
};

struct AddFormPayload {
    GoalsActions actions;
    // Owned by main.cpp, alive for the whole app — the form only
    // reads it to map a dropdown index to a dungeon.
    const std::vector<Dungeon>* dungeons;
    GtkWidget* name_entry;
    GtkWidget* dungeon_dropdown;
    GtkWidget* type_dropdown;
    GtkWidget* goal_spin;
    GtkWidget* form;
};

template <typename T>
void delete_payload(gpointer data) {
    delete static_cast<T*>(data);
}

void on_bump_clicked(GtkButton* button, gpointer) {
    // Contract: "cabal-payload" on this button is a BumpPayload
    // owned by the button (freed by its GDestroyNotify).
    const auto* payload = static_cast<const BumpPayload*>(
        g_object_get_data(G_OBJECT(button), "cabal-payload"));
    payload->actions.bump_count(payload->task_id, payload->delta);
}

void on_task_toggled(GtkToggleButton* toggle, gpointer) {
    // Contract: "cabal-payload" on this toggle is a TogglePayload
    // owned by the toggle.
    const auto* payload = static_cast<const TogglePayload*>(
        g_object_get_data(G_OBJECT(toggle), "cabal-payload"));
    payload->actions.set_completed(payload->task_id,
                                   gtk_toggle_button_get_active(toggle) == TRUE);
}

void on_add_task_clicked(GtkButton*, gpointer form_ptr) {
    // Contract: user_data is the form widget built by
    // goals_add_form_new; its "cabal-payload" is the AddFormPayload.
    auto* form = GTK_WIDGET(form_ptr);
    const auto* payload = static_cast<const AddFormPayload*>(
        g_object_get_data(G_OBJECT(form), "cabal-payload"));

    // A typed custom name wins over the dropdown selection; the
    // dropdown is the fast path, the entry the escape hatch.
    const char* typed = gtk_editable_get_text(GTK_EDITABLE(payload->name_entry));
    std::string name = typed != nullptr ? typed : "";

    const guint dungeon_index =
        gtk_drop_down_get_selected(GTK_DROP_DOWN(payload->dungeon_dropdown));
    if (name.empty() && dungeon_index != GTK_INVALID_LIST_POSITION)
        name = (*payload->dungeons)[dungeon_index].name;

    const TaskType type = gtk_drop_down_get_selected(GTK_DROP_DOWN(payload->type_dropdown)) == 0
        ? TaskType::Daily
        : TaskType::Weekly;
    const int goal = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(payload->goal_spin));

    if (name.empty()) return; // Nothing to add; keep the form open.

    payload->actions.add_task(type, name, goal);
    gtk_editable_set_text(GTK_EDITABLE(payload->name_entry), "");
    gtk_widget_set_visible(payload->form, FALSE);
}

void on_add_cancel_clicked(GtkButton*, gpointer form_ptr) {
    gtk_widget_set_visible(GTK_WIDGET(form_ptr), FALSE);
}

// Prefills the goal spin with the selected dungeon's maxRuns, so
// picking "Abandoned City" already shows 30 — one less field to type.
void on_dungeon_selected(GObject* dropdown, GParamSpec*, gpointer user_data) {
    const auto* payload = static_cast<const AddFormPayload*>(user_data);
    const guint index = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    if (index == GTK_INVALID_LIST_POSITION) return;
    gtk_spin_button_set_value(
        GTK_SPIN_BUTTON(payload->goal_spin),
        static_cast<double>((*payload->dungeons)[index].max_runs));
}

// ── Progress bar animation ───────────────────────────────────
// Rows are rebuilt from scratch on every change, so a bumped bar
// would appear at its final value instantly — the ugly "saw". The
// view remembers the fraction it last rendered per task id and
// animates each rebuilt bar from that value to the new one. GTK
// has no CSS transitions; a short GLib timer is the whole engine.

constexpr int kAnimSteps = 10;
constexpr int kAnimIntervalMs = 30; // ≈300 ms per bump

struct ProgressAnimation {
    GtkProgressBar* bar;
    double from;
    double to;
    int step = 0;
    guint source_id = 0;
};

// Fraction last rendered, keyed by task id. View-local memory — the
// model (TaskList) has no business knowing what the screen showed.
std::map<std::string, double> g_last_fractions;

gboolean on_animation_tick(gpointer data) {
    auto* anim = static_cast<ProgressAnimation*>(data);
    if (++anim->step >= kAnimSteps) {
        gtk_progress_bar_set_fraction(anim->bar, anim->to);
        anim->source_id = 0;
        return G_SOURCE_REMOVE;
    }
    // Ease-in-out cubic: accelerates away from `from`, brakes into
    // `to`. Being an interpolation between the two endpoints it can
    // never overshoot the limit — unlike a spring/elastic curve.
    const double t = static_cast<double>(anim->step) / kAnimSteps;
    double eased;
    if (t < 0.5) {
        eased = 4.0 * t * t * t;
    } else {
        const double u = -2.0 * t + 2.0;
        eased = 1.0 - (u * u * u) / 2.0;
    }
    gtk_progress_bar_set_fraction(
        anim->bar, anim->from + (anim->to - anim->from) * eased);
    return G_SOURCE_CONTINUE;
}

void on_animation_bar_destroyed(gpointer data, GObject*) {
    // The bar was destroyed (panel rebuild, app exit) mid-animation:
    // kill its timer before freeing the payload. The main loop is
    // single-threaded, so no tick can race this.
    auto* anim = static_cast<ProgressAnimation*>(data);
    if (anim->source_id != 0) g_source_remove(anim->source_id);
    delete anim;
}

void animate_progress(GtkProgressBar* bar, double from, double to) {
    auto* anim = new ProgressAnimation{bar, from, to};
    anim->source_id = g_timeout_add(kAnimIntervalMs, on_animation_tick, anim);
    // A weak ref (not a strong one): we must NOT keep the bar alive,
    // only hear about its death to stop touching it.
    g_object_weak_ref(G_OBJECT(bar), on_animation_bar_destroyed, anim);
}

// ── Task rows ────────────────────────────────────────────────

GtkWidget* make_bump_button(const GoalsActions& actions, const Task& task,
                            int delta, const char* label) {
    auto* button = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(button, "goal-bump");
    auto* payload = new BumpPayload{actions, task.id, delta};
    g_object_set_data_full(G_OBJECT(button), "cabal-payload", payload,
                           delete_payload<BumpPayload>);
    g_signal_connect(button, "clicked", G_CALLBACK(on_bump_clicked), nullptr);
    return button;
}

GtkWidget* make_toggle_button(const GoalsActions& actions, const Task& task) {
    auto* toggle = gtk_toggle_button_new_with_label("✓");
    gtk_widget_add_css_class(toggle, "goal-bump");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), task.completed);
    auto* payload = new TogglePayload{actions, task.id};
    g_object_set_data_full(G_OBJECT(toggle), "cabal-payload", payload,
                           delete_payload<TogglePayload>);
    // Connected AFTER set_active so building the row never fires the
    // action for the initial state.
    g_signal_connect(toggle, "toggled", G_CALLBACK(on_task_toggled), nullptr);
    return toggle;
}

GtkWidget* make_row(const Task& task, const GoalsActions& actions) {
    auto* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(row, "goal-row");

    auto* line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

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
    // Goal tasks get +/- counter buttons; plain tasks get a single
    // done toggle. In click-through mode none of these receive
    // clicks — they matter only while interactive.
    if (task.goal > 0) {
        gtk_box_append(GTK_BOX(line), make_bump_button(actions, task, -1, "-"));
        gtk_box_append(GTK_BOX(line), make_bump_button(actions, task, 1, "+"));
    } else {
        gtk_box_append(GTK_BOX(line), make_toggle_button(actions, task));
    }

    gtk_box_append(GTK_BOX(row), line);

    if (task.goal > 0) {
        auto* progress = gtk_progress_bar_new();
        gtk_progress_bar_set_fraction(
            GTK_PROGRESS_BAR(progress),
            static_cast<double>(task.count) / static_cast<double>(task.goal));
        gtk_widget_set_size_request(progress, 240, 6);
        // Lets the refresh pass match this bar back to its task for
        // the bump animation. The string is owned by the widget.
        g_object_set_data_full(G_OBJECT(progress), "cabal-task-id",
                               g_strdup(task.id.c_str()), g_free);
        gtk_box_append(GTK_BOX(row), progress);
    }
    return row;
}

// Depth-first walk of a freshly rebuilt tree: every progress bar
// animates from the fraction the PREVIOUS render showed for the same
// task id, and the new render's targets are collected into `rendered`
// (which becomes the new view memory once the walk finishes).
void collect_bar_targets(GtkWidget* widget,
                         std::map<std::string, double>& rendered) {
    if (GTK_IS_PROGRESS_BAR(widget)) {
        if (const char* id = static_cast<const char*>(
                g_object_get_data(G_OBJECT(widget), "cabal-task-id"))) {
            const double target =
                gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(widget));
            const auto previous = g_last_fractions.find(id);
            if (previous != g_last_fractions.end() && previous->second != target) {
                // Reset the fresh bar to the OLD value in the same
                // rebuild pass — otherwise it paints one frame at
                // `target`, then snaps back to `from` on the first
                // tick, which reads as a bounce.
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(widget),
                                              previous->second);
                animate_progress(GTK_PROGRESS_BAR(widget),
                                 previous->second, target);
            }
            rendered[id] = target;
        }
        return; // a progress bar has no child widgets to visit
    }
    for (GtkWidget* child = gtk_widget_get_first_child(widget);
         child != nullptr;
         child = gtk_widget_get_next_sibling(child))
        collect_bar_targets(child, rendered);
}

// Emits the section header on the first task of this type, so a
// type with zero tasks contributes nothing at all.
void append_section(GtkWidget* panel, const char* title, TaskType type,
                    const AppState& state, const GoalsActions& actions) {
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
        gtk_box_append(GTK_BOX(panel), make_row(task, actions));
    }
}

// ── Add-task form ────────────────────────────────────────────

GtkStringList* make_dungeon_model(const std::vector<Dungeon>& dungeons) {
    // gtk_string_list_new copies every string it is given, so the
    // model outlives any temporary we build it from.
    std::vector<const char*> names;
    names.reserve(dungeons.size());
    for (const Dungeon& dungeon : dungeons)
        names.push_back(dungeon.name.c_str());
    names.push_back(nullptr); // NULL-terminated array, C-style.
    return gtk_string_list_new(names.data());
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

void goals_panel_refresh(GtkWidget* goals_box, const AppState& state,
                         const GoalsActions& actions) {
    // Drop every current child, then rebuild from scratch. Widget
    // trees are cheap to create; trying to diff-and-patch GTK nodes
    // would be far more code for zero perceptible gain at this size.
    while (GtkWidget* child = gtk_widget_get_first_child(goals_box))
        gtk_box_remove(GTK_BOX(goals_box), child);

    append_section(goals_box, "DAILY", TaskType::Daily, state, actions);
    append_section(goals_box, "WEEKLY", TaskType::Weekly, state, actions);

    // Swap in fresh animation memory: bars animate from the previous
    // render's fractions, and ids whose tasks were deleted fall out
    // of the map with this assignment.
    std::map<std::string, double> rendered;
    collect_bar_targets(goals_box, rendered);
    g_last_fractions = std::move(rendered);
}

GtkWidget* goals_add_form_new(const GoalsActions& actions,
                              const std::vector<Dungeon>& dungeons) {
    auto* form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(form, "goal-form");

    auto* title = gtk_label_new("New task");
    gtk_widget_add_css_class(title, "goal-form-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_append(GTK_BOX(form), title);

    // Searchable dropdown over the 86-tracker catalog (GTK 4.10+).
    auto* dungeon_dropdown = gtk_drop_down_new(
        G_LIST_MODEL(make_dungeon_model(dungeons)), nullptr);
    gtk_drop_down_set_enable_search(GTK_DROP_DOWN(dungeon_dropdown), TRUE);
    gtk_widget_set_hexpand(dungeon_dropdown, TRUE);
    gtk_box_append(GTK_BOX(form), dungeon_dropdown);

    // Escape hatch for names not in the catalog.
    auto* name_entry = gtk_entry_new();
    gtk_widget_add_css_class(name_entry, "goal-form-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "or type a custom name");
    gtk_box_append(GTK_BOX(form), name_entry);

    auto* options = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    const char* type_names[] = {"Daily", "Weekly", nullptr};
    auto* type_dropdown = gtk_drop_down_new(
        G_LIST_MODEL(gtk_string_list_new(type_names)), nullptr);
    gtk_widget_set_hexpand(type_dropdown, TRUE);
    gtk_box_append(GTK_BOX(options), type_dropdown);

    // 0 goal = plain checkbox task, same convention as the tracker.
    // Prefilled with the first catalog entry (the dropdown shows it
    // even before the user makes an explicit selection).
    auto* goal_spin = gtk_spin_button_new_with_range(0, 999, 1);
    gtk_widget_add_css_class(goal_spin, "goal-form-spin");
    if (!dungeons.empty())
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(goal_spin),
                                  static_cast<double>(dungeons.front().max_runs));
    gtk_box_append(GTK_BOX(options), goal_spin);

    gtk_box_append(GTK_BOX(form), options);

    auto* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    auto* add_button = gtk_button_new_with_label("Add");
    gtk_widget_add_css_class(add_button, "goal-form-add");
    gtk_box_append(GTK_BOX(buttons), add_button);
    auto* cancel_button = gtk_button_new_with_label("Cancel");
    gtk_box_append(GTK_BOX(buttons), cancel_button);
    gtk_box_append(GTK_BOX(form), buttons);

    // One payload for the whole form, freed with it.
    auto* payload = new AddFormPayload{
        .actions = actions,
        .dungeons = &dungeons,
        .name_entry = name_entry,
        .dungeon_dropdown = dungeon_dropdown,
        .type_dropdown = type_dropdown,
        .goal_spin = goal_spin,
        .form = form,
    };
    g_object_set_data_full(G_OBJECT(form), "cabal-payload", payload,
                           delete_payload<AddFormPayload>);

    // The "add" handler needs the payload; it reaches it through the
    // form itself, which is exactly what user_data points to.
    g_signal_connect(add_button, "clicked", G_CALLBACK(on_add_task_clicked), form);
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_add_cancel_clicked), form);
    g_signal_connect(dungeon_dropdown, "notify::selected",
                     G_CALLBACK(on_dungeon_selected), payload);

    gtk_widget_set_visible(form, FALSE);
    return form;
}
