/* Transparent, always-on-top GTK window rendering the interpreter's current
 * sprite frame, with real x/y movement driven by each animation's pose
 * deltas and screen-edge collision against the primary monitor's bounds.
 * No per-window docking yet (context is only ever "none" or "vertical") --
 * that's a later phase.
 */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <time.h>
#include "interpreter.h"

#define TICK_MS 33

#define ANIM_WALK 1
#define ANIM_DRAG 4
#define ANIM_FALL 5

typedef struct {
    GtkWidget *window;
    GdkPixbuf *sheet;
    EsheepState state;
    int tile_size;
    GdkRectangle bounds; /* primary monitor geometry, the whole "world" for now */
    int pos_x, pos_y;    /* top-left of the sprite window, in screen coords */
    gboolean dragging;
    int drag_grab_x, drag_grab_y; /* pointer offset from window origin at grab time */
} App;

/* Apply the pose x/y deltas (constant per animation in this dataset -- start
 * and end always match for every animation currently in scope) for one
 * frame step of `anim`, then clamp to the monitor bounds. Returns a context
 * string ("none" or "vertical") describing which edge, if any, was hit
 * this step, for the caller to feed into esheep_border_event. */
static const char *step_position(App *app, const EsheepAnimation *anim) {
    int dx = atoi(anim->start.x);
    int dy = atoi(anim->start.y);
    app->pos_x += dx;
    app->pos_y += dy;

    int floor_y = app->bounds.y + app->bounds.height - app->tile_size;
    if (app->pos_y > floor_y) app->pos_y = floor_y;
    if (app->pos_y < app->bounds.y) app->pos_y = app->bounds.y;

    const char *context = "none";
    if (app->pos_x <= app->bounds.x) {
        app->pos_x = app->bounds.x;
        context = "vertical";
    } else if (app->pos_x + app->tile_size >= app->bounds.x + app->bounds.width) {
        app->pos_x = app->bounds.x + app->bounds.width - app->tile_size;
        context = "vertical";
    }
    return context;
}

static void draw_current_tile(cairo_t *cr, App *app) {
    const EsheepAnimation *anim = &esheep_animations[app->state.animation_id - 1];
    int tile = esheep_current_tile(&app->state);
    int tile_size = gdk_pixbuf_get_width(app->sheet) / esheep_tiles_x;
    int sx = (tile % esheep_tiles_x) * tile_size;
    int sy = (tile / esheep_tiles_x) * tile_size;

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    GdkPixbuf *subtile = gdk_pixbuf_new_subpixbuf(app->sheet, sx, sy, tile_size, tile_size);
    gdk_cairo_set_source_pixbuf(cr, subtile, 0, 0);
    cairo_paint(cr);
    g_object_unref(subtile);
    cairo_restore(cr);

    (void)anim; /* reserved for Phase 3 flip/opacity handling */
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)widget;
    draw_current_tile(cr, (App *)user_data);
    return FALSE;
}

static gboolean on_tick(gpointer user_data) {
    App *app = user_data;

    if (app->dragging) {
        /* Position is driven by the pointer while dragging; still let the
         * interpreter step so the drag animation's frames keep cycling. */
        int roll = rand() % 100;
        esheep_tick(&app->state, TICK_MS, "none", roll);
        gtk_widget_queue_draw(app->window);
        return G_SOURCE_CONTINUE;
    }

    int prev_anim = app->state.animation_id;
    int prev_frame = app->state.frame_index;
    int prev_repeat = app->state.repeat_index;
    /* Movement/collision context is decided by the CURRENT position, before
     * this tick's frame step -- e.g. if we're already pinned against the
     * right edge, this tick's context is "vertical" regardless of which
     * direction the current animation is trying to move. */
    const char *pretick_context =
        (app->pos_x <= app->bounds.x ||
         app->pos_x + app->tile_size >= app->bounds.x + app->bounds.width)
        ? "vertical" : "none";

    int roll = rand() % 100;
    esheep_tick(&app->state, TICK_MS, pretick_context, roll);

    /* A frame boundary was crossed this tick if the animation changed, the
     * frame index moved, OR the repeat_index advanced -- that last case
     * covers single-frame animations (e.g. "fall", frames=[133]) where
     * frame_index wraps right back to the same value every step, so
     * comparing (animation_id, frame_index) alone misses it and the sprite
     * never moves. Known residual gap: an animation with BOTH frame_count
     * 1 and repeat "0" (infinite loop) would still evade this -- neither
     * frame_index nor repeat_index ever change. Only "fall_wina" (id 51)
     * fits that, and it's a window-docking animation unreachable without
     * that (not yet implemented) context, so not fixed here; a fully
     * robust fix would have esheep_tick report "a step happened" directly
     * rather than reconstructing it from state deltas. */
    if (app->state.animation_id != prev_anim ||
        app->state.frame_index != prev_frame ||
        app->state.repeat_index != prev_repeat) {
        /* A frame boundary was crossed this tick -- apply the animation that
         * was PLAYING during that step's own pose delta, not the new one. */
        const EsheepAnimation *stepped_anim = &esheep_animations[prev_anim - 1];
        const char *hit = step_position(app, stepped_anim);
        if (hit[0] != 'n') { /* "vertical", not "none" */
            int border_roll = rand() % 100;
            esheep_border_event(&app->state, hit, border_roll);
        }
    }

    gtk_window_move(GTK_WINDOW(app->window), app->pos_x, app->pos_y);
    gtk_widget_queue_draw(app->window);
    return G_SOURCE_CONTINUE;
}

