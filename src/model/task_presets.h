// ─────────────────────────────────────────────────────────────
// task_presets.h — named preset task lists and their runtime state
//
// Presets are read-only task templates shipped in data/task_lists.json
// (editable by any clan, exactly like the dungeon catalog): a list has
// a name and a fixed set of tasks. The user can reorder a preset's
// tasks and track progress on them, but never add or remove — the
// content belongs to the template. "Custom" is the classic behavior:
// the free-form task list in state.json.
//
// Preset progress lives in its OWN file (presets.json next to
// state.json), never in state.json: that file mirrors the clan
// tracker's schema and must stay import-compatible with the web tool.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "model/dungeons.h"
#include "model/tasks.h"

// One task inside a preset template.
struct PresetTask {
    std::string name;
    TaskType type = TaskType::Daily;
    // 0 in the file means "use the catalog's maxRuns for this dungeon"
    // when the name matches; still 0 when it does not (checkbox task).
    int goal = 0;
};

struct TaskPreset {
    std::string id;
    std::string name;
    std::vector<PresetTask> tasks;
};

// Loads data/task_lists.json, resolving goal=0 entries against the
// dungeon catalog. Throws on unreadable/malformed JSON; callers that
// treat presets as optional (we do) catch and continue with no lists.
std::vector<TaskPreset> load_task_presets(const std::string& path,
                                          const std::vector<Dungeon>& catalog);

// ── Runtime state (presets.json) ─────────────────────────────

// Per-list user data: a display-order override plus progress, keyed
// by task name (names are unique inside a preset).
struct PresetListProgress {
    std::vector<std::string> order;       // empty = definition order
    std::map<std::string, int> counts;
    std::map<std::string, bool> completed;
};

struct PresetState {
    std::string active_list = "custom"; // "custom" or a preset id
    std::map<std::string, PresetListProgress> lists;
    // Reset markers for preset progress, independent of state.json's
    // (the two files are written independently and could drift).
    std::string last_daily_reset;
    std::string last_weekly_reset;
};

// presets.json next to state.json (XDG data dir / %APPDATA%).
std::filesystem::path default_presets_path();

// Missing file → fresh state. Malformed JSON → throws std::runtime_error.
PresetState load_preset_state(const std::filesystem::path& path);
void save_preset_state(const std::filesystem::path& path,
                       const PresetState& state);

// Mirror of apply_resets for preset progress: when a daily/weekly
// boundary passed (server time), clears counts and completed flags of
// the matching task type in every list. Returns true on any change.
bool apply_preset_resets(PresetState& state,
                         const std::vector<TaskPreset>& presets,
                         std::chrono::system_clock::time_point now);
