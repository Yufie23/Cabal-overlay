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
#include <variant>
#include <vector>

// One scheduled game event (Nation War, Daily Reset, ...).
struct ScheduleEvent {
    std::string id;
    std::string name;

    // Alarm kind for the alarms module — matches a flag in
    // AlarmsConfig (daily, weekly, gdg, world_boss, nation_war).
    // Defaults to the event id when the TOML omits it.
    std::string alarm;

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
// anchor/margins/opacity drive the bar's placement (platform::
// Placement), max_countdowns the bar text, and
// show_only_when_game_focused the visibility watch in main.cpp.
struct OverlayConfig {
    std::string anchor = "top-right";
    int margin_x = 20;
    int margin_y = 60;
    double opacity = 0.85;
    std::string theme = "prosperity";
    int max_countdowns = 3;
    // True → the overlay hides itself whenever the game window does
    // not hold the input focus (alt-tab, desktop, game closed). Needs
    // the game under XWayland; falls back to always-visible if the
    // X11 display cannot be opened.
    bool show_only_when_game_focused = true;
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

// [panel] section — the goals panel (dungeon counters with progress
// bars). Same placement fields as OverlayConfig; visible=false removes
// the panel entirely while keeping the bar.
struct PanelConfig {
    bool visible = true;
    std::string anchor = "right"; // "right" alone = vertically centered
    int margin_x = 10;
    int margin_y = 0;
    double opacity = 0.9;
    // Display toggles (header buttons in the panel; persisted here):
    // short_names → rows show the catalog short code, else full names.
    bool short_names = true;
    // collapsed → only the first few pending tasks plus a "… N more"
    // hint; a display-only compression, logic sees the full list.
    bool collapsed = false;
};

// How the global "toggle interactive" combo reaches the app:
//   External → a desktop/compositor shortcut calls the D-Bus action
//              toggle-interactive. Zero extra permissions; the
//              recommended default. On KDE: System Settings →
//              Shortcuts → Custom Shortcuts (see docs/04).
//   Evdev    → the app reads /dev/input itself (platform/hotkey.h).
//              No desktop setup and works while the game holds focus,
//              BUT requires "input" group membership — and from then
//              on ANY process running as your user can silently read
//              every keystroke. A real, permanent risk: opt in
//              knowingly. (The future Windows backend would use
//              RegisterHotKey instead, with no such trade-off.)
//   Disabled → no global combo at all; the D-Bus action still works
//              when called manually.
enum class HotkeyMode {
    External,
    Evdev,
    Disabled,
};

// [hotkey] section.
struct HotkeyConfig {
    HotkeyMode mode = HotkeyMode::External;
    std::string combo = "Shift+Space"; // only used in Evdev mode
};

// [dgcheck] section — screen-zone click counter. When enabled, a
// primary click landing inside the configured rectangle counts as
// one dungeon clear and bumps the first tracked task. Coordinates are
// absolute screen pixels in the X11 root coordinate space — the game
// runs under XWayland, so the dungeon-end dialog lives there.
struct DgcheckConfig {
    bool enabled = false;
    int x = 0;       // zone top-left corner, screen pixels
    int y = 0;
    int width = 240; // zone size
    int height = 120;
    bool require_ctrl = true; // only count clicks while CTRL is held
};

struct AppConfig {
    std::vector<ScheduleEvent> schedules;
    OverlayConfig overlay;
    AlarmsConfig alarms;
    PanelConfig panel;
    HotkeyConfig hotkey;
    DgcheckConfig dgcheck;
};

// Parses the TOML file into an AppConfig.
// Throws std::runtime_error with a human-readable message if the
// file is missing or malformed. Config errors are fatal: starting
// with wrong schedules is worse than not starting at all.
AppConfig load_config(const std::string& path);

// A writable config value. std::variant is a type-safe union: it
// holds exactly one of these types and visiting the wrong type is a
// compile error, not a runtime surprise — no "null means anything".
using ConfigValue = std::variant<bool, int, double, std::string>;

// Writes one config value back to the TOML file, addressed by dotted
// key ("overlay.opacity", "alarms.enabled", ...).
//
// The write is surgical on purpose: toml++ cannot preserve comments,
// and this TOML's comments are its documentation — so instead of
// re-serializing the whole file, exactly one `key = value` line
// inside its `[section]` is replaced and the rest stays
// byte-identical. String values must not contain '#' (inline-comment
// marker) or '"' — true for every key writable through the settings
// UI today; anything fancier belongs in a hand-edited file.
//
// Writes to a temporary file and atomically renames it into place,
// so the config monitor either sees the old file or the new one,
// never a half-written one. Throws on unreadable file, unknown key
// or type mismatch; the settings UI surfaces that as a warning.
void set_config_value(const std::string& path, const std::string& dotted_key,
                      const ConfigValue& value);
