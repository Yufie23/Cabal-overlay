// ─────────────────────────────────────────────────────────────
// schedule.cpp — next-occurrence math.
//
// The core idea: convert "now" into server-local time, walk day by
// day generating candidate instants, and keep the earliest one that
// is still in the future. Day-walking (instead of clever modular
// arithmetic) makes the weekly case and the "already passed today"
// edge case fall out of the same loop.
// ─────────────────────────────────────────────────────────────

#include "schedule.h"

#include <algorithm>
#include <format>

#include "server_time.h"

namespace {

// The next absolute instant at which `event` fires after `now`,
// or std::nullopt if the event can never fire.
std::optional<std::chrono::system_clock::time_point>
next_occurrence(const ScheduleEvent& event,
                std::chrono::system_clock::time_point now) {
    const std::chrono::zoned_time server_now{server_zone(), now};
    const auto today = std::chrono::floor<std::chrono::days>(server_now.get_local_time());

    std::optional<std::chrono::system_clock::time_point> best;

    // Daily events only need today + tomorrow. Weekly events need up
    // to 7 days ahead (today is Wednesday but 00:00 already passed →
    // the answer is next Wednesday). One loop covers both.
    const int horizon = event.weekday.has_value() ? 7 : 1;

    for (int offset = 0; offset <= horizon; ++offset) {
        const auto day = today + std::chrono::days{offset};

        if (event.weekday.has_value() &&
            std::chrono::weekday{day} != *event.weekday)
            continue; // not the right weekday; try the next day

        for (const auto time : event.times) {
            // All our configured times avoid the 02:00-03:00 DST gap,
            // so converting local→sys is always unambiguous here.
            const std::chrono::zoned_time candidate{server_zone(), day + time};
            const auto at = candidate.get_sys_time();

            if (at > now && (!best.has_value() || at < *best))
                best = at;
        }
    }
    return best;
}

} // namespace

std::vector<UpcomingEvent> upcoming_events(
    const std::vector<ScheduleEvent>& events,
    std::chrono::system_clock::time_point now,
    std::size_t limit) {

    std::vector<UpcomingEvent> upcoming;
    for (const auto& event : events) {
        if (const auto at = next_occurrence(event, now))
            upcoming.push_back({event.name, *at});
    }

    // Keep only the soonest `limit`, chronologically sorted.
    const auto by_soonest = [](const UpcomingEvent& a, const UpcomingEvent& b) {
        return a.at < b.at;
    };
    if (upcoming.size() > limit) {
        // partial_sort: only the first `limit` elements end up sorted;
        // sorting 5 events fully would cost the same, but this habit
        // scales when the list grows.
        std::partial_sort(upcoming.begin(), upcoming.begin() + limit,
                          upcoming.end(), by_soonest);
        upcoming.resize(limit);
    } else {
        std::sort(upcoming.begin(), upcoming.end(), by_soonest);
    }
    return upcoming;
}

std::string format_countdown(std::chrono::system_clock::duration remaining) {
    auto total = std::chrono::duration_cast<std::chrono::seconds>(remaining).count();
    if (total < 0) total = 0;

    const long days    = total / 86400;
    const long hours   = (total % 86400) / 3600;
    const long minutes = (total % 3600) / 60;
    const long seconds = total % 60;

    if (days > 0)
        return std::format("{}d {:02}:{:02}:{:02}", days, hours, minutes, seconds);
    return std::format("{:02}:{:02}:{:02}", hours, minutes, seconds);
}

std::string events_text(
    const std::vector<ScheduleEvent>& events,
    std::chrono::system_clock::time_point now,
    std::size_t limit) {

    const auto upcoming = upcoming_events(events, now, limit);

    std::string text;
    for (std::size_t i = 0; i < upcoming.size(); ++i) {
        if (i > 0) text += "  ·  ";
        text += std::format("{} {}",
                            upcoming[i].name,
                            format_countdown(upcoming[i].at - now));
    }
    return text;
}
