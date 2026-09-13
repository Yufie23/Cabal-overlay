// ─────────────────────────────────────────────────────────────
// platform/sound.h — alarm sound contract
//
// One job: play the "an event is about to start" sound, fire and
// forget. The alarm pipeline (schedule → warning window → trigger)
// stays platform-free; only this thin layer knows HOW audio works
// on the host.
//
// Backend split:
//
//   sound_canberra.cpp   → freedesktop themed sounds via libcanberra
//                        (Linux; volume = per-play dB multiplier)
//   sound_windows.cpp    → PlaySound with the SystemExclamation alias
//                        (Win32 has no per-app volume for that API —
//                        the volume argument is accepted and ignored)
// ─────────────────────────────────────────────────────────────
#pragma once

namespace platform {

// Plays the alarm sound. Never blocks and never throws: a missing
// sound backend must never take the alarm notification down with it.
void play_alarm_bell(int volume_percent);

} // namespace platform
