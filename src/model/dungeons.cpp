// ─────────────────────────────────────────────────────────────
// dungeons.cpp — JSON catalog loading. The only file besides
// state.cpp that knows the dungeons.json schema.
// ─────────────────────────────────────────────────────────────

#include "dungeons.h"

#include <cctype>   // std::tolower
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace {

std::string to_lower(std::string text) {
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

} // anonymous namespace

std::vector<Dungeon> load_dungeons(const std::string& path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("cannot open dungeon catalog: " + path);

    const nlohmann::json root = nlohmann::json::parse(input);

    std::vector<Dungeon> dungeons;
    for (const auto& node : root.at("dungeons")) {
        Dungeon dungeon;
        node.at("name").get_to(dungeon.name);
        if (node.contains("shortName"))
            node.at("shortName").get_to(dungeon.short_name);
        node.at("maxRuns").get_to(dungeon.max_runs);
        dungeons.push_back(std::move(dungeon));
    }
    return dungeons;
}

std::string dungeon_label(const Dungeon& dungeon) {
    // "AC — Abandoned City": the clan's short code up front, full name
    // for context. The em dash separator never appears in names.
    if (dungeon.short_name.empty()) return dungeon.name;
    return dungeon.short_name + " — " + dungeon.name;
}

const Dungeon* find_dungeon_by_label(const std::vector<Dungeon>& dungeons,
                                     const std::string& label) {
    // Labels are unique because dungeon names are unique, so the first
    // hit is the only hit.
    for (const Dungeon& dungeon : dungeons)
        if (dungeon_label(dungeon) == label) return &dungeon;
    return nullptr;
}

std::string short_code_for(const std::vector<Dungeon>& dungeons,
                           const std::string& dungeon_name) {
    for (const Dungeon& dungeon : dungeons)
        if (dungeon.name == dungeon_name) return dungeon.short_name;
    return ""; // custom-typed task, or the catalog entry has no code
}

bool dungeon_matches(const Dungeon& dungeon, const std::string& query) {
    // Case-insensitive substring match on the name OR the short code.
    // Everything is lowered per comparison — 86 dungeons per keystroke
    // is not worth caching anything.
    const std::string needle = to_lower(query);
    if (to_lower(dungeon.name).find(needle) != std::string::npos)
        return true;
    return !dungeon.short_name.empty() &&
           to_lower(dungeon.short_name).find(needle) != std::string::npos;
}
