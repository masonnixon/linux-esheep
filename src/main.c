/* Transparent, always-on-top GTK window rendering the interpreter's current
 * sprite frame, with real x/y movement driven by each animation's pose
 * deltas and collision against X11 client windows, panels, and the monitor
 * workarea.
 */
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include "interpreter.h"

#define TICK_MS 33

#ifndef ESHEEP_DATADIR
#define ESHEEP_DATADIR "assets" /* dev-build default: run from the repo root */
#endif

#define ANIM_WALK 1
#define ANIM_DRAG 4
#define ANIM_FALL 5
#define MAX_OBJECTS 128

typedef struct {
    GdkRectangle rect;
    gboolean taskbar;
} DesktopObject;

typedef struct {
    GtkWidget *window;
    GdkPixbuf *sheet;
    EsheepState state;
    int tile_size;
    GdkRectangle bounds; /* primary monitor geometry, the whole "world" for now */
    int pos_x, pos_y;    /* top-left of the sprite window, in screen coords */
    gboolean dragging;
    int drag_grab_x, drag_grab_y; /* pointer offset from window origin at grab time */
    Window xwindow;
    DesktopObject objects[MAX_OBJECTS];
    int object_count;
} App;

static gboolean rects_overlap_x(int left_a, int width_a, int left_b, int width_b) {
    return left_a < left_b + width_b && left_a + width_a > left_b;
}

static gboolean get_window_type(Display *display, Window window, Atom type_atom,
                                Atom *type) {
    Atom actual_type;
    int format;
    unsigned long count, bytes_after;
    unsigned char *data = NULL;
    int result = XGetWindowProperty(display, window, type_atom, 0, 1, False,
                                    XA_ATOM, &actual_type, &format, &count,
                                    &bytes_after, &data);
    if (result != Success || !data || count == 0 || format != 32) {
        if (data) XFree(data);
        return FALSE;
    }
    *type = ((Atom *)data)[0];
    XFree(data);
    return TRUE;
}

static gboolean window_has_type(Display *display, Window window, Atom type_atom,
                                Atom wanted) {
    Atom type;
    return get_window_type(display, window, type_atom, &type) && type == wanted;
}

static gboolean is_conky_window(Display *display, Window window) {
    XClassHint class_hint = {0};
    gboolean is_conky = FALSE;
    if (XGetClassHint(display, window, &class_hint)) {
        is_conky = (class_hint.res_name &&
                    strcasecmp(class_hint.res_name, "conky") == 0) ||
                   (class_hint.res_class &&
                    strcasecmp(class_hint.res_class, "conky") == 0);
        if (class_hint.res_name) XFree(class_hint.res_name);
        if (class_hint.res_class) XFree(class_hint.res_class);
    }
    return is_conky;
}

static void refresh_objects(App *app) {
    app->object_count = 0;
    GdkDisplay *gdk_display = gtk_widget_get_display(app->window);
    if (!GDK_IS_X11_DISPLAY(gdk_display)) return;

    Display *display = gdk_x11_display_get_xdisplay(gdk_display);
    Window root = DefaultRootWindow(display);
    Atom client_list = XInternAtom(display, "_NET_CLIENT_LIST", False);
    Atom window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom dock_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", False);
    Atom desktop_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    Atom actual_type;
    int format;
    unsigned long count, bytes_after;
    Window *windows = NULL;
    int result = XGetWindowProperty(display, root, client_list, 0, MAX_OBJECTS,
                                    False, XA_WINDOW, &actual_type, &format,
                                    &count, &bytes_after,
                                    (unsigned char **)&windows);
    if (result != Success || !windows || format != 32) {
        if (windows) XFree(windows);
        return;
    }

    for (unsigned long i = 0; i < count && app->object_count < MAX_OBJECTS; i++) {
        if (windows[i] == app->xwindow) continue;
        if (window_has_type(display, windows[i], window_type, desktop_type)) continue;
        if (is_conky_window(display, windows[i])) continue;
        XWindowAttributes attributes;
        if (!XGetWindowAttributes(display, windows[i], &attributes) ||
            attributes.map_state != IsViewable || attributes.class != InputOutput ||
            attributes.width <= 1 || attributes.height <= 1) continue;

        int root_x, root_y;
        Window child;
        if (!XTranslateCoordinates(display, windows[i], root, 0, 0, &root_x,
                                   &root_y, &child)) continue;

        Atom type;
        gboolean is_taskbar = get_window_type(display, windows[i], window_type, &type) &&
                              type == dock_type;
        DesktopObject *object = &app->objects[app->object_count++];
        object->rect = (GdkRectangle){ root_x, root_y, attributes.width,
                                       attributes.height };
        object->taskbar = is_taskbar;
    }
    XFree(windows);
}

