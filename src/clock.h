// ─────────────────────────────────────────────────────────────
// clock.h — public contract of the clock module
//
// A header (.h) contains DECLARATIONS: "this function exists and
// looks like this". The matching .cpp contains the DEFINITION:
// the actual body. Other files include the header and can call
// the function without ever seeing how it works inside.
//
// #pragma once = "include this file only once per compilation",
// the modern replacement for #ifndef/#define/#endif guards.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <string>

// Builds the bar text: Cabal server time (Europe/Berlin) and the
// machine's local time, both derived from one single clock reading.
std::string clock_text();
