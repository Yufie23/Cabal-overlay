// ─────────────────────────────────────────────────────────────
// update_check.cpp — async GitHub release check via GSubprocess.
//
// Why a subprocess and not an HTTP library: curl ships with Windows
// 10+ and every Linux distro, so the bundle gains zero dependencies
// and zero link surface. g_subprocess_communicate_utf8_async runs it
// without blocking the main loop — the callback lands on the main
// thread like any GLib signal.
// ─────────────────────────────────────────────────────────────

#include "update_check.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <gio/gio.h> // GSubprocess (GIO, not plain GLib)
#include <nlohmann/json.hpp>

namespace {

constexpr char kReleasesApi[] =
    "https://api.github.com/repos/Yufie23/Cabal-overlay/releases/latest";

struct Payload {
    std::function<void(const std::string&)> on_newer;
    std::string current;
};

// "v0.10.2" → {0, 10, 2}. Trailing junk after a number ("-rc1")
// ends the parse; a tag with no numbers at all yields an empty list,
// which is_newer treats as "not comparable, not newer".
std::vector<int> parse_version(std::string tag) {
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V'))
        tag = tag.substr(1);
    std::vector<int> parts;
    std::string current;
    const auto flush = [&] {
        parts.push_back(current.empty() ? 0 : std::stoi(current));
        current.clear();
    };
    for (const char c : tag) {
        if (c == '.') flush();
        else if (c >= '0' && c <= '9') current += c;
        else break; // suffix reached
    }
    if (!current.empty() || !parts.empty()) flush();
    return parts;
}

bool is_newer(const std::string& latest, const std::string& current) {
    const std::vector<int> a = parse_version(latest);
    const std::vector<int> b = parse_version(current);
    if (a.empty() || b.empty()) return false;
    const std::size_t longest = std::max(a.size(), b.size());
    for (std::size_t i = 0; i < longest; ++i) {
        const int x = i < a.size() ? a[i] : 0;
        const int y = i < b.size() ? b[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

void on_curl_done(GObject* source, GAsyncResult* result,
                  gpointer payload_ptr) {
    auto* payload = static_cast<Payload*>(payload_ptr);
    char* output = nullptr;
    GError* error = nullptr;
    const gboolean ok = g_subprocess_communicate_utf8_finish(
        G_SUBPROCESS(source), result, &output, nullptr, &error);
    if (!ok) {
        g_message("update check: %s (offline is fine; next run retries)",
                  error != nullptr ? error->message : "curl failed");
        if (error != nullptr) g_error_free(error);
    } else if (output != nullptr) {
        try {
            const nlohmann::json root = nlohmann::json::parse(output);
            const std::string tag = root.value("tag_name", "");
            if (!tag.empty() && is_newer(tag, payload->current)) {
                if (payload->on_newer) payload->on_newer(tag);
            } else {
                g_message("update check: %s is up to date",
                          payload->current.c_str());
            }
        } catch (const std::exception& parse_error) {
            g_warning("update check: unreadable API response: %s",
                      parse_error.what());
        }
        g_free(output);
    }
    delete payload;
}

} // anonymous namespace

namespace updates {

void check_latest(const std::string& current_version,
                  std::function<void(const std::string&)> on_newer) {
    GError* error = nullptr;
    GSubprocess* process = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE, &error,
        "curl", "-sf", "--max-time", "10",
        "-H", "Accept: application/vnd.github+json",
        kReleasesApi, nullptr);
    if (process == nullptr) {
        g_message("update check: cannot start curl: %s",
                  error != nullptr ? error->message : "unknown error");
        if (error != nullptr) g_error_free(error);
        return;
    }
    auto* payload = new Payload{ std::move(on_newer), current_version };
    g_subprocess_communicate_utf8_async(process, nullptr, nullptr,
                                        on_curl_done, payload);
    g_object_unref(process); // the async operation holds its own ref
}

} // namespace updates
