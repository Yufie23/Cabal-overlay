// ─────────────────────────────────────────────────────────────
// config.h — typed representation of config/overlay.toml
//
// The TOML file is text for humans; this module turns it into
// structs the rest of the app can work with. All parsing problems
// (missing keys, bad times, unknown weekdays) are reported here,
// once, at startup — the rest of the app sees only valid data.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

// One scheduled game event (Nation War, Daily Reset, ...).
struct ScheduleEvent {
    std::string id;
    std::string name;

    // Times of day when the event fires, as minutes after midnight
    // in SERVER time. A "daily" event simply has several entries.
    std::vector<std::chrono::minutes> times;

    // Set only for weekly events (e.g. Wednesday for weekly reset).
    // std::optional = "this may hold no value", said in the type
    // system instead of with a magic null. Forced by our own rules:
    // nobody can read the weekday without checking it exists.
    std::optional<std::chrono::weekday> weekday;
};

// [overlay] section. Every field has an in-class initializer: no
// code path can ever leave garbage in these members (an
// uninitialized int in C++ holds whatever was in that RAM before —
// there is no "undefined" here).
//
// Only max_countdowns is wired into the app today; anchor, margins,
// opacity and theme are parsed for the upcoming settings/positioning
// work, so the TOML and this struct stay in sync.
struct OverlayConfig {
    std::string anchor = "top-right";
    int margin_x = 20;
    int margin_y = 60;
    double opacity = 0.85;
    std::string theme = "prosperity";
    int max_countdowns = 3;
};

// [alarms] section. Parsed so the struct always reflects the file;
// the alarms module (sound + notification, roadmap phase 1.5) reads
// these fields when it lands.
struct AlarmsConfig {
    bool enabled = false;
    int volume = 50;
    int warn_before_min = 5;
    bool daily = true;
    bool weekly = true;
    bool gdg = true;
    bool world_boss = true;
    bool nation_war = true;
};

struct AppConfig {
    std::vector<ScheduleEvent> schedules;
    OverlayConfig overlay;
    AlarmsConfig alarms;
};

// Parses the TOML file into an AppConfig.
// Throws std::runtime_error with a human-readable message if the
// file is missing or malformed. Config errors are fatal: starting
// with wrong schedules is worse than not starting at all.
AppConfig load_config(const std::string& path);
