/* Phase 2: transparent always-on-top window rendering the current sprite
 * frame from the interpreter, driven by a fixed-rate timer. No movement or
 * collision yet (context is always "none") -- that's Phase 3.
 */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <time.h>
#include "interpreter.h"

#define TICK_MS 33

typedef struct {
    GtkWidget *window;
    GdkPixbuf *sheet;
    EsheepState state;
} App;

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
    int roll = rand() % 100;
    if (esheep_tick(&app->state, TICK_MS, "none", roll)) {
        /* transitioned to a new animation this tick */
    }
    gtk_widget_queue_draw(app->window);
    return G_SOURCE_CONTINUE;
}

static gboolean on_autoquit(gpointer user_data) {
    (void)user_data;
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    srand((unsigned)time(NULL));

    const char *sheet_path = getenv("ESHEEP_SPRITESHEET");
    if (!sheet_path) sheet_path = "assets/penguin_ice_blue_spritesheet.png";

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
    esheep_init(&app.state, 1); /* start on animation 1, "walk" */

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
    GdkRectangle geom;
    gdk_monitor_get_geometry(monitor, &geom);
    int start_x = geom.x + geom.width / 2;
    int start_y = geom.y + geom.height - tile_size;
    gtk_window_move(GTK_WINDOW(window), start_x, start_y);

    g_signal_connect(window, "draw", G_CALLBACK(on_draw), &app);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

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