static gboolean on_autoquit(gpointer user_data) {
    (void)user_data;
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

static void on_quit_activate(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    gtk_main_quit();
}

static void show_quit_menu(App *app, GdkEventButton *event) {
    (void)app;
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_quit_activate), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    App *app = user_data;
    (void)widget;

    if (event->button == 1) {
        app->dragging = TRUE;
        app->drag_grab_x = (int)event->x;
        app->drag_grab_y = (int)event->y;
        esheep_init(&app->state, ANIM_DRAG);
    } else if (event->button == 3) {
        show_quit_menu(app, event);
    }
    return TRUE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    App *app = user_data;
    (void)widget;

    if (event->button == 1 && app->dragging) {
        app->dragging = FALSE;
        gtk_window_get_position(GTK_WINDOW(app->window), &app->pos_x, &app->pos_y);
        int floor_y = app->bounds.y + app->bounds.height - app->tile_size;
        esheep_init(&app->state, app->pos_y < floor_y ? ANIM_FALL : ANIM_WALK);
    }
    return TRUE;
}

static gboolean on_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    App *app = user_data;
    (void)widget;

    if (app->dragging) {
        app->pos_x = (int)event->x_root - app->drag_grab_x;
        app->pos_y = (int)event->y_root - app->drag_grab_y;
        gtk_window_move(GTK_WINDOW(app->window), app->pos_x, app->pos_y);
    }
    return TRUE;
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    srand((unsigned)time(NULL));

    const char *sheet_path = getenv("ESHEEP_SPRITESHEET");
    if (!sheet_path) sheet_path = "assets/sheep_spritesheet.png";

    GError *error = NULL;
    GdkPixbuf *sheet = gdk_pixbuf_new_from_file(sheet_path, &error);
    if (!sheet) {
        g_printerr("failed to load spritesheet '%s': %s\n", sheet_path,
                   error ? error->message : "unknown error");
        return 1;
    }

    int tile_size = gdk_pixbuf_get_width(sheet) / esheep_tiles_x;

    App app = {0};
    app.sheet = sheet;
    app.tile_size = tile_size;
    esheep_init(&app.state, ANIM_WALK);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_POPUP);
    app.window = window;

    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(window, visual);
    }

    gtk_widget_set_app_paintable(window, TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), tile_size, tile_size);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    gtk_window_stick(GTK_WINDOW(window));

    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
    if (!monitor) monitor = gdk_display_get_monitor(display, 0);
    gdk_monitor_get_geometry(monitor, &app.bounds);
    app.pos_x = app.bounds.x + app.bounds.width / 2;
    app.pos_y = app.bounds.y + app.bounds.height - tile_size;
    gtk_window_move(GTK_WINDOW(window), app.pos_x, app.pos_y);

    gtk_widget_add_events(window, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                       GDK_POINTER_MOTION_MASK);

    g_signal_connect(window, "draw", G_CALLBACK(on_draw), &app);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(window, "button-press-event", G_CALLBACK(on_button_press), &app);
    g_signal_connect(window, "button-release-event", G_CALLBACK(on_button_release), &app);
    g_signal_connect(window, "motion-notify-event", G_CALLBACK(on_motion), &app);

    gtk_widget_show_all(window);

    g_timeout_add(TICK_MS, on_tick, &app);

    const char *autoquit = getenv("ESHEEP_AUTOQUIT_MS");
    if (autoquit) {
        g_timeout_add((guint)atoi(autoquit), on_autoquit, NULL);
    }

    gtk_main();

    g_object_unref(sheet);
    return 0;
}