static const char *object_underfoot(const App *app) {
    int bottom = app->pos_y + app->tile_size;
    for (int i = 0; i < app->object_count; i++) {
        const DesktopObject *object = &app->objects[i];
        if (bottom == object->rect.y &&
            rects_overlap_x(app->pos_x, app->tile_size, object->rect.x,
                            object->rect.width))
            return object->taskbar ? "taskbar" : "window";
    }
    return NULL;
}

static void set_sprite_input_region(App *app) {
    GdkWindow *window = gtk_widget_get_window(app->window);
    if (!window) return;

    int tile = esheep_current_tile(&app->state);
    int sx = (tile % esheep_tiles_x) * app->tile_size;
    int sy = (tile / esheep_tiles_x) * app->tile_size;
    int rowstride = gdk_pixbuf_get_rowstride(app->sheet);
    int channels = gdk_pixbuf_get_n_channels(app->sheet);
    const guchar *pixels = gdk_pixbuf_get_pixels(app->sheet);
    cairo_region_t *region = cairo_region_create();

    for (int y = 0; y < app->tile_size; y++) {
        int run_start = -1;
        for (int x = 0; x <= app->tile_size; x++) {
            gboolean opaque = FALSE;
            if (x < app->tile_size) {
                const guchar *pixel = pixels + (sy + y) * rowstride +
                                       (sx + x) * channels;
                opaque = channels < 4 || pixel[3] > 16;
            }
            if (opaque && run_start < 0) run_start = x;
            if (!opaque && run_start >= 0) {
                cairo_rectangle_int_t rect = { run_start, y, x - run_start, 1 };
                cairo_region_union_rectangle(region, &rect);
                run_start = -1;
            }
        }
    }
    /* Apply the alpha mask to both input and visible bounds.  The latter
     * keeps X11 compositors from drawing a shadow or translucent square for
     * the full tile around the sheep. */
    gdk_window_shape_combine_region(window, region, 0, 0);
    gdk_window_input_shape_combine_region(window, region, 0, 0);
    cairo_region_destroy(region);
}

/* Apply the pose x/y deltas (constant per animation in this dataset -- start
 * and end always match for every animation currently in scope) for one
 * frame step of `anim`, then clamp to the monitor bounds. Returns a context
 * string ("none" or "vertical") describing which edge, if any, was hit
 * this step, for the caller to feed into esheep_border_event. */
static const char *step_position(App *app, const EsheepAnimation *anim) {
    int old_y = app->pos_y;
    int dx = atoi(anim->start.x);
    int dy = atoi(anim->start.y);
    app->pos_x += dx;
    app->pos_y += dy;

    const char *context = "none";
    if (dy > 0) {
        int old_bottom = old_y + app->tile_size;
        int new_bottom = app->pos_y + app->tile_size;
        for (int i = 0; i < app->object_count; i++) {
            const DesktopObject *object = &app->objects[i];
            if (old_bottom <= object->rect.y && new_bottom >= object->rect.y &&
                rects_overlap_x(app->pos_x, app->tile_size, object->rect.x,
                                object->rect.width)) {
                app->pos_y = object->rect.y - app->tile_size;
                context = object->taskbar ? "taskbar" : "window";
                break;
            }
        }
    }

    int floor_y = app->bounds.y + app->bounds.height - app->tile_size;
    if (app->pos_y > floor_y) app->pos_y = floor_y;
    if (app->pos_y < app->bounds.y) app->pos_y = app->bounds.y;

    if (context[0] != 'n') return context;
    if (app->pos_x <= app->bounds.x) {
        app->pos_x = app->bounds.x;
        context = "vertical";
    } else if (app->pos_x + app->tile_size >= app->bounds.x + app->bounds.width) {
        app->pos_x = app->bounds.x + app->bounds.width - app->tile_size;
        context = "vertical";
    }
    const char *surface = object_underfoot(app);
    return surface ? surface : context;
}

