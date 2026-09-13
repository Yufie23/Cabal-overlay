// ─────────────────────────────────────────────────────────────
// dgcheck.h — pure logic for the screen-zone click counter
//
// Deliberately platform-free: this module knows nothing about X11,
// GTK or file formats. Given "a click happened at (x, y) with this
// CTRL state" plus the configured zone, it answers two questions:
// does it count, and which task does it bump. Keeping the rules in
// one testable place means the platform backend (pointer_x11.cpp)
// stays a dumb sensor.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <string>

#include "app/config.h"
#include "model/tasks.h"

namespace dgcheck {

// True when a primary click at (x, y) should count as a dungeon
// clear. The CTRL requirement is part of the zone, not the caller:
// a click outside the rectangle, or without CTRL when CTRL is
// required, is just a normal game click and must be ignored.
bool counts_click(const DgcheckConfig& zone, int x, int y, bool ctrl);

// The task a counted click bumps: the first daily task that is not
// done yet, or the first weekly one when no dailies remain — done
// tasks stay in the list but are skipped, so the counter moves on to
// the next pending one. Empty string (id == "") means "no task to
// bump"; callers must check instead of indexing blindly.
std::string target_task_id(const TaskList& tasks);

} // namespace dgcheck
