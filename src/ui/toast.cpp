// ─────────────────────────────────────────────────────────────
// toast.cpp — the notification surface. One at a time: showing a
// new toast while another is alive just replaces its content and
// restarts the 4-second fade.
// ─────────────────────────────────────────────────────────────

#include "toast.h"

#include <glib.h>

#include "platform/overlay/overlay.h"

namespace {

constexpr guint kToastLifetimeMs = 4000;
constexpr guint kFadeStepMs = 50;
constexpr int kFadeSteps = 8; // 400 ms of fade after ~3.6 s

struct ToastState {
    GtkWindow* window = nullptr;
    GtkWidget* title_label = nullptr;
    GtkWidget* body_label = nullptr;
    guint fade_source = 0;
    int fade_step = 0;
};

ToastState g_toast;

void stop_fade() {
    if (g_toast.fade_source != 0) {
        g_source_remove(g_toast.fade_source);
        g_toast.fade_source = 0;
    }
}

gboolean on_fade_step(gpointer) {
    ++g_toast.fade_step;
    if (g_toast.fade_step >= kFadeSteps) {
        gtk_window_destroy(g_toast.window);
        g_toast = ToastState{};
        return G_SOURCE_REMOVE;
    }
    gtk_widget_set_opacity(
        GTK_WIDGET(g_toast.window),
        1.0 - static_cast<double>(g_toast.fade_step) / kFadeSteps);
    return G_SOURCE_CONTINUE;
}

} // anonymous namespace

namespace toast {

void show(GtkApplication* app, const std::string& title,
          const std::string& body) {
    if (g_toast.window == nullptr) {
        g_toast.window = GTK_WINDOW(gtk_application_window_new(app));
        gtk_widget_add_css_class(GTK_WIDGET(g_toast.window),
                                 "overlay-window");
        platform::overlay_init(g_toast.window);

        auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_add_css_class(box, "overlay-bar");
        g_toast.title_label = gtk_label_new("");
        gtk_widget_add_css_class(g_toast.title_label, "toast-title");
        gtk_box_append(GTK_BOX(box), g_toast.title_label);
        g_toast.body_label = gtk_label_new("");
        gtk_widget_add_css_class(g_toast.body_label, "toast-body");
        gtk_box_append(GTK_BOX(box), g_toast.body_label);

        gtk_window_set_child(g_toast.window, box);
        platform::overlay_apply_placement(
            g_toast.window,
            platform::Placement{ .anchor = "top",
                                 .margin_x = 0,
                                 .margin_y = 40,
                                 .opacity = 0.95 });
        gtk_window_present(g_toast.window);
    }

    gtk_label_set_text(GTK_LABEL(g_toast.title_label), title.c_str());
    gtk_label_set_text(GTK_LABEL(g_toast.body_label), body.c_str());
    gtk_widget_set_opacity(GTK_WIDGET(g_toast.window), 1.0);

    stop_fade();
    g_toast.fade_step = 0;
    g_toast.fade_source = g_timeout_add(kToastLifetimeMs / kFadeSteps,
                                        on_fade_step, nullptr);
}

} // namespace toast
