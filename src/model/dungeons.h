// ─────────────────────────────────────────────────────────────
// dungeons.h — the dungeon catalog extracted from the clan tracker
//
// Static data source for the add-task form: dungeon names plus the
// tracker's maxRuns per day. Loaded once at startup from
// data/dungeons.json. A missing or broken catalog is NOT fatal —
// the form simply offers no suggestions.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <string>
#include <vector>

struct Dungeon {
    std::string name;
    int max_runs = 0; // 0 = no daily limit (plain checkbox task)
};

// Throws a std::exception subclass (nlohmann::json::parse_error or
// std::runtime_error) on unreadable or malformed JSON. Callers that
// treat the catalog as optional (we do) catch std::exception and
// continue with an empty list.
std::vector<Dungeon> load_dungeons(const std::string& path);
