// ─────────────────────────────────────────────────────────────
// goals_panel.cpp — goals panel and add-task form rendering
//
// The goals box is a vertical GtkBox. Each tracked dungeon becomes a
// "row": a horizontal line (short code left, counter right, bump buttons)
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

#include "goals_panel.h"

#include <algorithm>
#include <format>
#include <map>
#include <set>

#include "model/tasks.h"

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

struct RemovePayload {
    GoalsActions actions;
    std::string task_id;
};

// Drag-and-drop payload for the panel-wide drop target. The AppState
// pointer is owned by main.cpp and outlives every rebuild.
struct DropPayload {
    GoalsActions actions;
    const AppState* state;
    GtkWidget* box;
};

struct AddFormPayload {
    GoalsActions actions;
    // Owned by main.cpp, alive for the whole app — the form only
    // reads it to resolve a dropdown label back to a dungeon.
    const std::vector<Dungeon>* dungeons;
    GtkWidget* name_entry;
    GtkWidget* search_entry;
    GtkWidget* dungeon_dropdown;
    GtkWidget* type_dropdown;
    GtkWidget* goal_spin;
    GtkWidget* form;

    // The dungeon behind the dropdown's current selection. Positions
    // in the FILTERED model are not catalog positions, so selection is
    // resolved through the displayed label (unique — names are).
    const Dungeon* selected_dungeon() const {
        auto* item = gtk_drop_down_get_selected_item(
            GTK_DROP_DOWN(dungeon_dropdown)); // owned by the model
        if (item == nullptr) return nullptr;
        return find_dungeon_by_label(
            *dungeons, gtk_string_object_get_string(GTK_STRING_OBJECT(item)));
    }
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

void on_remove_clicked(GtkButton* button, gpointer) {
    // Contract: "cabal-payload" is a RemovePayload owned by the button.
    const auto* payload = static_cast<const RemovePayload*>(
        g_object_get_data(G_OBJECT(button), "cabal-payload"));
    payload->actions.remove_task(payload->task_id);
}

// ── Drag-and-drop reorder ────────────────────────────────────
// Every row is a drag source carrying its task id as a string; the
// goals box is a drop target. On drop we find the row under the
// pointer and slide the dragged task to that slot via TaskList::move
// (same reorder op the old arrow buttons used — the model never
// learned about drag and drop).

GdkContentProvider* on_drag_prepare(GtkDragSource*, double, double,
                                    gpointer row_ptr) {
    // Contract: row_ptr is a task row widget built by make_row; its
    // "cabal-task-id" holds the id string, owned by the row.
    const auto* row = GTK_WIDGET(row_ptr);
    const char* id = static_cast<const char*>(
        g_object_get_data(G_OBJECT(row), "cabal-task-id"));
    if (id == nullptr) return nullptr; // header or foreign widget: cancel
    return gdk_content_provider_new_typed(G_TYPE_STRING, id);
}

// Position of a task in the list, or -1 when absent.
int index_of(const TaskList& tasks, const char* id) {
    const auto& all = tasks.all();
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].id == id) return static_cast<int>(i);
    return -1;
}

