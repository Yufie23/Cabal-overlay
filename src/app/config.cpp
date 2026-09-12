// ─────────────────────────────────────────────────────────────
// config.cpp — TOML → structs, plus the surgical writer below.
// The only file that includes toml++.
// ─────────────────────────────────────────────────────────────

#include "config.h"

#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
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

    if (const auto* panel = root["panel"].as_table()) {
        config.panel.visible    = (*panel)["visible"].value_or(config.panel.visible);
        config.panel.anchor     = (*panel)["anchor"].value_or(config.panel.anchor);
        config.panel.margin_x   = (*panel)["margin_x"].value_or(config.panel.margin_x);
        config.panel.margin_y   = (*panel)["margin_y"].value_or(config.panel.margin_y);
        config.panel.opacity    = (*panel)["opacity"].value_or(config.panel.opacity);
    }

    if (const auto* hotkey = root["hotkey"].as_table()) {
        // Enum modes parse explicitly: an unknown mode string is a
        // config typo and must fail loudly, not silently disable the
        // user's only way to click the overlay.
        if (const auto mode = (*hotkey)["mode"].value<std::string>()) {
            if (*mode == "external")      config.hotkey.mode = HotkeyMode::External;
            else if (*mode == "evdev")    config.hotkey.mode = HotkeyMode::Evdev;
            else if (*mode == "disabled") config.hotkey.mode = HotkeyMode::Disabled;
            else
                throw std::runtime_error("unknown hotkey mode: '" + *mode +
                                         "' (expected external|evdev|disabled)");
        }
        config.hotkey.combo = (*hotkey)["combo"].value_or(config.hotkey.combo);
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
        // alarm kind defaults to the id (ids already match the
        // AlarmsConfig flag names in our TOML).
        if (const auto alarm = (*table)["alarm"].value<std::string>())
            event.alarm = *alarm;
        else
            event.alarm = event.id;

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

namespace {

// Renders a ConfigValue as TOML source text.
std::string render_toml_value(const ConfigValue& value) {
    return std::visit(
        [](const auto& item) -> std::string {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, bool>)
                return item ? "true" : "false";
            else if constexpr (std::is_same_v<T, int>)
                return std::to_string(item);
            else if constexpr (std::is_same_v<T, double>) {
                // Doubles must keep a decimal point: TOML "1.0" and
                // "1" are different node types, and the loader reads
                // this key back as floating point.
                std::string text = std::format("{}", item);
                if (text.find('.') == std::string::npos)
                    text += ".0";
                return text;
            } else
                return std::format("\"{}\"", item);
        },
        value);
}

std::size_t skip_spaces(const std::string& line, std::size_t pos) {
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
        ++pos;
    return pos;
}

// True when `line` opens the TOML section named `section`, i.e. it
// is "[section]" or "[[section]]" (schedules are never written
// through here, but both forms are recognized).
bool opens_section(const std::string& line, const std::string& section) {
    const std::size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos || line[start] != '[')
        return false;
    const bool array_table = line.compare(start, 2, "[[") == 0;
    const std::size_t name_start = start + (array_table ? 2 : 1);
    const std::size_t close =
        line.find(array_table ? "]]" : "]", name_start);
    if (close == std::string::npos)
        return false;
    return line.substr(name_start, close - name_start) == section;
}

} // anonymous namespace

void set_config_value(const std::string& path, const std::string& dotted_key,
                      const ConfigValue& value) {
    const auto dot = dotted_key.find('.');
    if (dot == std::string::npos)
        throw std::runtime_error("config key must be section-scoped: '" +
                                 dotted_key + "'");
    const std::string section = dotted_key.substr(0, dot);
    const std::string key = dotted_key.substr(dot + 1);

    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("cannot open config for writing: " + path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(input, line);)
        lines.push_back(std::move(line));

    bool in_section = false;
    bool written = false;
    for (std::string& line : lines) {
        if (opens_section(line, section)) {
            in_section = true;
            continue;
        }
        if (const std::size_t start = line.find_first_not_of(" \t");
            start != std::string::npos && line[start] == '[')
            in_section = false; // some other section begins
        if (!in_section)
            continue;

        // Match `key =` with arbitrary whitespace around both parts.
        std::size_t pos = skip_spaces(line, 0);
        const std::size_t key_start = pos;
        while (pos < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[pos])) ||
                line[pos] == '_'))
            ++pos;
        if (line.substr(key_start, pos - key_start) != key)
            continue;
        pos = skip_spaces(line, pos);
        if (pos >= line.size() || line[pos] != '=')
            continue;

        // Keep a trailing inline comment ("= 0.85   # fade"): our
        // writable string values never contain '#' (contract in the
        // header), so the first one reliably marks the comment.
        const std::size_t hash = line.find('#', pos + 1);
        const std::string comment =
            hash == std::string::npos ? "" : line.substr(hash);

        line = line.substr(0, pos + 1) + " " + render_toml_value(value);
        if (!comment.empty())
            line += "  " + comment;
        written = true;
    }

    if (!written)
        throw std::runtime_error("key not found in " + path + ": " +
                                 dotted_key);

    // Atomic publish: the config monitor sees either the whole old
    // file or the whole new one, never a torn write.
    const std::string tmp_path = path + ".tmp";
    {
        std::ofstream output(tmp_path, std::ios::trunc);
        if (!output)
            throw std::runtime_error("cannot write " + tmp_path);
        for (const std::string& line : lines)
            output << line << '\n';
        output.flush();
        if (!output)
            throw std::runtime_error("write failed: " + tmp_path);
    }
    std::error_code error;
    std::filesystem::rename(tmp_path, path, error);
    if (error) {
        std::filesystem::remove(tmp_path);
        throw std::runtime_error("atomic rename failed: " + error.message());
    }
}
