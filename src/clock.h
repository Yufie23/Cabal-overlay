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

#include <chrono>
#include <string>

// Builds the local-time text for the bar. The game shows server
// time natively, so only the machine's own clock is displayed.
std::string local_clock_text(std::chrono::system_clock::time_point now);
