// ─────────────────────────────────────────────────────────────
// sound_canberra.cpp — themed alarm sounds via libcanberra
//
// libcanberra is the freedesktop sound API KDE/GNOME themes
// implement. The context is created lazily on the first alarm and
// lives until process exit — destroying it right after
// ca_context_play() would cancel the queued sample. Playback is
// asynchronous: the tick never blocks on audio.
// ─────────────────────────────────────────────────────────────

#include "platform/sound/sound.h"

#include <glib.h>

#include <canberra.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace {

ca_context* g_context = nullptr;

} // anonymous namespace

namespace platform {

void play_alarm_bell(int volume_percent) {
    if (g_context == nullptr &&
        ca_context_create(&g_context) != CA_SUCCESS) {
        g_context = nullptr; // no sound support; alarm still shows
        return;
    }
    // volume_percent is a 0-100 fader; canberra wants a decibel
    // multiplier per play (0 dB = theme default). Linear amplitude →
    // dB keeps 100 at "default loudness" and 50 at half amplitude
    // (-6 dB), like an audio fader.
    const double percent = std::clamp(volume_percent, 0, 100);
    const double decibels = 20.0 * std::log10(percent / 100.0);
    const std::string volume = std::format("{:.2f}", decibels);
    // "bell" is part of the base freedesktop sound set; themes with
    // richer sets substitute their own sample for it.
    const int result = ca_context_play(
        g_context, 0,
        CA_PROP_EVENT_ID, "bell",
        CA_PROP_EVENT_DESCRIPTION, "Cabal overlay alarm",
        CA_PROP_CANBERRA_VOLUME, volume.c_str(),
        nullptr);
    if (result != CA_SUCCESS)
        g_warning("alarm sound failed: %s", ca_strerror(result));
}

} // namespace platform
