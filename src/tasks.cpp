// ─────────────────────────────────────────────────────────────
// tasks.cpp — the model. Pure logic, no GTK, no disk I/O.
// ─────────────────────────────────────────────────────────────

#include "tasks.h"

#include <algorithm>
#include <stdexcept>

namespace {

// Tracker semantics: a task is done when flagged, or when its
// counter reached its goal.
bool is_done(const Task& task) {
    return task.completed ||
           (task.goal > 0 && task.count >= task.goal);
}

} // namespace

Task& TaskList::add(TaskType type, std::string name, int goal) {
    Task task;
    task.id   = next_id();
    task.type = type;
    task.name = std::move(name);
    task.goal = goal;
    m_tasks.push_back(std::move(task));
    return m_tasks.back();
}

bool TaskList::remove(const std::string& id) {
    const auto it = std::find_if(m_tasks.begin(), m_tasks.end(),
                                 [&](const Task& t) { return t.id == id; });
    if (it == m_tasks.end()) return false;
    m_tasks.erase(it);
    return true;
}

Task* TaskList::find(const std::string& id) {
    const auto it = std::find_if(m_tasks.begin(), m_tasks.end(),
                                 [&](const Task& t) { return t.id == id; });
    return it != m_tasks.end() ? &*it : nullptr;
}

const Task* TaskList::find(const std::string& id) const {
    const auto it = std::find_if(m_tasks.begin(), m_tasks.end(),
                                 [&](const Task& t) { return t.id == id; });
    return it != m_tasks.end() ? &*it : nullptr;
}

void TaskList::bump_count(const std::string& id, int delta) {
    Task* task = find(id);
    if (task == nullptr) return; // routine "not found", not an error

    task->count += delta;
    if (task->count < 0) task->count = 0;
    if (task->goal > 0) {
        task->count = std::min(task->count, task->goal); // capped at goal
        if (task->count >= task->goal) task->completed = true;
    }
}

void TaskList::set_completed(const std::string& id, bool completed) {
    if (Task* task = find(id)) task->completed = completed;
}

void TaskList::set_notes(const std::string& id, std::string notes) {
    if (Task* task = find(id)) task->notes = std::move(notes);
}

bool TaskList::reset(TaskType type) {
    bool changed = false;
    for (Task& task : m_tasks) {
        if (task.type != type) continue;
        if (task.completed || task.count != 0) changed = true;
        task.completed = false;
        task.count = 0;
    }
    return changed;
}

std::pair<int, int> TaskList::progress(TaskType type) const {
    int done = 0;
    int total = 0;
    for (const Task& task : m_tasks) {
        if (task.type != type) continue;
        ++total;
        if (is_done(task)) ++done;
    }
    return {done, total};
}

std::string TaskList::next_id() {
    return std::to_string(m_next_id++);
}

void TaskList::seed_id_counter() {
    for (const Task& task : m_tasks) {
        try {
            // stoull returns unsigned long long, which is a distinct
            // type from uint64_t on Linux: convert explicitly or
            // std::max cannot pick an overload.
            const auto id = static_cast<std::uint64_t>(std::stoull(task.id));
            m_next_id = std::max(m_next_id, id + 1);
        } catch (...) {
            // non-numeric id from a hand-edited file: ignore it
        }
    }
}
