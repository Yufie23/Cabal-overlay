// ─────────────────────────────────────────────────────────────
// tutorial.h — first-run tutorial wizard
//
// A small paged window (Next / Back / Done) shown once on the first
// run and reopenable anytime from the "?" button in the panel
// footer. Modeled after the clan tracker's first-run tour: every
// feature in one screen of plain text each, dummy-proof.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <functional>

#include <gtk/gtk.h>

namespace tutorial {

// Shows the wizard (singleton: a second call just presents the
// existing window). `on_done` fires exactly once when the user
// finishes or dismisses it — the caller persists the "seen" flag.
void present(GtkApplication* app, std::function<void()> on_done);

} // namespace tutorial
