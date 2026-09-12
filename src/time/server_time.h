// ─────────────────────────────────────────────────────────────
// server_time.h — the one place that knows the server's timezone
//
// `inline` on a non-member function means: "this may be defined in
// several translation units; linker, they are all the same one".
// Combined with a function-local `static`, every caller shares a
// single cached timezone lookup.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <chrono>
#include <format>
#include <string>

// The Cabal server runs on Europe/Berlin (CET/CEST). The IANA
// timezone database handles daylight-saving switches for us.
// locate_zone never fails for a valid name; a bad name throws.
inline const std::chrono::time_zone* server_zone() {
    static const auto* zone = std::chrono::locate_zone("Europe/Berlin");
    return zone;
}

// "2026-09-11" in server time. The tracker compares these strings
// to detect that a daily reset happened while the app was closed.
inline std::string berlin_date_string(std::chrono::system_clock::time_point now) {
    const std::chrono::zoned_time server_now{server_zone(), now};
    return std::format("{:%Y-%m-%d}", server_now);
}

// Date string of the most recent Wednesday in server time — the
// tracker's "weekly reset" anchor. weekday subtraction is circular
// (always 0-6 days back), which is exactly the "last Wednesday"
// semantics we need, including for Sundays and Mondays.
inline std::string berlin_weekly_reset_string(std::chrono::system_clock::time_point now) {
    const std::chrono::zoned_time server_now{server_zone(), now};
    const auto day = std::chrono::floor<std::chrono::days>(server_now.get_local_time());
    const auto since_wednesday = std::chrono::weekday{day} - std::chrono::Wednesday;
    return std::format("{:%Y-%m-%d}", day - since_wednesday);
}
