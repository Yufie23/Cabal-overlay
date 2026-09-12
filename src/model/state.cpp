// ─────────────────────────────────────────────────────────────
// state.cpp — state.json load/save, plus reset detection.
//
// Serialization uses nlohmann::json. The to_json/from_json free
// functions below are found automatically by the library through
// "argument-dependent lookup" (ADL): nlohmann calls them because
// they live in the same namespace as the type they convert. This
// is the library's idiom — do not move them into a class.
// ─────────────────────────────────────────────────────────────

#include "state.h"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "time/server_time.h"

namespace {

constexpr double kSchemaVersion = 6.1; // keep in sync with the tracker

const char* to_string(TaskType type) {
    return type == TaskType::Daily ? "daily" : "weekly";
}

TaskType task_type_from(const std::string& text) {
    return text == "weekly" ? TaskType::Weekly : TaskType::Daily;
}

} // anonymous namespace

// These two live at GLOBAL scope on purpose: nlohmann finds them by
// argument-dependent lookup, and for a global-namespace type like
// Task that search only covers the global namespace — hiding them in
// an anonymous namespace makes them invisible to ADL.
void to_json(nlohmann::json& j, const Task& task) {
    j = nlohmann::json{
        {"id", task.id},
        {"type", to_string(task.type)},
        {"name", task.name},
        {"completed", task.completed},
        {"goal", task.goal},
        {"count", task.count},
        {"notes", task.notes},
    };
}

// And back. Every field falls back to a sane default when absent,
// so an older or partial file still loads (mirrors the tracker's
// own defensive import).
void from_json(const nlohmann::json& j, Task& task) {
    task.id        = j.value("id", "");
    task.type      = task_type_from(j.value("type", "daily"));
    task.name      = j.value("name", "");
    task.completed = j.value("completed", false);
    task.goal      = j.value("goal", 0);
    task.count     = j.value("count", 0);
    task.notes     = j.value("notes", "");
}

std::filesystem::path default_state_path() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME"))
        return std::filesystem::path{xdg} / "cabal-overlay" / "state.json";
    if (const char* home = std::getenv("HOME"))
        return std::filesystem::path{home} / ".local/share/cabal-overlay/state.json";
    throw std::runtime_error("cannot resolve state path: HOME is not set");
}

AppState load_state(const std::filesystem::path& path) {
    std::ifstream file{path};
    if (!file) return {}; // first run: fresh state, not an error

    const nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded())
        throw std::runtime_error{"malformed state file: " + path.string()};

    AppState state;
    if (root.contains("tasks") && root["tasks"].is_array()) {
        for (const nlohmann::json& j : root["tasks"]) {
            Task task;
            from_json(j, task);
            if (task.id.empty()) continue; // skip entries without identity
            state.tasks.all().push_back(std::move(task));
        }
    }
    state.last_daily_reset  = root.value("lastDailyResetDate", "");
    state.last_weekly_reset = root.value("lastWeeklyResetDate", "");

    // Ids must never collide with loaded ones: move the counter
    // past the highest numeric id in the file.
    state.tasks.seed_id_counter();
    return state;
}

void save_state(const std::filesystem::path& path, const AppState& state) {
    // Assignments (not one big brace-init) so the compiler uses the
    // ADL to_json hook for each Task inside the vector.
    nlohmann::json root;
    root["version"] = kSchemaVersion;
    root["tasks"] = state.tasks.all();
    root["lastDailyResetDate"] = state.last_daily_reset;
    root["lastWeeklyResetDate"] = state.last_weekly_reset;

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream file{path, std::ios::trunc};
    if (!file)
        throw std::runtime_error{"cannot write state file: " + path.string()};
    file << root.dump(2) << '\n';
    if (!file)
        throw std::runtime_error{"failed writing state file: " + path.string()};
}

bool apply_resets(AppState& state, std::chrono::system_clock::time_point now) {
    bool changed = false;

    if (state.last_daily_reset != berlin_date_string(now)) {
        changed |= state.tasks.reset(TaskType::Daily);
        state.last_daily_reset = berlin_date_string(now);
        changed = true;
    }
    if (state.last_weekly_reset != berlin_weekly_reset_string(now)) {
        changed |= state.tasks.reset(TaskType::Weekly);
        state.last_weekly_reset = berlin_weekly_reset_string(now);
        changed = true;
    }
    return changed;
}
