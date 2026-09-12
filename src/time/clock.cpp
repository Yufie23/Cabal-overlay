// ─────────────────────────────────────────────────────────────
// clock.cpp — local-time text for the bar. The game itself shows
// server time natively, so the overlay only tracks local time.
// 100% portable: no GTK, no Wayland.
// ─────────────────────────────────────────────────────────────

#include "clock.h"

#include <format>

std::string local_clock_text(std::chrono::system_clock::time_point now) {
    // current_zone() reads the timezone configured in the OS itself.
    const std::chrono::zoned_time local_now{std::chrono::current_zone(), now};
    return std::format("LOC {:%H:%M}", local_now);
}
