// ─────────────────────────────────────────────────────────────
// dungeons.cpp — JSON catalog loading. The only file besides
// state.cpp that knows the dungeons.json schema.
// ─────────────────────────────────────────────────────────────

#include "dungeons.h"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

std::vector<Dungeon> load_dungeons(const std::string& path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("cannot open dungeon catalog: " + path);

    const nlohmann::json root = nlohmann::json::parse(input);

    std::vector<Dungeon> dungeons;
    for (const auto& node : root.at("dungeons")) {
        Dungeon dungeon;
        node.at("name").get_to(dungeon.name);
        node.at("maxRuns").get_to(dungeon.max_runs);
        dungeons.push_back(std::move(dungeon));
    }
    return dungeons;
}
