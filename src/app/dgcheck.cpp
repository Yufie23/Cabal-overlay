// ─────────────────────────────────────────────────────────────
// dgcheck.cpp — zone hit test + target selection
// ─────────────────────────────────────────────────────────────

#include "dgcheck.h"

namespace dgcheck {

bool counts_click(const DgcheckConfig& zone, int x, int y, bool ctrl) {
    if (!zone.enabled) return false;
    if (zone.require_ctrl && !ctrl) return false;
    // Half-open rectangle [x, x+width) × [y, y+height): a click on
    // the right/bottom edge is outside, same as CSS box sizing.
    return x >= zone.x && x < zone.x + zone.width &&
           y >= zone.y && y < zone.y + zone.height;
}

// The task a counted click bumps: the first VISIBLE (not done) daily
// task, or the first visible weekly one when no dailies remain. Done
// tasks stay in the list but are skipped — the next click moves on to
// the following task automatically. Empty id = nothing to bump.
std::string target_task_id(const TaskList& tasks) {
    for (const Task& task : tasks.all()) {
        const bool done = task.completed ||
                          (task.goal > 0 && task.count >= task.goal);
        if (task.type == TaskType::Daily && !done) return task.id;
    }
    for (const Task& task : tasks.all()) {
        const bool done = task.completed ||
                          (task.goal > 0 && task.count >= task.goal);
        if (task.type == TaskType::Weekly && !done) return task.id;
    }
    return {}; // empty id: no tasks tracked, nothing to bump
}

} // namespace dgcheck