gboolean on_box_drop(GtkDropTarget*, const GValue* value, double,
                     double y, gpointer payload_ptr) {
    const auto* payload = static_cast<const DropPayload*>(payload_ptr);
    const char* dragged_id = g_value_get_string(value);
    if (dragged_id == nullptr) return FALSE;

    const TaskList& tasks = payload->state->tasks;
    const int source = index_of(tasks, dragged_id);
    if (source < 0) return FALSE;

    // First row whose vertical midpoint is below the drop point:
    // insert before it. Bounds are computed in the box's coordinate
    // space — exactly the space of the drop `y`. No row matched =
    // drop below the last row = end of the list.
    int insert_at = static_cast<int>(tasks.all().size());
    for (GtkWidget* child = gtk_widget_get_first_child(payload->box);
         child != nullptr;
         child = gtk_widget_get_next_sibling(child)) {
        const char* child_id = static_cast<const char*>(
            g_object_get_data(G_OBJECT(child), "cabal-task-id"));
        if (child_id == nullptr) continue; // section header
        graphene_rect_t bounds;
        if (!gtk_widget_compute_bounds(child, payload->box, &bounds))
            continue;
        if (y < bounds.origin.y + bounds.size.height / 2.0f) {
            insert_at = index_of(tasks, child_id);
            break;
        }
    }
    if (insert_at < 0) return FALSE;

    // Slide delta in ORIGINAL list coordinates: removing the dragged
    // task shifts everything after it one slot, so a downward move
    // lands one earlier than the raw difference suggests.
    const int delta = source < insert_at ? insert_at - 1 - source
                                         : insert_at - source;
    if (delta != 0) payload->actions.move_task(dragged_id, delta);
    return TRUE;
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

    // The dropdown's selection is a position in the FILTERED model,
    // not the catalog — resolve it through the displayed label.
    const Dungeon* selected = payload->selected_dungeon();
    if (name.empty() && selected != nullptr) name = selected->name;

    const TaskType type = gtk_drop_down_get_selected(GTK_DROP_DOWN(payload->type_dropdown)) == 0
        ? TaskType::Daily
        : TaskType::Weekly;
    const int goal = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(payload->goal_spin));

    if (name.empty()) return; // Nothing to add; keep the form open.

    payload->actions.add_task(type, name, goal);
    gtk_editable_set_text(GTK_EDITABLE(payload->name_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(payload->search_entry), "");
    gtk_widget_set_visible(payload->form, FALSE);
}

void on_add_cancel_clicked(GtkButton*, gpointer form_ptr) {
    auto* form = GTK_WIDGET(form_ptr);
    const auto* payload = static_cast<const AddFormPayload*>(
        g_object_get_data(G_OBJECT(form), "cabal-payload"));
    // Clear the filter so the next open starts from the full catalog.
    gtk_editable_set_text(GTK_EDITABLE(payload->search_entry), "");
    gtk_widget_set_visible(form, FALSE);
}

// Prefills the goal spin with the selected dungeon's maxRuns, so
// picking "Abandoned City" already shows 30 — one less field to type.
void on_dungeon_selected(GObject*, GParamSpec*, gpointer user_data) {
    const auto* payload = static_cast<const AddFormPayload*>(user_data);
    const Dungeon* dungeon = payload->selected_dungeon();
    if (dungeon == nullptr) return; // no selection (e.g. model swapped)
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(payload->goal_spin),
                              static_cast<double>(dungeon->max_runs));
}

