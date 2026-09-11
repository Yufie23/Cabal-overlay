// ─────────────────────────────────────────────────────────────
// server_time.h — the one place that knows the server's timezone
//
// `inline` on a non-member function means: "this may be defined in
// several translation units; linker, they are all the same one".
// Combined with a function-local `static`, every caller shares a
// single cached timezone lookup.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <chrono>

// The Cabal server runs on Europe/Berlin (CET/CEST). The IANA
// timezone database handles daylight-saving switches for us.
// locate_zone never fails for a valid name; a bad name throws.
inline const std::chrono::time_zone* server_zone() {
    static const auto* zone = std::chrono::locate_zone("Europe/Berlin");
    return zone;
}
