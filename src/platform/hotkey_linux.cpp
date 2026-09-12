// ─────────────────────────────────────────────────────────────
// platform/hotkey_linux.cpp — global hotkey via evdev
//
// Reads keyboard events straight from /dev/input/event*. This works
// under every compositor (KWin, Mutter, wlroots) and while any
// window — the game included — holds the focus, because the kernel
// feeds every device to every reader. Devices are opened O_RDONLY
// and never grabbed, so the game still receives the keys.
//
// Detection is a small state machine shared by all keyboard devices:
// modifier live-state plus a press (value == 1, autorepeat value 2
// ignored) of the target key while the required modifiers are down.
//
// Permission model: /dev/input/event* is root:input 0660, so the
// user must belong to the input group (sudo usermod -aG input $USER,
// then re-login). Every failure path logs one actionable warning.
// ─────────────────────────────────────────────────────────────

#include "hotkey.h"

#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <sys/ioctl.h>

#include <cerrno>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <utility>

#include <unistd.h>

#include <glib-unix.h>

namespace {

// What to hold + what to press.
struct Combo {
    unsigned modifiers = 0;
    int key_code = -1;
};

constexpr unsigned kModShift = 1u << 0;
constexpr unsigned kModCtrl  = 1u << 1;
constexpr unsigned kModAlt   = 1u << 2;

Combo        g_combo;
std::function<void()> g_on_trigger;

// Live modifier state, shared across all keyboard devices. Two
// keyboards pressed at once is not worth per-device bookkeeping.
bool g_shift_down = false;
bool g_ctrl_down = false;
bool g_alt_down = false;

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// "space" → KEY_SPACE, "f9" → KEY_F9, "p" → KEY_P. Returns -1 for
// anything we do not map — callers fail loudly on unknown names
// instead of silently listening for a key that can never fire.
int lookup_key(const std::string& name) {
    static const std::pair<const char*, int> kNamedKeys[] = {
        {"space", KEY_SPACE},
        {"tab",   KEY_TAB},
        {"esc",   KEY_ESC},
        {"enter", KEY_ENTER},
        {"up",    KEY_UP},
        {"down",  KEY_DOWN},
        {"left",  KEY_LEFT},
        {"right", KEY_RIGHT},
    };
    for (const auto& [text, code] : kNamedKeys)
        if (name == text) return code;

    if (name.size() == 1) {
        const char c = name[0];
        if (c >= 'a' && c <= 'z') return KEY_A + (c - 'a');
        if (c >= '0' && c <= '9') return KEY_0 + (c - '0');
        return -1;
    }

    // F1-F12: 'f' followed by digits.
    if (name.size() >= 2 && name[0] == 'f') {
        const int n = std::stoi(name.substr(1)); // throws on garbage
        if (n >= 1 && n <= 12) return KEY_F1 + (n - 1);
    }
    return -1;
}

// "Shift+Space" → {modifiers: kModShift, key_code: KEY_SPACE}.
// Throws std::runtime_error on a malformed combo.
Combo parse_combo(const std::string& text) {
    Combo combo;
    std::size_t start = 0;
    while (true) {
        const std::size_t plus = text.find('+', start);
        const std::string part =
            lower(text.substr(start, plus == std::string::npos
                                       ? std::string::npos
                                       : plus - start));
        // Trim spaces around each part ("Shift + Space").
        const std::size_t begin = part.find_first_not_of(' ');
        const std::size_t end = part.find_last_not_of(' ');
        if (begin == std::string::npos)
            throw std::runtime_error("empty part in combo: '" + text + "'");
        const std::string token = part.substr(begin, end - begin + 1);

        if (token == "shift")      combo.modifiers |= kModShift;
        else if (token == "ctrl" || token == "control") combo.modifiers |= kModCtrl;
        else if (token == "alt")   combo.modifiers |= kModAlt;
        else if (combo.key_code == -1) combo.key_code = lookup_key(token);
        else
            throw std::runtime_error("combo has more than one plain key: '" + text + "'");

        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    if (combo.key_code == -1)
        throw std::runtime_error("combo has no key: '" + text + "'");
    return combo;
}

bool test_bit(const unsigned char* bitfield, int bit) {
    return (bitfield[bit / 8] >> (bit % 8)) & 1;
}

// Keyboard heuristic: has EV_KEY, lacks EV_REL/EV_ABS (mice and
// touchpads carry those). Also requires the target key to exist on
// the device, so a keypad or foot pedal never becomes the hotkey.
bool is_usable_keyboard(int fd, int key_code) {
    unsigned char evbits[(EV_MAX + 8) / 8] = {};
    if (ioctl(fd, EVIOCGBIT(0, sizeof evbits), evbits) < 0) return false;
    if (!test_bit(evbits, EV_KEY)) return false;
    if (test_bit(evbits, EV_REL) || test_bit(evbits, EV_ABS)) return false;

    unsigned char keybits[(KEY_MAX + 8) / 8] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keybits), keybits) < 0) return false;
    return test_bit(keybits, key_code);
}

