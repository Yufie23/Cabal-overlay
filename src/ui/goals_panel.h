// ─────────────────────────────────────────────────────────────
// ui.h — the goals panel widget tree
//
// Pure view layer: it knows how to (re)build the GTK widget tree of
// the goals panel (task rows with bump buttons) and the "add task"
// form, but it owns no state. main.cpp injects WHAT happens on user
// actions through GoalsActions (the React "callback props" pattern:
// the view raises events, the owner mutates state), decides WHEN to
// rebuild (via goals_signature) and WHETHER the panel is visible.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "model/dungeons.h"
#include "model/state.h"

// Actions the panel may request. Wired by main.cpp to the app's
// state; the view layer never touches AppState directly.
struct GoalsActions {
    // delta +1/-1 on a task's run counter.
    std::function<void(const std::string& task_id, int delta)> bump_count;
    // Check/uncheck a plain (goal-less) task.
    std::function<void(const std::string& task_id, bool completed)> set_completed;
    // Create a new task.
    std::function<void(TaskType type, const std::string& name, int goal)> add_task;
};

// Fingerprint of everything the task rows render: ids, names, types,
// counters, goals, flags. Cheap to compute, cheap to compare; when
// it differs from the last render, the rows must be rebuilt. This is
// the "key" pattern from React: a coarse diff upfront, then a full
// rebuild instead of fine-grained mutation.
std::string goals_signature(const AppState& state);

// Rebuilds the task rows from scratch inside `goals_box`: drops every
// child and re-creates the DAILY/WEEKLY section headers and rows
// (name + counter + bump buttons + progress bar). Only the goals_box
// is touched — the caller packs the add-task form and its toggle
// button OUTSIDE this box so an open form survives rebuilds.
void goals_panel_refresh(GtkWidget* goals_box, const AppState& state,
                         const GoalsActions& actions);

// Builds the collapsible "add task" form, initially hidden. Packed by
// the caller below the goals box. `dungeons` must outlive the form
// (main keeps it for the app's whole lifetime).
GtkWidget* goals_add_form_new(const GoalsActions& actions,
                              const std::vector<Dungeon>& dungeons);
