// ─────────────────────────────────────────────────────────────
// config.cpp — TOML → structs. The only file that includes toml++.
// ─────────────────────────────────────────────────────────────

#include "config.h"

#include <stdexcept>

#include <toml++/toml.hpp>

namespace {

// "19:55" → 19h 55m as minutes-after-midnight. Rejects anything
// that is not exactly HH:MM in range — config typos must fail
// loudly at startup, not silently skip an event.
std::chrono::minutes parse_hhmm(const std::string& text) {
    if (text.size() != 5 || text[2] != ':')
        throw std::runtime_error("bad time format (expected HH:MM): '" + text + "'");

    const int hh = std::stoi(text.substr(0, 2)); // may throw std::invalid_argument
    const int mm = std::stoi(text.substr(3, 2));
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59)
        throw std::runtime_error("time out of range (00:00-23:59): '" + text + "'");

    return std::chrono::hours{hh} + std::chrono::minutes{mm};
}

std::chrono::weekday parse_weekday(const std::string& name) {
    // Structured bindings (`auto& [text, day]`) unpack each pair
    // from the array, like destructuring in JS.
    static const std::pair<const char*, std::chrono::weekday> kNames[] = {
        {"monday",    std::chrono::Monday},
        {"tuesday",   std::chrono::Tuesday},
        {"wednesday", std::chrono::Wednesday},
        {"thursday",  std::chrono::Thursday},
        {"friday",    std::chrono::Friday},
        {"saturday",  std::chrono::Saturday},
        {"sunday",    std::chrono::Sunday},
    };
    for (const auto& [text, day] : kNames)
        if (name == text) return day;
    throw std::runtime_error("unknown weekday: '" + name + "'");
}

} // namespace

AppConfig load_config(const std::string& path) {
    // toml++ throws toml::parse_error (a std::runtime_error) on
    // unreadable or invalid files, so this one call covers both
    // "file not found" and "TOML syntax error".
    const toml::table root = toml::parse_file(path);

    const auto* schedule_nodes = root["schedule"].as_array();
    const auto* overlay_nodes = root["overlay"].as_table();
    if (schedule_nodes == nullptr)
        throw std::runtime_error("config has no [[schedule]] entries: " + path);

    AppConfig config;

    // Parse the whole [overlay] section so the struct always reflects
    // the file; value_or keeps the in-class default when a key is
    // absent.
    if (overlay_nodes) {
        const auto& overlay = *overlay_nodes;
        config.overlay.anchor         = overlay["anchor"].value_or(config.overlay.anchor);
        config.overlay.margin_x       = overlay["margin_x"].value_or(config.overlay.margin_x);
        config.overlay.margin_y       = overlay["margin_y"].value_or(config.overlay.margin_y);
        config.overlay.opacity        = overlay["opacity"].value_or(config.overlay.opacity);
        config.overlay.theme          = overlay["theme"].value_or(config.overlay.theme);
        config.overlay.max_countdowns = overlay["max_countdowns"].value_or(config.overlay.max_countdowns);
    }

    if (const auto* alarms = root["alarms"].as_table()) {
        config.alarms.enabled         = (*alarms)["enabled"].value_or(config.alarms.enabled);
        config.alarms.volume          = (*alarms)["volume"].value_or(config.alarms.volume);
        config.alarms.warn_before_min = (*alarms)["warn_before_min"].value_or(config.alarms.warn_before_min);
        config.alarms.daily           = (*alarms)["daily"].value_or(config.alarms.daily);
        config.alarms.weekly          = (*alarms)["weekly"].value_or(config.alarms.weekly);
        config.alarms.gdg             = (*alarms)["gdg"].value_or(config.alarms.gdg);
        config.alarms.world_boss      = (*alarms)["world_boss"].value_or(config.alarms.world_boss);
        config.alarms.nation_war      = (*alarms)["nation_war"].value_or(config.alarms.nation_war);
    }
    for (const auto& node : *schedule_nodes) {
        const auto* table = node.as_table();
        if (table == nullptr)
            throw std::runtime_error("a [[schedule]] entry is not a table");

        ScheduleEvent event;
        event.id = (*table)["id"].value_or("unnamed");
        // name defaults to id when absent.
        if (const auto name = (*table)["name"].value<std::string>())
            event.name = *name;
        else
            event.name = event.id;

        // "times" is an array for daily events; weekly events use the
        // singular "time". We normalize both into the same vector so
        // the rest of the app never cares which form the TOML used.
        if (const auto* times = (*table)["times"].as_array()) {
            for (const auto& value : *times)
                event.times.push_back(parse_hhmm(value.value_or("")));
        }
        if (const auto single = (*table)["time"].value<std::string>())
            event.times.push_back(parse_hhmm(*single));

        if (event.times.empty())
            throw std::runtime_error("schedule '" + event.id + "' has no times");

        if (const auto day = (*table)["weekday"].value<std::string>())
            event.weekday = parse_weekday(*day);

        config.schedules.push_back(std::move(event));
    }
    return config;
}
