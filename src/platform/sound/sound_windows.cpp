// ─────────────────────────────────────────────────────────────
// sound_windows.cpp — alarm sound via PlaySound
//
// PlaySound with a system alias is the simplest async sound on
// Win32: no device handling, no threading, returns immediately with
// SND_ASYNC. Its limitation is volume — the API has no per-app
// level, so [alarms] volume is accepted (keeps the caller
// platform-free) and documented as ignored here.
// ─────────────────────────────────────────────────────────────

#include "platform/sound/sound.h"

#include <windows.h>

namespace platform {

void play_alarm_bell(int /*volume_percent: no per-app volume on this API*/) {
    // SystemExclamation is the classic "alert" chime on every
    // Windows theme since forever; SND_ASYNC never blocks the tick.
    PlaySoundW(L"SystemExclamation", nullptr,
               SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
}

} // namespace platform
