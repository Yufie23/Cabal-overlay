// ─────────────────────────────────────────────────────────────
// active_tasks.h — preset-list logic as pure functions
//
// Extracted from main.cpp: everything here takes its inputs by
// argument and touches no globals, which keeps the composition root
// small and makes the logic testable in isolation.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <string>
#include <vector>

#include "model/task_presets.h"
#include "model/tasks.h"

// First preset with this id, or nullptr.
const TaskPreset* find_preset(const std::vector<TaskPreset>& presets,
                              const std::string& id);

// Preset row ids are "p:<list id>:<task name>" — stable across
// refreshes so progress, ghosts and drag-and-drop all resolve.
// This strips the prefix back to the bare task name.
std::string preset_task_name(const TaskPreset& preset,
                             const std::string& id);

// Builds the displayable list for the active source: a copy of
// custom_tasks, or the preset definition plus the user's progress
// and order override (read from preset_state). Template edits are
// tolerated both ways: names the override does not know are
// appended, names the template dropped are skipped.
TaskList materialize_active_tasks(const std::vector<TaskPreset>& presets,
                                  PresetState& preset_state,
                                  const TaskList& custom_tasks);
