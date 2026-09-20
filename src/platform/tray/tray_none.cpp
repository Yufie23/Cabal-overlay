// ─────────────────────────────────────────────────────────────
// tray_none.cpp — no-op backend for platforms without a tray story
//
// Wayland layer-shell apps have no tray equivalent without a
// StatusNotifierItem D-Bus bridge, and the app already exposes
// settings and quit through its D-Bus actions on Linux, so there is
// nothing to do here — the contract's "false = unsupported" is the
// honest answer.
// ─────────────────────────────────────────────────────────────

#include "platform/tray/tray.h"

namespace platform {

// NOLINTBEGIN(performance-unnecessary-value-param) — the contract
// passes by value; this backend simply never uses the argument.
bool tray_start(TrayActions /*actions*/) {
    return false; // no tray on this platform; D-Bus actions cover it
}
// NOLINTEND(performance-unnecessary-value-param)

void tray_stop() {
    // Nothing was started.
}

} // namespace platform