// Live filter for the catalog dropdown: rebuilt from scratch on every
// keystroke. GtkDropDown's built-in popup search only matches the
// model strings; our filter matches the full name OR the short code,
// case-insensitively, and typing happens in a normal entry in the
// panel — no popup grab involved (layer-shell surfaces handle popups
// poorly anyway). 86 strings rebuilt per keystroke is trivial.
void on_search_changed(GtkEditable* entry, gpointer user_data) {
    const auto* payload = static_cast<const AddFormPayload*>(user_data);
    const char* text = gtk_editable_get_text(entry);
    const std::string query = text != nullptr ? text : "";

    auto* model = gtk_string_list_new(nullptr);
    for (const Dungeon& dungeon : *payload->dungeons) {
        if (query.empty() || dungeon_matches(dungeon, query))
            gtk_string_list_append(model, dungeon_label(dungeon).c_str());
    }
    // set_model refs the list; unref leaves the dropdown as sole owner.
    // Selection resets to "nothing" — on_dungeon_selected ignores it.
    gtk_drop_down_set_model(GTK_DROP_DOWN(payload->dungeon_dropdown),
                            G_LIST_MODEL(model));
    g_object_unref(model);
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

// ── Fade-out for completed tasks ─────────────────────────────
// Completing a task drops its row on the next rebuild — an abrupt
// pop. Instead the row lingers as a "ghost": a frozen snapshot that
// fades to invisible over kFadeMs and is then detached. The snapshot
// survives both model changes and panel rebuilds: every rebuild
// re-creates the ghost widget from the map, so nothing can kill the
// animation halfway. View-layer memory only — the model knows
// nothing about it.

constexpr int kFadeMs = 300;

struct FadingRow {
    Task snapshot;               // copied: the model may delete the task
    gint64 started_us = 0;       // g_get_monotonic_time() base
    GtkWidget* widget = nullptr; // current ghost instance, if built
    GtkWidget* box = nullptr;    // parent box, to detach at the end
};

std::map<std::string, FadingRow> g_fading;
std::set<std::string> g_visible_last; // ids rendered in the last refresh
guint g_fade_ticker = 0;

double fade_fraction(const FadingRow& row) {
    const gint64 elapsed_us = g_get_monotonic_time() - row.started_us;
    return std::clamp(1.0 - static_cast<double>(elapsed_us) / (kFadeMs * 1000),
                      0.0, 1.0);
}

void on_ghost_destroyed(gpointer data, GObject*) {
    // Contract: data is a FadingRow owned by the g_fading map; the
    // ghost widget died (panel rebuild) — forget the dangling pointer.
    static_cast<FadingRow*>(data)->widget = nullptr;
}

void detach_ghost(FadingRow& row) {
    if (row.widget == nullptr) return;
    // Unregister the weak ref BEFORE removing: the removal destroys
    // the widget, and no callback may run once the map entry is gone.
    g_object_weak_unref(G_OBJECT(row.widget), on_ghost_destroyed, &row);
    GtkWidget* widget = row.widget;
    row.widget = nullptr;
    gtk_box_remove(GTK_BOX(row.box), widget);
}

gboolean on_fade_tick(gpointer) {
    for (auto it = g_fading.begin(); it != g_fading.end();) {
        FadingRow& row = it->second;
        const double fraction = fade_fraction(row);
        if (fraction <= 0.0) {
            detach_ghost(row);
            it = g_fading.erase(it);
            continue;
        }
        if (row.widget != nullptr)
            gtk_widget_set_opacity(row.widget, fraction);
        ++it;
    }
    if (g_fading.empty()) {
        g_fade_ticker = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

void ensure_fade_ticker() {
    if (g_fade_ticker == 0)
        g_fade_ticker = g_timeout_add(30, on_fade_tick, nullptr);
}

// ── Task rows ────────────────────────────────────────────────

// A task is done when flagged, or when its counter reached its goal
// (tracker semantics). Done tasks are not deleted: they stay in the
// state file and the daily/weekly reset brings them back — the panel
// just stops drawing them.
bool row_is_done(const Task& task) {
    return task.completed ||
           (task.goal > 0 && task.count >= task.goal);
}

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

GtkWidget* make_remove_button(const GoalsActions& actions, const Task& task) {
    auto* button = gtk_button_new_with_label("✕");
    gtk_widget_add_css_class(button, "goal-bump");
    gtk_widget_set_tooltip_text(button, "Remove task");
    auto* payload = new RemovePayload{actions, task.id};
    g_object_set_data_full(G_OBJECT(button), "cabal-payload", payload,
                           delete_payload<RemovePayload>);
    g_signal_connect(button, "clicked", G_CALLBACK(on_remove_clicked), nullptr);
    return button;
}

GtkWidget* make_row(const Task& task, const GoalsActions& actions,
                    const std::vector<Dungeon>& dungeons) {
    auto* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(row, "goal-row");
    // The drag source reads the id from here; the drop target maps
    // rows back to positions. Owned by the row.
    g_object_set_data_full(G_OBJECT(row), "cabal-task-id",
                           g_strdup(task.id.c_str()), g_free);
    gtk_widget_set_tooltip_text(row, "Drag to reorder");

    // Drag source: a press-and-hold gesture on the row starts a drag
    // carrying the task id (GTK keeps clicks and drags separate, so
    // the buttons below still work normally).
    auto* drag_source = gtk_drag_source_new();
    g_signal_connect(drag_source, "prepare",
                     G_CALLBACK(on_drag_prepare), row);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(drag_source));

    auto* line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    // Rows show the clan's short code when the catalog has one; the
    // full name survives as the label's tooltip. The stored task name
    // stays the full name (tracker-schema compatibility).
    const std::string code = short_code_for(dungeons, task.name);
    auto* name = gtk_label_new(code.empty() ? task.name.c_str()
                                            : code.c_str());
    if (!code.empty())
        gtk_widget_set_tooltip_text(name, task.name.c_str());
    gtk_widget_add_css_class(name, "goal-name");
    gtk_label_set_xalign(GTK_LABEL(name), 0.0); // left-aligned
    gtk_widget_set_hexpand(name, TRUE);         // pushes the counter right

    // Goal tasks show "22/30"; plain checkbox tasks show a tick or a
    // dot. Reaching the goal counts as done even if the flag was
    // never set — row_is_done, same as TaskList::progress.
    const bool done = row_is_done(task);
    const std::string counter = task.goal > 0
        ? std::format("{}/{}", task.count, task.goal)
        : (done ? "✓" : "·");

    auto* count = gtk_label_new(counter.c_str());
    gtk_widget_add_css_class(count, "goal-count");
    gtk_label_set_xalign(GTK_LABEL(count), 1.0); // right-aligned

    gtk_box_append(GTK_BOX(line), name);
    gtk_box_append(GTK_BOX(line), count);
    // Goal tasks get +/- counter buttons; plain tasks get a single
    // done toggle. Every row gets a remove button; reordering is
    // drag-and-drop, not arrows. In click-through mode none of these
    // receive clicks — they matter only while interactive.
    if (task.goal > 0) {
        gtk_box_append(GTK_BOX(line), make_bump_button(actions, task, -1, "-"));
        gtk_box_append(GTK_BOX(line), make_bump_button(actions, task, 1, "+"));
    } else {
        gtk_box_append(GTK_BOX(line), make_toggle_button(actions, task));
    }
    gtk_box_append(GTK_BOX(line), make_remove_button(actions, task));

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

// Emits the section header on the first VISIBLE (not done) task of
// this type, so a type whose tasks are all done contributes nothing.
void append_section(GtkWidget* panel, const char* title, TaskType type,
                    const AppState& state, const GoalsActions& actions,
                    const std::vector<Dungeon>& dungeons) {
    bool first = true;
    for (const Task& task : state.tasks.all()) {
        if (task.type != type || row_is_done(task)) continue;
        if (first) {
            auto* header = gtk_label_new(title);
            gtk_widget_add_css_class(header, "goal-section");
            gtk_label_set_xalign(GTK_LABEL(header), 0.0);
            gtk_box_append(GTK_BOX(panel), header);
            first = false;
        }
        gtk_box_append(GTK_BOX(panel), make_row(task, actions, dungeons));
    }
}

// ── Add-task form ────────────────────────────────────────────

// The dropdown shows dungeon_label() strings ("AC — Abandoned City"):
// scannable short codes, full name for context, and both parts are
// searchable through the filter entry above the dropdown.
GtkStringList* make_dungeon_model(const std::vector<Dungeon>& dungeons) {
    // gtk_string_list_new copies every string it is given, so the
    // model outlives any temporary we build it from.
    std::vector<std::string> labels;
    labels.reserve(dungeons.size());
    for (const Dungeon& dungeon : dungeons)
        labels.push_back(dungeon_label(dungeon));
    std::vector<const char*> pointers;
    pointers.reserve(labels.size());
    for (const std::string& label : labels)
        pointers.push_back(label.c_str());
    pointers.push_back(nullptr); // NULL-terminated array, C-style.
    return gtk_string_list_new(pointers.data());
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
                         const GoalsActions& actions,
                         const std::vector<Dungeon>& dungeons) {
    // The drop target lives on the box itself, which persists across
    // rebuilds — install it exactly once (guarded by its own data
    // slot). Rows are drag SOURCES and get rebuilt every refresh, so
    // those are attached per-row in make_row instead.
    if (g_object_get_data(G_OBJECT(goals_box), "cabal-drop") == nullptr) {
        auto* payload = new DropPayload{actions, &state, goals_box};
        g_object_set_data_full(G_OBJECT(goals_box), "cabal-drop", payload,
                               delete_payload<DropPayload>);
        auto* drop = gtk_drop_target_new(G_TYPE_STRING, GDK_ACTION_COPY);
        // COPY, not MOVE: the protocol payload is a copied id string;
        // the actual reorder happens in the model. The drag source
        // offers COPY by default — demanding MOVE here leaves the two
        // action sets disjoint and every drop shows "not allowed".
        g_signal_connect(drop, "drop", G_CALLBACK(on_box_drop), payload);
        gtk_widget_add_controller(goals_box, GTK_EVENT_CONTROLLER(drop));
    }

    // Tasks that completed since the last render get a ghost instead
    // of vanishing (they were visible, they are done now).
    for (const Task& task : state.tasks.all()) {
        if (!row_is_done(task)) continue;
        if (g_visible_last.count(task.id) == 0) continue;
        if (g_fading.count(task.id) != 0) continue;
        g_fading[task.id] = FadingRow{ .snapshot = task,
                                       .started_us = g_get_monotonic_time(),
                                       .widget = nullptr,
                                       .box = goals_box };
    }
    // A daily/weekly reset may revive a task while its ghost is still
    // fading: cancel the ghost and let the real row render.
    for (auto it = g_fading.begin(); it != g_fading.end();) {
        const Task* revived = state.tasks.find(it->first);
        if (revived != nullptr && !row_is_done(*revived)) {
            detach_ghost(it->second);
            it = g_fading.erase(it);
        } else {
            ++it;
        }
    }

    // Drop every current child, then rebuild from scratch. Widget
    // trees are cheap to create; trying to diff-and-patch GTK nodes
    // would be far more code for zero perceptible gain at this size.
    while (GtkWidget* child = gtk_widget_get_first_child(goals_box))
        gtk_box_remove(GTK_BOX(goals_box), child);

    append_section(goals_box, "DAILY", TaskType::Daily, state, actions,
                   dungeons);
    append_section(goals_box, "WEEKLY", TaskType::Weekly, state, actions,
                   dungeons);

    // Ghosts re-create from the snapshot on every rebuild, so a
    // rebuild mid-fade restarts the widget but not the clock.
    for (auto& entry : g_fading) {
        FadingRow& row = entry.second;
        GtkWidget* ghost = make_row(row.snapshot, actions, dungeons);
        gtk_widget_set_sensitive(ghost, FALSE); // leaving: no input
        gtk_widget_set_opacity(ghost, fade_fraction(row));
        row.widget = ghost;
        g_object_weak_ref(G_OBJECT(ghost), on_ghost_destroyed, &row);
        gtk_box_append(GTK_BOX(goals_box), ghost);
    }
    if (!g_fading.empty()) ensure_fade_ticker();

    g_visible_last.clear();
    for (const Task& task : state.tasks.all())
        if (!row_is_done(task)) g_visible_last.insert(task.id);

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

    // Catalog filter: live, case-insensitive, matches the full name OR
    // the short code. Typing here rebuilds the dropdown model; the
    // built-in popup search was replaced because it only matches the
    // model strings and popups behave oddly on layer-shell surfaces.
    auto* search_entry = gtk_entry_new();
    gtk_widget_add_css_class(search_entry, "goal-form-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry),
                                   "Filter by name or short code…");
    gtk_box_append(GTK_BOX(form), search_entry);

    auto* dungeon_dropdown = gtk_drop_down_new(
        G_LIST_MODEL(make_dungeon_model(dungeons)), nullptr);
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
        .search_entry = search_entry,
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
    g_signal_connect(search_entry, "changed",
                     G_CALLBACK(on_search_changed), payload);

    gtk_widget_set_visible(form, FALSE);
    return form;
}
