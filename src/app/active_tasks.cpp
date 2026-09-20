// ─────────────────────────────────────────────────────────────
// active_tasks.cpp — implementations; see the header for contracts.
// ─────────────────────────────────────────────────────────────

#include "active_tasks.h"

#include <algorithm>
#include <utility>

const TaskPreset* find_preset(const std::vector<TaskPreset>& presets,
                              const std::string& id) {
    for (const TaskPreset& preset : presets)
        if (preset.id == id) return &preset;
    return nullptr;
}

std::string preset_task_name(const TaskPreset& preset,
                             const std::string& id) {
    const std::string prefix = "p:" + preset.id + ":";
    if (id.starts_with(prefix)) return id.substr(prefix.size());
    return id;
}

TaskList materialize_active_tasks(const std::vector<TaskPreset>& presets,
                                  PresetState& preset_state,
                                  const TaskList& custom_tasks) {
    const TaskPreset* preset = find_preset(presets, preset_state.active_list);
    if (preset == nullptr) return custom_tasks;

    TaskList list;
    PresetListProgress& progress = preset_state.lists[preset->id];
    std::vector<std::string> order = progress.order;
    for (const PresetTask& task : preset->tasks)
        if (std::ranges::find(order, task.name) == order.end())
            order.push_back(task.name);

    for (const std::string& name : order) {
        const PresetTask* def = nullptr;
        for (const PresetTask& task : preset->tasks)
            if (task.name == name) { def = &task; break; }
        if (def == nullptr) continue; // removed from the template since

        Task task;
        task.id = "p:" + preset->id + ":" + def->name;
        task.type = def->type;
        task.name = def->name;
        task.goal = def->goal;
        task.locked = true; // template content: no remove button
        if (auto it = progress.counts.find(name); it != progress.counts.end())
            task.count = it->second;
        if (auto it = progress.completed.find(name); it != progress.completed.end())
            task.completed = it->second;
        list.all().push_back(std::move(task));
    }
    return list;
}
