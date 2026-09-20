// ─────────────────────────────────────────────────────────────
// css.cpp — the stylesheet payload, extracted from main.cpp.
// ─────────────────────────────────────────────────────────────

#include "css.h"

#include <gtk/gtk.h>

namespace css {

// GTK styling works with CSS, same idea as the web tracker but
// applied to native widgets instead of DOM elements.
void apply() {
    auto* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, R"css(
        window { background-color: transparent; }
        .overlay-bar {
            background-color: alpha(black, 0.5);
            color: #ffd24d;
            font-family: monospace;
            font-size: 14px;
            padding: 6px 14px;
            border-radius: 8px;
            border: 1px solid alpha(#ffd24d, 0.4);
        }
        .goals-panel {
            background-color: alpha(black, 0.5);
            color: #ffd24d;
            padding: 10px 12px;
            border-radius: 8px;
            border: 1px solid alpha(#ffd24d, 0.4);
            min-width: 240px;
        }
        .goal-section {
            font-family: monospace;
            font-size: 11px;
            font-weight: bold;
            color: alpha(#ffd24d, 0.75);
            margin-top: 4px;
        }
        .goal-section:first-child { margin-top: 0; }
        .goal-more-hint {
            font-family: monospace;
            font-size: 11px;
            color: alpha(#ffd24d, 0.45);
        }
        .tutorial-title {
            font-size: 16px;
            font-weight: bold;
        }
        .tutorial-body {
            font-size: 13px;
        }
        .goal-name {
            font-family: monospace;
            font-size: 12px;
        }
        .goal-count {
            font-family: monospace;
            font-size: 12px;
            color: white;
        }
        .goals-panel progressbar trough {
            background-color: alpha(white, 0.15);
            border-radius: 3px;
            min-height: 6px;
        }
        .goals-panel progressbar progress {
            background-color: #ffd24d;
            border-radius: 3px;
            min-height: 6px;
        }
        .goal-bump {
            font-family: monospace;
            font-size: 11px;
            padding: 0 8px;
            min-height: 18px;
            background-color: alpha(white, 0.08);
            color: #ffd24d;
            border-radius: 4px;
        }
        .goal-bump:hover { background-color: alpha(#ffd24d, 0.25); }
        .goal-add-toggle {
            font-family: monospace;
            font-size: 11px;
            padding: 2px 8px;
            background-color: alpha(#ffd24d, 0.12);
            color: #ffd24d;
            border-radius: 4px;
        }
        .goal-add-toggle:hover { background-color: alpha(#ffd24d, 0.25); }
        .goal-form { border-top: 1px solid alpha(#ffd24d, 0.25); padding-top: 6px; }
        .goal-form-title {
            font-family: monospace;
            font-size: 11px;
            font-weight: bold;
            color: alpha(#ffd24d, 0.75);
        }
        .goal-form-entry, .goal-form-spin, .goal-form-add, .goal-form button {
            font-family: monospace;
            font-size: 12px;
        }
        .goal-form-add {
            background-color: alpha(#ffd24d, 0.25);
            color: #ffd24d;
            border-radius: 4px;
        }
    )css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider); // The display keeps its own reference now.
}

} // namespace css
