// ─────────────────────────────────────────────────────────────
// task_presets.cpp — preset template loading and preset state I/O.
// The only file that knows the task_lists.json and presets.json
// schemas.
// ─────────────────────────────────────────────────────────────

#include "task_presets.h"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <utility>

#include <glib.h> // g_message / g_warning for resolution logging
#include <nlohmann/json.hpp>

#include "time/server_time.h"

namespace {

TaskType parse_type(const nlohmann::json& node, const std::string& list_id) {
    const std::string type = node.value("type", "daily");
    if (type == "daily") return TaskType::Daily;
    if (type == "weekly") return TaskType::Weekly;
    throw std::runtime_error("preset '" + list_id + "': unknown task type '" +
                             type + "'");
}

} // anonymous namespace

std::vector<TaskPreset> load_task_presets(const std::string& path,
                                          const std::vector<Dungeon>& catalog) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("cannot open preset lists: " + path);

    const nlohmann::json root = nlohmann::json::parse(input);

    std::vector<TaskPreset> presets;
    for (const auto& list_node : root.at("lists")) {
        TaskPreset preset;
        list_node.at("id").get_to(preset.id);
        list_node.at("name").get_to(preset.name);
        for (const auto& task_node : list_node.at("tasks")) {
            PresetTask task;
            task_node.at("name").get_to(task.name);
            task.type = parse_type(task_node, preset.id);
            task.goal = task_node.value("goal", 0);

            // Smart resolution: typos, case, short codes and partial
            // names canonize to the tracker's exact dungeon name (so
            // short codes display and the catalog goal applies). Only
            // UNIQUE matches are accepted — an unresolvable name stays
            // literal and works as a plain task, with a loud warning.
            if (const Dungeon* match = find_dungeon_smart(catalog, task.name)) {
                if (match->name != task.name)
                    g_message("preset '%s': resolved '%s' -> '%s'",
                              preset.id.c_str(), task.name.c_str(),
                              match->name.c_str());
                task.name = match->name;
                if (task.goal == 0) task.goal = match->max_runs;
            } else {
                g_warning("preset '%s': no dungeon matches '%s' — "
                          "keeping it as a plain task",
                          preset.id.c_str(), task.name.c_str());
            }
            preset.tasks.push_back(std::move(task));
        }
        presets.push_back(std::move(preset));
    }
    return presets;
}

std::filesystem::path default_presets_path() {
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA"))
        return std::filesystem::path{appdata} / "cabal-overlay" / "presets.json";
    throw std::runtime_error("cannot resolve presets path: APPDATA is not set");
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"))
        return std::filesystem::path{xdg} / "cabal-overlay" / "presets.json";
    if (const char* home = std::getenv("HOME"))
        return std::filesystem::path{home} / ".local/share/cabal-overlay/presets.json";
    throw std::runtime_error("cannot resolve presets path: HOME is not set");
#endif
}

PresetState load_preset_state(const std::filesystem::path& path) {
    std::ifstream file{path};
    if (!file) return {}; // first run: fresh state, not an error

    const nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded())
        throw std::runtime_error{"malformed presets file: " + path.string()};

    PresetState state;
    state.active_list = root.value("activeList", "custom");
    state.last_daily_reset = root.value("lastDailyResetDate", "");
    state.last_weekly_reset = root.value("lastWeeklyResetDate", "");

    if (root.contains("lists") && root["lists"].is_object()) {
        for (const auto& [list_id, node] : root["lists"].items()) {
            PresetListProgress progress;
            if (node.contains("order") && node["order"].is_array())
                node["order"].get_to(progress.order);
            if (node.contains("counts") && node["counts"].is_object())
                node["counts"].get_to(progress.counts);
            if (node.contains("completed") && node["completed"].is_object())
                node["completed"].get_to(progress.completed);
            state.lists[list_id] = std::move(progress);
        }
    }
    return state;
}

void save_preset_state(const std::filesystem::path& path,
                       const PresetState& state) {
    nlohmann::json root;
    root["activeList"] = state.active_list;
    root["lastDailyResetDate"] = state.last_daily_reset;
    root["lastWeeklyResetDate"] = state.last_weekly_reset;
    nlohmann::json lists = nlohmann::json::object();
    for (const auto& [list_id, progress] : state.lists) {
        nlohmann::json node;
        node["order"] = progress.order;
        node["counts"] = progress.counts;
        node["completed"] = progress.completed;
        lists[list_id] = std::move(node);
    }
    root["lists"] = std::move(lists);

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream file{path, std::ios::trunc};
    if (!file)
        throw std::runtime_error{"cannot write presets file: " + path.string()};
    file << root.dump(2) << '\n';
    if (!file)
        throw std::runtime_error{"failed writing presets file: " + path.string()};
}

bool apply_preset_resets(PresetState& state,
                         const std::vector<TaskPreset>& presets,
                         std::chrono::system_clock::time_point now) {
    const bool daily = state.last_daily_reset != berlin_date_string(now);
    const bool weekly =
        state.last_weekly_reset != berlin_weekly_reset_string(now);
    if (!daily && !weekly) return false;

    for (const TaskPreset& preset : presets) {
        PresetListProgress& progress = state.lists[preset.id];
        for (const PresetTask& task : preset.tasks) {
            if ((daily && task.type == TaskType::Daily) ||
                (weekly && task.type == TaskType::Weekly)) {
                progress.counts.erase(task.name);
                progress.completed.erase(task.name);
            }
        }
    }
    // A boundary passed: the markers changed, so the caller persists
    // even when no list had progress to clear (mirrors apply_resets).
    if (daily) state.last_daily_reset = berlin_date_string(now);
    if (weekly) state.last_weekly_reset = berlin_weekly_reset_string(now);
    return true;
}
