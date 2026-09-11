// ─────────────────────────────────────────────────────────────
// state.h — persistence in the tracker-compatible schema.
//
// The on-disk shape mirrors the clan tracker's `cabalTrackerData`
// exactly, so a state.json exported here imports cleanly into the
// web app and vice versa. Fields the tracker has and we do not
// care about (web settings, tour flags, alarm cache) are simply
// absent from our files — the tracker's import fills defaults.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <chrono>
#include <filesystem>

#include "tasks.h"

struct AppState {
    TaskList tasks;
    std::string last_daily_reset;   // Berlin date strings
    std::string last_weekly_reset;  // (see server_time.h)
};

// ~/.local/share/cabal-overlay/state.json (respects XDG_DATA_HOME).
std::filesystem::path default_state_path();

// Missing file → fresh, empty state (first run is not an error).
// Malformed JSON → throws std::runtime_error; the caller decides
// what to do (we refuse to save over a file we could not read).
AppState load_state(const std::filesystem::path& path);

void save_state(const std::filesystem::path& path, const AppState& state);

// Detects daily/weekly resets that happened since `last_*_reset`
// were recorded (including while the app was closed) and clears
// the matching tasks. Returns true when anything was reset.
bool apply_resets(AppState& state, std::chrono::system_clock::time_point now);
