// ─────────────────────────────────────────────────────────────
// update_check.h — "is there a newer release on GitHub?"
//
// Notify-only updater: one async curl call against the GitHub
// releases API at startup. When a newer tag exists the caller shows
// a notification with a Download button; downloading and installing
// stays manual (an unsigned auto-installer would re-trigger
// SmartScreen anyway, for zero UX gain). No auto-download, no second
// process, nothing running in the background.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <functional>
#include <string>

namespace updates {

// Fires `on_newer(tag)` on the GLib main thread only when the latest
// GitHub release tag is strictly newer than `current_version`.
// Every failure (offline, curl missing, API error, bad JSON) is
// logged and forgotten — the app never notices.
void check_latest(const std::string& current_version,
                  std::function<void(const std::string& tag)> on_newer);

} // namespace updates