static void draw_current_tile(cairo_t *cr, App *app) {
    const EsheepAnimation *anim = &esheep_animations[app->state.animation_id - 1];
    int tile = esheep_current_tile(&app->state);
    int tile_size = gdk_pixbuf_get_width(app->sheet) / esheep_tiles_x;
    int sx = (tile % esheep_tiles_x) * tile_size;
    int sy = (tile / esheep_tiles_x) * tile_size;

    cairo_save(cr);
    /* This is a transparent toplevel that moves frequently.  CLEAR removes
     * both the pixel data and its alpha from the complete invalidated area,
     * including pixels left behind by the previous frame. */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    GdkPixbuf *subtile = gdk_pixbuf_new_subpixbuf(app->sheet, sx, sy, tile_size, tile_size);
    gdk_cairo_set_source_pixbuf(cr, subtile, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
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
        set_sprite_input_region(app);
        return G_SOURCE_CONTINUE;
    }

    refresh_objects(app);
    int prev_anim = app->state.animation_id;
    int prev_frame = app->state.frame_index;
    int prev_repeat = app->state.repeat_index;
    /* Movement/collision context is decided by the CURRENT position, before
     * this tick's frame step -- e.g. if we're already pinned against the
     * right edge, this tick's context is "vertical" regardless of which
     * direction the current animation is trying to move. */
    const char *pretick_context = object_underfoot(app);
    if (!pretick_context &&
        (app->pos_x <= app->bounds.x ||
         app->pos_x + app->tile_size >= app->bounds.x + app->bounds.width))
        pretick_context = "vertical";
    if (!pretick_context) pretick_context = "none";

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
     * fits that, and it's a single-frame context animation, so not fixed
     * here; a fully
     * robust fix would have esheep_tick report "a step happened" directly
     * rather than reconstructing it from state deltas. */
    if (app->state.animation_id != prev_anim ||
        app->state.frame_index != prev_frame ||
        app->state.repeat_index != prev_repeat) {
        /* A frame boundary was crossed this tick -- apply the animation that
         * was PLAYING during that step's own pose delta, not the new one. */
        const EsheepAnimation *stepped_anim = &esheep_animations[prev_anim - 1];
        const char *hit = step_position(app, stepped_anim);
        if (hit[0] != 'n') { /* a screen edge, window, or taskbar */
            int border_roll = rand() % 100;
            esheep_border_event(&app->state, hit, border_roll);
        }
    }

    gtk_window_move(GTK_WINDOW(app->window), app->pos_x, app->pos_y);
    gtk_widget_queue_draw(app->window);
    set_sprite_input_region(app);
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
        set_sprite_input_region(app);
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
        refresh_objects(app);
        set_sprite_input_region(app);
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
    char default_sheet_path[4096];
    if (!sheet_path) {
        snprintf(default_sheet_path, sizeof(default_sheet_path),
                 "%s/sheep_spritesheet.png", ESHEEP_DATADIR);
        sheet_path = default_sheet_path;
    }

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
    /* GTK's intermediate backing buffer can retain rectangular fragments
     * when an RGBA popup is moved.  Let the compositor use the surface we
     * paint directly instead. */
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_widget_set_double_buffered(window, FALSE);
    G_GNUC_END_IGNORE_DEPRECATIONS
    gtk_window_set_default_size(GTK_WINDOW(window), tile_size, tile_size);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    gtk_window_stick(GTK_WINDOW(window));

    GdkDisplay *display = gdk_display_get_default();
    /* Spawn on whichever monitor the pointer is actually on, not GDK's
     * notion of "primary" -- on a multi-monitor setup those can easily
     * differ, and a sheep spawning on a monitor you're not looking at
     * just looks like the app did nothing. */
    GdkMonitor *monitor = NULL;
    GdkSeat *seat = gdk_display_get_default_seat(display);
    if (seat) {
        GdkDevice *pointer = gdk_seat_get_pointer(seat);
        if (pointer) {
            GdkScreen *pointer_screen;
            int px, py;
            gdk_device_get_position(pointer, &pointer_screen, &px, &py);
            monitor = gdk_display_get_monitor_at_point(display, px, py);
        }
    }
    if (!monitor) monitor = gdk_display_get_primary_monitor(display);
    if (!monitor) monitor = gdk_display_get_monitor(display, 0);
    /* workarea excludes panels/docks/taskbars -- using raw geometry here
     * would let the sheep spawn flush with the physical bottom edge of the
     * screen, which on most desktops means directly underneath (and fully
     * hidden by) a bottom panel. */
    gdk_monitor_get_workarea(monitor, &app.bounds);
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

    if (GDK_IS_X11_DISPLAY(display)) {
        app.xwindow = gdk_x11_window_get_xid(gtk_widget_get_window(window));
        refresh_objects(&app);
        /* Start on a visible application window when one exists.  A window
         * manager panel is tracked for landing, but is not a spawn target. */
        for (int i = app.object_count - 1; i >= 0; i--) {
            DesktopObject *object = &app.objects[i];
            if (object->taskbar || object->rect.width < app.tile_size) continue;
            int spawn_x = object->rect.x + (object->rect.width - app.tile_size) / 2;
            int spawn_y = object->rect.y - app.tile_size;
            if (spawn_y < app.bounds.y) continue;
            app.pos_x = spawn_x;
            app.pos_y = spawn_y;
            break;
        }
        if (app.pos_x < app.bounds.x) app.pos_x = app.bounds.x;
        if (app.pos_x + app.tile_size > app.bounds.x + app.bounds.width)
            app.pos_x = app.bounds.x + app.bounds.width - app.tile_size;
        if (app.pos_y < app.bounds.y) app.pos_y = app.bounds.y;
        gtk_window_move(GTK_WINDOW(window), app.pos_x, app.pos_y);
    }
    set_sprite_input_region(&app);

    g_timeout_add(TICK_MS, on_tick, &app);

    const char *autoquit = getenv("ESHEEP_AUTOQUIT_MS");
    if (autoquit) {
        g_timeout_add((guint)atoi(autoquit), on_autoquit, NULL);
    }

    gtk_main();

    g_object_unref(sheet);
    return 0;
}
