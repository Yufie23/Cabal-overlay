// ─────────────────────────────────────────────────────────────
// clock.cpp — time logic. 100% portable: no GTK, no Wayland,
// nothing platform-specific. This file would compile unchanged
// on Windows, macOS or a fridge running Linux.
// ─────────────────────────────────────────────────────────────

#include "clock.h"

#include <chrono>
#include <format>

namespace {

// The game server runs on Europe/Berlin time (CET/CEST). The IANA
// timezone database handles daylight-saving switches for us.
constexpr char kServerTimezone[] = "Europe/Berlin";

} // namespace

std::string clock_text() {
    const auto now = std::chrono::system_clock::now();

    const auto* server_zone = std::chrono::locate_zone(kServerTimezone);
    const std::chrono::zoned_time server_now{server_zone, now};

    // current_zone() reads the timezone configured in the OS itself.
    const std::chrono::zoned_time local_now{std::chrono::current_zone(), now};

    return std::format("SRV {:%H:%M} | LOC {:%H:%M}", server_now, local_now);
}
