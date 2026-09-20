// ─────────────────────────────────────────────────────────────
// tutorial.cpp — the wizard window: a GtkStack of pages plus
// Back/Next/Done navigation. One heap payload owns all state and
// dies with the window, so reopening is always a clean start.
// ─────────────────────────────────────────────────────────────

#include "tutorial.h"

#include <string>
#include <vector>

namespace {

// Page texts live here, one pair per wizard page. Keep them short:
// a tutorial nobody reads is worse than none.
struct Page {
    const char* title;
    const char* body;
};

const Page kPages[] = {
    {
        "Welcome to Cabal Overlay",
        "Two floating surfaces sit on top of your game:\n\n"
        "  •  The BAR — clock and countdowns to game events.\n"
        "  •  The GOALS PANEL — your dungeon task list.\n\n"
        "The overlay never touches the game: no injection, no memory "
        "reading, no input sent. It only draws on top and watches "
        "which window has focus.",
    },
    {
        "Click-through vs Interactive",
        "By default your clicks pass THROUGH to the game — the "
        "overlay is invisible to the mouse.\n\n"
        "Press the hotkey (Shift+Space on Windows, or your desktop "
        "shortcut on Linux) to make it clickable: drag the windows "
        "to reposition them, open settings, edit tasks.\n\n"
        "Press the combo again — or click the bar — to hand the "
        "mouse back to the game.",
    },
    {
        "The goals panel",
        "Each row is a task:\n\n"
        "  •  − / + buttons count runs; the bar fills to the goal.\n"
        "  •  Done tasks fade out and come back on the daily reset.\n"
        "  •  Drag rows to reorder them.\n\n"
        "In game, Ctrl+click the dungeon-clear dialog and the counter "
        "moves by itself — calibrate that spot once in Settings → "
        "DG Check.",
    },
    {
        "Lists and display",
        "The dropdown at the top switches between your CUSTOM list "
        "(yours to edit freely) and PRESET templates — fixed lists "
        "your clan can ship in data/task_lists.json.\n\n"
        "  •  \"Aa\" flips short codes ↔ full names.\n"
        "  •  \"▾\" collapses a long list to its first 3 tasks.",
    },
    {
        "Settings, alarms and quitting",
        "The ⚙ button opens the settings: alarms, positions, hotkey, "
        "DG Check zone. Everything applies live.\n\n"
        "On Windows the overlay lives in the SYSTEM TRAY — "
        "right-click its icon for Settings or Quit.\n\n"
        "Alarms chime before scheduled events; event times (including "
        "guild dungeon) are plain [[schedule]] entries in "
        "overlay.toml.\n\n"
        "This tutorial is always available from the \"?\" button in "
        "the panel.",
    },
};

struct Wizard {
    std::function<void()> on_done;
    GtkWidget* window;
    GtkWidget* stack;
    GtkWidget* back_button;
    GtkWidget* next_button;
    guint page = 0;
    bool finished = false; // on_done fires once, from exactly one path
};

constexpr guint kPageCount =
    sizeof(kPages) / sizeof(kPages[0]);

void delete_wizard(gpointer data) {
    delete static_cast<Wizard*>(data);
}

void finish(Wizard* wizard) {
    if (wizard->finished) return;
    wizard->finished = true;
    if (wizard->on_done) wizard->on_done();
    gtk_window_destroy(GTK_WINDOW(wizard->window));
}

void sync_navigation(Wizard* wizard) {
    gtk_widget_set_sensitive(wizard->back_button, wizard->page > 0);
    gtk_button_set_label(GTK_BUTTON(wizard->next_button),
                         wizard->page + 1 == kPageCount ? "Done" : "Next →");
}

void on_back(GtkButton*, gpointer wizard_ptr) {
    auto* wizard = static_cast<Wizard*>(wizard_ptr);
    if (wizard->page == 0) return;
    --wizard->page;
    gtk_stack_set_visible_child_name(
        GTK_STACK(wizard->stack),
        std::to_string(wizard->page).c_str());
    sync_navigation(wizard);
}

void on_next(GtkButton*, gpointer wizard_ptr) {
    auto* wizard = static_cast<Wizard*>(wizard_ptr);
    if (wizard->page + 1 >= kPageCount) {
        finish(wizard); // "Done" on the last page
        return;
    }
    ++wizard->page;
    gtk_stack_set_visible_child_name(
        GTK_STACK(wizard->stack),
        std::to_string(wizard->page).c_str());
    sync_navigation(wizard);
}

GtkWidget* make_page(const Page& page) {
    auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 10);

    auto* title = gtk_label_new(page.title);
    gtk_widget_add_css_class(title, "tutorial-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_append(GTK_BOX(box), title);

    auto* body = gtk_label_new(page.body);
    gtk_widget_add_css_class(body, "tutorial-body");
    gtk_label_set_xalign(GTK_LABEL(body), 0.0);
    gtk_label_set_wrap(GTK_LABEL(body), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(body), 52);
    gtk_box_append(GTK_BOX(box), body);
    return box;
}

void on_close_request(GtkWindow*, gpointer wizard_ptr) {
    // Closing the window mid-tour counts as done too — the tutorial
    // must never reappear just because someone dismissed it early.
    auto* wizard = static_cast<Wizard*>(wizard_ptr);
    if (wizard->finished) return;
    wizard->finished = true;
    if (wizard->on_done) wizard->on_done();
    // No destroy here: GTK is already doing it.
}

} // anonymous namespace

namespace tutorial {

void present(GtkApplication* app, std::function<void()> on_done) {
    auto* wizard = new Wizard{ std::move(on_done), nullptr, nullptr,
                               nullptr, nullptr, 0, false };

    wizard->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(wizard->window), "Cabal Overlay — Quick Tour");
    gtk_window_set_default_size(GTK_WINDOW(wizard->window), 480, -1);
    gtk_window_set_resizable(GTK_WINDOW(wizard->window), FALSE);
    g_object_set_data_full(G_OBJECT(wizard->window), "cabal-wizard", wizard,
                           delete_wizard);
    g_signal_connect(wizard->window, "close-request",
                     G_CALLBACK(on_close_request), wizard);

    auto* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    wizard->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(wizard->stack),
                                  GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    for (guint i = 0; i < kPageCount; ++i)
        gtk_stack_add_named(GTK_STACK(wizard->stack), make_page(kPages[i]),
                            std::to_string(i).c_str());
    gtk_box_append(GTK_BOX(root), wizard->stack);

    auto* nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(nav, 16);
    gtk_widget_set_margin_end(nav, 16);
    gtk_widget_set_margin_bottom(nav, 12);
    wizard->back_button = gtk_button_new_with_label("← Back");
    g_signal_connect(wizard->back_button, "clicked",
                     G_CALLBACK(on_back), wizard);
    gtk_box_append(GTK_BOX(nav), wizard->back_button);
    auto* spacer = gtk_label_new("");
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(nav), spacer);
    wizard->next_button = gtk_button_new_with_label("Next →");
    gtk_widget_add_css_class(wizard->next_button, "suggested-action");
    g_signal_connect(wizard->next_button, "clicked",
                     G_CALLBACK(on_next), wizard);
    gtk_box_append(GTK_BOX(nav), wizard->next_button);
    gtk_box_append(GTK_BOX(root), nav);

    gtk_window_set_child(GTK_WINDOW(wizard->window), root);
    sync_navigation(wizard);
    gtk_window_present(GTK_WINDOW(wizard->window));
}

} // namespace tutorial
