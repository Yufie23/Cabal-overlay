// ─────────────────────────────────────────────────────────────
// tasks.h — task model and operations.
//
// enum class (a "scoped enum") is like a union type of string
// literals in TS, but the values are checked at compile time and
// cannot accidentally be compared with ints or other enums —
// TaskType::Daily is its own type, not a number in disguise.
//
// The operations live in TaskList rather than as free functions
// so that the id counter stays encapsulated: nothing outside can
// hand out duplicate ids.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

enum class TaskType : std::uint8_t {
    Daily,
    Weekly,
};

struct Task {
    std::string id;
    TaskType type = TaskType::Daily;
    std::string name;
    bool completed = false;
    int goal = 0;   // 0 = no goal (plain checkbox task)
    int count = 0;  // progress counter toward the goal
    std::string notes;
    // Display-time flag, never serialized: preset tasks render without
    // the remove button (their content belongs to the template).
    bool locked = false;
};

class TaskList {
public:
    // Adds a task and returns it (stable reference into the vector;
    // callers must not hold it across further add() calls).
    Task& add(TaskType type, std::string name, int goal = 0);

    bool remove(const std::string& id);

    // Moves the task `delta` positions in the list (negative = toward
    // the front), sliding the tasks in between — like reordering rows
    // in a list UI, not swapping contents. Clamped at the ends.
    // Returns true when the task actually moved.
    bool move(const std::string& id, int delta);

    // nullptr when the id does not exist (a pointer instead of a
    // reference so "not found" is representable — checked access,
    // no exceptions for a routine lookup).
    [[nodiscard]] Task* find(const std::string& id);
    [[nodiscard]] const Task* find(const std::string& id) const;

    // Adds delta to the counter, clamped to [0, goal] when a goal
    // exists. Reaching the goal auto-completes the task, exactly
    // like the tracker does.
    void bump_count(const std::string& id, int delta);

    void set_completed(const std::string& id, bool completed);
    void set_notes(const std::string& id, std::string notes);

    // Clears completed flags and counters for one type — the daily
    // or weekly reset. Returns true when anything changed.
    bool reset(TaskType type);

    // {completed, total} for one type. A task with a goal counts
    // as completed once its counter reaches it, even if the flag
    // was never set — tracker semantics.
    [[nodiscard]] std::pair<int, int> progress(TaskType type) const;

    [[nodiscard]] const std::vector<Task>& all() const { return m_tasks; }
    [[nodiscard]] std::vector<Task>& all() { return m_tasks; }

    // Moves the id counter past the highest numeric id currently in
    // the list (called after loading a file, so new ids never
    // collide with loaded ones). Non-numeric ids are ignored.
    void seed_id_counter();

private:
    std::string next_id();

    std::vector<Task> m_tasks;
    std::uint64_t m_next_id = 1;
};
