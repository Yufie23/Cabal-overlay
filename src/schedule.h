// ─────────────────────────────────────────────────────────────
// schedule.h — "when is the next X?" math over the config data.
// Portable: no GTK anywhere in sight.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "config.h"

// An event together with the absolute moment it will next fire.
struct UpcomingEvent {
    std::string name;
    std::chrono::system_clock::time_point at;
};

// The next absolute instant at which `event` fires after `now`,
// or std::nullopt if the event can never fire. Exposed for the
// alarms module; everything else should use upcoming_events().
std::optional<std::chrono::system_clock::time_point>
next_occurrence(const ScheduleEvent& event,
                std::chrono::system_clock::time_point now);

// The next `limit` events after `now`, sorted soonest-first.
// Events that never fire (misconfigured) are silently skipped —
// they were already rejected at config load, so this is paranoia.
std::vector<UpcomingEvent> upcoming_events(
    const std::vector<ScheduleEvent>& events,
    std::chrono::system_clock::time_point now,
    std::size_t limit);

// Renders a duration as "HH:MM:SS", or "Nd HH:MM:SS" past 24h.
std::string format_countdown(std::chrono::system_clock::duration remaining);

// Bar-ready text: "Nation War 00:44:12 · Daily Reset 03:44:12".
std::string events_text(
    const std::vector<ScheduleEvent>& events,
    std::chrono::system_clock::time_point now,
    std::size_t limit);
