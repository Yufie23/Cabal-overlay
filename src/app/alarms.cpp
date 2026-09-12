// ─────────────────────────────────────────────────────────────
// alarms.cpp — warning-window detection
//
// For each scheduled event the question is: "is the NEXT occurrence
// close enough to warn about, and did we already warn about THAT
// occurrence?" Both halves matter — without the second, a 1-second
// tick would re-notify every second for five minutes.
// ─────────────────────────────────────────────────────────────

#include "alarms.h"

#include "time/schedule.h"

namespace {

// Maps a TOML alarm kind onto the matching [alarms] flag. Unknown
// kinds stay silent: better to miss an alarm than to invent config.
bool alarm_kind_enabled(const AlarmsConfig& config, const std::string& kind) {
    if (kind == "daily")      return config.daily;
    if (kind == "weekly")     return config.weekly;
    if (kind == "gdg")        return config.gdg;
    if (kind == "world_boss") return config.world_boss;
    if (kind == "nation_war") return config.nation_war;
    return false;
}

} // anonymous namespace

void AlarmTracker::check(
    const std::vector<ScheduleEvent>& events,
    const AlarmsConfig& config,
    std::chrono::system_clock::time_point now,
    const std::function<void(const ScheduleEvent&, int minutes)>& on_alarm) {

    if (!config.enabled) return;

    for (const ScheduleEvent& event : events) {
        if (!alarm_kind_enabled(config, event.alarm)) continue;

        const auto at = next_occurrence(event, now);
        if (!at.has_value()) continue;

        // Truncated minutes: "4:59 away" counts as 4 and warns with
        // warn_before_min = 5. Never rounds UP into warning early.
        const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(*at - now).count();
        if (minutes > config.warn_before_min) continue;

        const auto already = m_fired.find(event.id);
        if (already != m_fired.end() && already->second == *at) continue;

        m_fired[event.id] = *at;
        on_alarm(event, static_cast<int>(minutes));
    }
}