// One opened keyboard device. Freed (and closed) by GLib when its
// fd source is destroyed — the destroy notify attached in hotkey_start.
struct DeviceSource {
    int fd;
};

gboolean on_device_ready(gint fd, GIOCondition, gpointer) {
    input_event events[16];
    while (true) {
        const ssize_t count =
            read(fd, events, sizeof events); // O_NONBLOCK: never blocks
        if (count < 0) {
            if (errno == EAGAIN) break;      // drained for now
            if (errno == EINTR) continue;
            g_warning("hotkey: reading input device failed: %s",
                      std::strerror(errno));
            break;
        }
        const auto* first = events;
        const auto* last = events + count / static_cast<ssize_t>(sizeof(input_event));
        for (const auto* event = first; event != last; ++event) {
            if (event->type != EV_KEY) continue;
            const bool down = event->value != 0; // 1 = press, 2 = autorepeat
            switch (event->code) {
            case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: g_shift_down = down; break;
            case KEY_LEFTCTRL:  case KEY_RIGHTCTRL:  g_ctrl_down  = down; break;
            case KEY_LEFTALT:   case KEY_RIGHTALT:   g_alt_down   = down; break;
            default: break;
            }
            // Fire only on a fresh press (not autorepeat) of the
            // target key with every required modifier held.
            if (event->code != g_combo.key_code || event->value != 1) continue;
            const bool mods_ok =
                (!(g_combo.modifiers & kModShift) || g_shift_down) &&
                (!(g_combo.modifiers & kModCtrl)  || g_ctrl_down) &&
                (!(g_combo.modifiers & kModAlt)   || g_alt_down);
            if (mods_ok) g_on_trigger();
        }
    }
    return G_SOURCE_CONTINUE;
}

void on_source_destroy(gpointer data) {
    const auto* source = static_cast<const DeviceSource*>(data);
    close(source->fd);
    delete source;
}

} // anonymous namespace

namespace platform {

bool hotkey_start(const std::string& combo_text,
                  std::function<void()> on_trigger) {
    try {
        g_combo = parse_combo(combo_text);
    } catch (const std::exception& error) {
        g_warning("hotkey: %s", error.what());
        return false;
    }
    g_on_trigger = std::move(on_trigger);

    int opened = 0;
    bool denied = false;
    for (int n = 0; n < 64; ++n) {
        const std::string path = "/dev/input/event" + std::to_string(n);
        const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            if (errno == EACCES) denied = true; // exists but not readable
            continue;                            // ENOENT: end of devices
        }
        if (!is_usable_keyboard(fd, g_combo.key_code)) {
            close(fd);
            continue;
        }
        auto* source = new DeviceSource{fd};
        // The destroy notify closes the fd and frees the payload when
        // the source is removed (app shutdown).
        g_unix_fd_add_full(G_PRIORITY_DEFAULT, fd, G_IO_IN,
                           on_device_ready, source, on_source_destroy);
        g_message("hotkey: listening on %s for '%s'",
                  path.c_str(), combo_text.c_str());
        ++opened;
    }

    if (opened > 0) return true;

    if (denied)
        g_warning("hotkey: keyboard devices exist but are not readable — "
                  "run 'sudo usermod -aG input $USER' and re-login");
    else
        g_warning("hotkey: no usable keyboard device found for '%s'",
                  combo_text.c_str());
    return false;
}

} // namespace platform
