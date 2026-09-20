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
    std::string short_name; // optional, for compact display in the form
    int max_runs = 0; // 0 = no daily limit (plain checkbox task)
};

// Throws a std::exception subclass (nlohmann::json::parse_error or
// std::runtime_error) on unreadable or malformed JSON. Callers that
// treat the catalog as optional (we do) catch std::exception and
// continue with an empty list.
std::vector<Dungeon> load_dungeons(const std::string& path);

// ── Catalog queries used by the panel and the add-task form ──
// Kept here (not in the UI layer) so the JSON schema stays the only
// source of truth about what a dungeon is.

// Display label: "AC — Abandoned City" — short code first when the
// catalog has one, full name for context.
std::string dungeon_label(const Dungeon& dungeon);

// First dungeon with exactly this label, or nullptr. Labels are
// unique because names are unique.
const Dungeon* find_dungeon_by_label(const std::vector<Dungeon>& dungeons,
                                     const std::string& label);

// The short code for a catalog dungeon name; "" for custom-typed
// tasks or catalog entries without a code.
std::string short_code_for(const std::vector<Dungeon>& dungeons,
                           const std::string& dungeon_name);

// Case-insensitive substring match on the name OR the short code.
bool dungeon_matches(const Dungeon& dungeon, const std::string& query);

// Resolves a possibly-misspelled name against the catalog, strictest
// first: exact, case-insensitive, short code, UNIQUE substring, then
// UNIQUE edit distance <= 3. nullptr when nothing matches uniquely —
// a wrong guess is worse than no guess (the caller keeps the literal
// name as a plain task and warns).
const Dungeon* find_dungeon_smart(const std::vector<Dungeon>& dungeons,
                                  const std::string& name);
