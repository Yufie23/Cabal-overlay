// ─────────────────────────────────────────────────────────────
// clock.cpp — time logic. 100% portable: no GTK, no Wayland,
// nothing platform-specific. This file would compile unchanged
// on Windows, macOS or a fridge running Linux.
// ─────────────────────────────────────────────────────────────

#include "clock.h"

#include <format>

#include "server_time.h"

std::string clock_text(std::chrono::system_clock::time_point now) {
    const std::chrono::zoned_time server_now{server_zone(), now};

    // current_zone() reads the timezone configured in the OS itself.
    const std::chrono::zoned_time local_now{std::chrono::current_zone(), now};

    return std::format("SRV {:%H:%M} | LOC {:%H:%M}", server_now, local_now);
}
