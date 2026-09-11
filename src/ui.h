// ─────────────────────────────────────────────────────────────
// ui.h — the goals panel widget tree
//
// Pure view layer: given the app state, it knows how to (re)build
// the GTK widget tree of the goals panel (dungeon counters with
// progress bars). It owns no state of its own; main.cpp decides
// WHEN to rebuild (via goals_signature) and WHETHER the panel
// window is visible at all.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <string>

#include <gtk/gtk.h>

#include "state.h"

// Fingerprint of everything the panel renders: ids, names, types,
// counters, goals, flags. Cheap to compute, cheap to compare; when
// it differs from the last render, the panel must be rebuilt. This
// is the "key" pattern from React: a coarse diff upfront, then a
// full rebuild instead of fine-grained mutation.
std::string goals_signature(const AppState& state);

// Rebuilds the panel contents from scratch: drops every child and
// re-creates the DAILY/WEEKLY section headers and task rows (name +
// counter + progress bar). Only called when goals_signature changed.
void goals_panel_refresh(GtkWidget* panel, const AppState& state);
