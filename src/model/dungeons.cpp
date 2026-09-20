// ─────────────────────────────────────────────────────────────
// dungeons.cpp — JSON catalog loading. The only file besides
// state.cpp that knows the dungeons.json schema.
// ─────────────────────────────────────────────────────────────

#include "dungeons.h"

#include <algorithm> // std::min with initializer list (edit_distance)
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

namespace {

// Classic Levenshtein edit distance, lowercased both sides. Small
// strings only (dungeon names), so the full DP table is fine.
int edit_distance(std::string a, std::string b) {
    a = to_lower(std::move(a));
    b = to_lower(std::move(b));
    std::vector<int> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j)
        row[j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        int diagonal = row[0];
        row[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const int above = row[j];
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + cost});
            diagonal = above;
        }
    }
    return row[b.size()];
}

} // anonymous namespace

const Dungeon* find_dungeon_smart(const std::vector<Dungeon>& dungeons,
                                  const std::string& name) {
    // 1-2. Exact, then case-insensitive exact.
    for (const Dungeon& dungeon : dungeons)
        if (dungeon.name == name) return &dungeon;
    const std::string lowered = to_lower(name);
    for (const Dungeon& dungeon : dungeons)
        if (to_lower(dungeon.name) == lowered) return &dungeon;

    // 3. Short code, case-insensitive.
    for (const Dungeon& dungeon : dungeons)
        if (!dungeon.short_name.empty() &&
            to_lower(dungeon.short_name) == lowered)
            return &dungeon;

    // 4. Substring — only when exactly ONE dungeon contains it.
    const Dungeon* substring_hit = nullptr;
    int substring_hits = 0;
    for (const Dungeon& dungeon : dungeons) {
        if (to_lower(dungeon.name).find(lowered) != std::string::npos) {
            substring_hit = &dungeon;
            if (++substring_hits > 1) break;
        }
    }
    if (substring_hits == 1) return substring_hit;

    // 5. Typo tolerance: unique closest name within edit distance 3.
    const Dungeon* closest = nullptr;
    int best = 4; // strictly better than the threshold
    int ties = 0;
    for (const Dungeon& dungeon : dungeons) {
        const int distance = edit_distance(dungeon.name, name);
        if (distance < best) {
            best = distance;
            closest = &dungeon;
            ties = 1;
        } else if (distance == best) {
            ++ties;
        }
    }
    if (best <= 3 && ties == 1) return closest;
    return nullptr;
}
