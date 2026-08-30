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
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include "interpreter.h"

#define TICK_MS 33
#define ESHEEP_VERSION "0.1.0"

#ifndef ESHEEP_DATADIR
#define ESHEEP_DATADIR "assets" /* dev-build default: run from the repo root */
#endif

#define ANIM_WALK 1
#define ANIM_DRAG 4
#define ANIM_FALL 5
#define MAX_OBJECTS 128
#define MAX_SHEEP 32

typedef struct {
    GdkRectangle rect;
    gboolean taskbar;
    int stack_order;
} DesktopObject;

typedef struct App App;

struct App {
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
    guint tick_ms;
    gboolean window_landing;
    gboolean exclude_conky;
    gboolean spawn_on_window;
    gboolean random_spawn;
    int climb_target_y;
    int direction; /* -1 = left, +1 = right */
    App *siblings;
    int sibling_count;
};

static gboolean env_bool(const char *name, gboolean fallback) {
    const char *value = getenv(name);
    if (!value) return fallback;
    return strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0 ||
           strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0;
}

static guint env_uint(const char *name, guint fallback, guint minimum, guint maximum) {
    const char *value = getenv(name);
    if (!value || !*value) return fallback;
    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (*end || parsed < minimum || parsed > maximum) return fallback;
    return (guint)parsed;
}

static gboolean env_equals(const char *name, const char *expected) {
    const char *value = getenv(name);
    return value && strcasecmp(value, expected) == 0;
}

static void choose_random_spawn(App *app) {
    int roll = rand() % 106;
    int floor_y = app->bounds.y + app->bounds.height - app->tile_size;
    if (roll < 20) {
        app->direction = -1;
        app->pos_x = app->bounds.x + app->bounds.width + 10;
        app->pos_y = floor_y;
        esheep_init(&app->state, ANIM_WALK);
    } else if (roll < 100) {
        app->direction = rand() % 2 ? 1 : -1;
        int usable_width = app->bounds.width - app->tile_size - 50;
        app->pos_x = app->bounds.x + 25 +
                     (usable_width > 0 ? rand() % usable_width : 0);
        app->pos_y = app->bounds.y - app->tile_size - 20;
        esheep_init(&app->state, ANIM_FALL);
    } else if (roll < 103) {
        app->direction = -1;
        app->pos_x = app->bounds.x + app->bounds.width + 10;
        app->pos_y = app->bounds.y + app->bounds.height / 2 - app->tile_size;
        esheep_init(&app->state, 21);
    } else {
        app->direction = -1;
        app->pos_x = app->bounds.x + app->bounds.width;
        app->pos_y = floor_y;
        esheep_init(&app->state, 28);
    }
}

static void print_usage(const char *program) {
    g_print("Usage: %s [options]\n\n", program);
    g_print("Options:\n");
    g_print("  --help                 Show this help.\n");
    g_print("  --version              Show the version.\n");
    g_print("  --sprite PATH          Use a spritesheet.\n");
    g_print("  --character NAME       Use sheep or penguin sprites.\n");
    g_print("  --config PATH          Load settings from an INI config file.\n");
    g_print("  --spawn MODE           Use bottom, window, or random spawn.\n");
    g_print("  --count N              Spawn N sheep (1-32).\n");
    g_print("  --no-window-landing    Disable window and panel landing.\n");
    g_print("  --allow-conky          Allow landing on Conky.\n");
    g_print("  --x11-fallback         Use XWayland when available.\n");
    g_print("  --tick-ms N            Set the update interval (10-1000).\n");
}

static void update_monitor_bounds(App *app) {
    GdkDisplay *display = gtk_widget_get_display(app->window);
    int center_x = app->pos_x + app->tile_size / 2;
    int center_y = app->pos_y + app->tile_size / 2;
    GdkMonitor *monitor = gdk_display_get_monitor_at_point(display, center_x,
                                                             center_y);
    if (!monitor) return;

    GdkRectangle workarea;
    gdk_monitor_get_workarea(monitor, &workarea);
    if (memcmp(&app->bounds, &workarea, sizeof(workarea)) != 0)
        app->bounds = workarea;
}

static gboolean rects_overlap_x(int left_a, int width_a, int left_b, int width_b) {
    return left_a < left_b + width_b && left_a + width_a > left_b;
}

static gboolean reverses_with_walk_direction(int animation_id) {
    switch (animation_id) {
    case 1: case 7: case 25: case 28: case 29: case 35:
    case 36: case 39: case 44: case 49: case 50: case 51:
        return TRUE;
    default:
        return FALSE;
    }
}

static int horizontal_delta(const App *app, const EsheepAnimation *anim,
                            int delta) {
    if (!reverses_with_walk_direction(anim->id)) return delta;
    return app->direction < 0 ? delta : -delta;
}

static int pose_delta(const App *app, const EsheepAnimation *anim,
                      int frame_index, gboolean x_axis);

static gboolean sprite_is_flipped(const App *app, const EsheepAnimation *anim) {
    return anim->flip ||
           (reverses_with_walk_direction(anim->id) && app->direction > 0);
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

static int stacking_order(const Window *stacking, unsigned long count, Window window) {
    for (unsigned long i = 0; i < count; i++)
        if (stacking[i] == window) return (int)i;
    return -1;
}

static void refresh_objects(App *app) {
    app->object_count = 0;
    if (!app->window_landing) return;
    GdkDisplay *gdk_display = gtk_widget_get_display(app->window);
    if (!GDK_IS_X11_DISPLAY(gdk_display)) return;

    Display *display = gdk_x11_display_get_xdisplay(gdk_display);
    Window root = DefaultRootWindow(display);
    Atom client_list = XInternAtom(display, "_NET_CLIENT_LIST", False);
    Atom window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom dock_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", False);
    Atom desktop_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    Atom client_list_stacking = XInternAtom(display, "_NET_CLIENT_LIST_STACKING", False);
    Atom actual_type;
    int format;
    unsigned long count, bytes_after;
    Window *windows = NULL;
    Window *stacking = NULL;
    int result = XGetWindowProperty(display, root, client_list, 0, MAX_OBJECTS,
                                    False, XA_WINDOW, &actual_type, &format,
                                    &count, &bytes_after,
                                    (unsigned char **)&windows);
    if (result != Success || !windows || format != 32) {
        if (windows) XFree(windows);
        return;
    }

    unsigned long stacking_count = 0;
    result = XGetWindowProperty(display, root, client_list_stacking, 0,
                                MAX_OBJECTS, False, XA_WINDOW, &actual_type,
                                &format, &stacking_count, &bytes_after,
                                (unsigned char **)&stacking);
    if (result != Success || !stacking || format != 32) {
        if (stacking) XFree(stacking);
        stacking = NULL;
        stacking_count = 0;
    }

    for (unsigned long i = 0; i < count && app->object_count < MAX_OBJECTS; i++) {
        gboolean own_window = windows[i] == app->xwindow;
        for (int sibling = 0; !own_window && sibling < app->sibling_count;
             sibling++)
            own_window = windows[i] == app->siblings[sibling].xwindow;
        if (own_window) continue;
        if (window_has_type(display, windows[i], window_type, desktop_type)) continue;
        if (app->exclude_conky && is_conky_window(display, windows[i])) continue;
        XWindowAttributes attributes;
        if (!XGetWindowAttributes(display, windows[i], &attributes) ||
            attributes.map_state != IsViewable || attributes.class != InputOutput ||
            attributes.width <= 1 || attributes.height <= 1) continue;

        /* _NET_CLIENT_LIST contains client windows.  Their origin starts
         * below the window-manager title bar, so use the parent frame for
         * collision geometry when one exists. */
        Window tree_root, parent, *children = NULL;
        unsigned int child_count = 0;
        Window geometry_window = windows[i];
        if (XQueryTree(display, windows[i], &tree_root, &parent, &children,
                       &child_count)) {
            if (parent != root) geometry_window = parent;
            if (children) XFree(children);
        }

        XWindowAttributes geometry;
        if (!XGetWindowAttributes(display, geometry_window, &geometry) ||
            geometry.width <= 1 || geometry.height <= 1) continue;
        int root_x, root_y;
        Window child;
        if (!XTranslateCoordinates(display, geometry_window, root, 0, 0, &root_x,
                                   &root_y, &child)) continue;

        Atom type;
        gboolean is_taskbar = get_window_type(display, windows[i], window_type, &type) &&
                              type == dock_type;
        DesktopObject *object = &app->objects[app->object_count++];
        object->rect = (GdkRectangle){ root_x, root_y, geometry.width,
                                       geometry.height };
        object->taskbar = is_taskbar;
        object->stack_order = stacking_order(stacking, stacking_count, windows[i]);
    }
    XFree(windows);
    if (stacking) XFree(stacking);
}

static const char *object_underfoot(const App *app) {
    int bottom = app->pos_y + app->tile_size;
    const DesktopObject *best = NULL;
    for (int i = 0; i < app->object_count; i++) {
        const DesktopObject *object = &app->objects[i];
        if (abs(bottom - object->rect.y) <= 2 &&
            rects_overlap_x(app->pos_x, app->tile_size, object->rect.x,
                            object->rect.width) &&
            (!best || object->stack_order > best->stack_order))
            best = object;
    }
    return best ? (best->taskbar ? "taskbar" : "window") : NULL;
}

static void snap_to_surface(App *app) {
    int bottom = app->pos_y + app->tile_size;
    for (int i = 0; i < app->object_count; i++) {
        const DesktopObject *object = &app->objects[i];
        if (abs(bottom - object->rect.y) <= 2 &&
            rects_overlap_x(app->pos_x, app->tile_size, object->rect.x,
                            object->rect.width)) {
            app->pos_y = object->rect.y - app->tile_size;
            return;
        }
    }
}

static gboolean start_window_climb(App *app, const EsheepAnimation *anim) {
    if (app->state.animation_id != ANIM_WALK) return FALSE;
    int dx = horizontal_delta(app, anim, pose_delta(app, anim, 0, TRUE));
    if (dx == 0) return FALSE;

    int next_x = app->pos_x + dx;
    int bottom = app->pos_y + app->tile_size;
    for (int i = 0; i < app->object_count; i++) {
        const DesktopObject *object = &app->objects[i];
        int object_bottom = object->rect.y + object->rect.height;
        if (bottom <= object->rect.y || app->pos_y >= object_bottom ||
            bottom == object->rect.y) continue;

        gboolean approaching_left = dx > 0 &&
            app->pos_x + app->tile_size <= object->rect.x &&
            next_x + app->tile_size >= object->rect.x;
        gboolean approaching_right = dx < 0 &&
            app->pos_x >= object->rect.x + object->rect.width &&
            next_x <= object->rect.x + object->rect.width;
        if (!approaching_left && !approaching_right) continue;

        int target_y = object->rect.y - app->tile_size;
        if (target_y >= app->pos_y) continue;

        app->pos_x = approaching_left ? object->rect.x
                                      : object->rect.x + object->rect.width -
                                        app->tile_size;
        app->climb_target_y = target_y;
        if (app->climb_target_y < app->bounds.y)
            app->climb_target_y = app->bounds.y;
        esheep_init(&app->state, 37);
        return TRUE;
    }
    return FALSE;
}

static int pose_value(const char *expression, int image_width, int image_height) {
    char *end = NULL;
    long integer = strtol(expression, &end, 10);
    if (end != expression && *end == '\0') return (int)integer;

    double factor;
    if (sscanf(expression, "-imageW*%lf", &factor) == 1)
        return (int)(-image_width * factor + 0.5);
    if (sscanf(expression, "imageW*%lf", &factor) == 1)
        return (int)(image_width * factor + 0.5);
    if (sscanf(expression, "-imageH*%lf", &factor) == 1)
        return (int)(-image_height * factor + 0.5);
    if (sscanf(expression, "imageH*%lf", &factor) == 1)
        return (int)(image_height * factor + 0.5);
    return 0;
}

static int pose_delta(const App *app, const EsheepAnimation *anim,
                      int frame_index, gboolean x_axis) {
    const char *start = x_axis ? anim->start.x : anim->start.y;
    const char *end = x_axis ? anim->end.x : anim->end.y;
    int start_value = pose_value(start, app->tile_size, app->tile_size);
    int end_value = pose_value(end, app->tile_size, app->tile_size);
    if (anim->frame_count <= 1)
        return x_axis ? horizontal_delta(app, anim, start_value) :
                        start_value;
    if (frame_index <= 0) {
        return x_axis ? horizontal_delta(app, anim, start_value) :
                        start_value;
    }

    double progress = (double)frame_index / (double)(anim->frame_count - 1);
    double previous_progress = (double)(frame_index - 1) /
                               (double)(anim->frame_count - 1);
    double current = start_value + (end_value - start_value) * progress;
    double previous = start_value +
                      (end_value - start_value) * previous_progress;
    double delta = current - previous;
    int result = (int)(delta + (delta >= 0.0 ? 0.5 : -0.5));
    return x_axis ? horizontal_delta(app, anim, result) : result;
}

static double pose_progress(const EsheepAnimation *anim, int frame_index) {
    if (anim->frame_count <= 1) return 0.0;
    if (frame_index < 0) frame_index = 0;
    if (frame_index >= anim->frame_count) frame_index = anim->frame_count - 1;
    return (double)frame_index / (double)(anim->frame_count - 1);
}

static int pose_offset_y(const EsheepAnimation *anim, int frame_index) {
    double progress = pose_progress(anim, frame_index);
    double start = (double)atoi(anim->start.offsety);
    double end = (double)atoi(anim->end.offsety);
    return (int)(start + (end - start) * progress +
                 (progress >= 0.5 ? 0.5 : -0.5));
}

static double pose_opacity(const EsheepAnimation *anim, int frame_index) {
    double progress = pose_progress(anim, frame_index);
    return anim->start.opacity +
           (anim->end.opacity - anim->start.opacity) * progress;
}

static gboolean is_airborne_animation(int animation_id) {
    return animation_id == 25 || animation_id == 44 || animation_id == 45;
}

static void set_sprite_input_region(App *app) {
    GdkWindow *window = gtk_widget_get_window(app->window);
    if (!window) return;

    int tile = esheep_current_tile(&app->state);
    const EsheepAnimation *anim = &esheep_animations[app->state.animation_id - 1];
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
                int run_width = x - run_start;
                int region_x = sprite_is_flipped(app, anim) ?
                               app->tile_size - x : run_start;
                cairo_rectangle_int_t rect = { region_x, y + pose_offset_y(anim,
                                                                            app->state.frame_index),
                                               run_width, 1 };
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

/* Apply the interpolated pose delta for one frame step of `anim`, then clamp
 * to the monitor bounds. Returns the surface context hit by the step. */
static const char *step_position(App *app, const EsheepAnimation *anim,
                                 int frame_index) {
    int old_y = app->pos_y;
    int dx = pose_delta(app, anim, frame_index, TRUE);
    int dy = pose_delta(app, anim, frame_index, FALSE);
    app->pos_x += dx;
    app->pos_y += dy;

    const char *context = "none";
    gboolean hit_floor = FALSE;
    if (dy > 0) {
        int old_bottom = old_y + app->tile_size;
        int new_bottom = app->pos_y + app->tile_size;
        const DesktopObject *best = NULL;
        for (int i = 0; i < app->object_count; i++) {
            const DesktopObject *object = &app->objects[i];
            if (old_bottom <= object->rect.y && new_bottom >= object->rect.y &&
                rects_overlap_x(app->pos_x, app->tile_size, object->rect.x,
                                object->rect.width) &&
                (!best || object->stack_order > best->stack_order))
                best = object;
        }
        if (best) {
            app->pos_y = best->rect.y - app->tile_size;
            context = best->taskbar ? "taskbar" : "window";
        }
    }

    int floor_y = app->bounds.y + app->bounds.height - app->tile_size;
    if (app->pos_y > floor_y) {
        app->pos_y = floor_y;
        hit_floor = dy > 0;
    }
    if (app->pos_y < app->bounds.y) app->pos_y = app->bounds.y;

    if (context[0] != 'n') return context;
    if (hit_floor) return "horizontal+";
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

    int offset_y = pose_offset_y(anim, app->state.frame_index);
    gboolean flipped = sprite_is_flipped(app, anim);
    cairo_translate(cr, flipped ? tile_size : 0, offset_y);
    cairo_scale(cr, flipped ? -1 : 1, 1);

    GdkPixbuf *subtile = gdk_pixbuf_new_subpixbuf(app->sheet, sx, sy, tile_size, tile_size);
    gdk_cairo_set_source_pixbuf(cr, subtile, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint_with_alpha(cr, pose_opacity(anim, app->state.frame_index));
    g_object_unref(subtile);
    cairo_restore(cr);
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
        esheep_tick(&app->state, (int)app->tick_ms, "none", roll);
        gtk_widget_queue_draw(app->window);
        set_sprite_input_region(app);
        return G_SOURCE_CONTINUE;
    }

    update_monitor_bounds(app);
    refresh_objects(app);
    int floor_y = app->bounds.y + app->bounds.height - app->tile_size;
    const char *surface = object_underfoot(app);
    if (surface) snap_to_surface(app);
    gboolean climbing = FALSE;
    if (!surface && app->state.animation_id == ANIM_WALK)
        climbing = start_window_climb(app, &esheep_animations[ANIM_WALK - 1]);
    if (!surface && !climbing && !is_airborne_animation(app->state.animation_id) &&
        app->pos_y < floor_y &&
        app->state.animation_id != ANIM_FALL) {
        esheep_gravity_event(&app->state, "none", rand() % 100);
        if (app->state.animation_id != ANIM_FALL)
            esheep_init(&app->state, ANIM_FALL);
    }
    /* Movement/collision context is decided by the CURRENT position, before
     * this tick's frame step -- e.g. if we're already pinned against the
     * right edge, this tick's context is "vertical" regardless of which
     * direction the current animation is trying to move. */
    const char *pretick_context = surface;
    if (!pretick_context &&
        (app->pos_x <= app->bounds.x ||
         app->pos_x + app->tile_size >= app->bounds.x + app->bounds.width))
        pretick_context = "vertical";
    if (!pretick_context) pretick_context = "none";

    int roll = rand() % 100;
    gboolean stepped = esheep_tick(&app->state, (int)app->tick_ms,
                                   pretick_context, roll);

    if (stepped) {
        const char *hit = "none";
        for (int event_index = 0; event_index < app->state.event_count;
             event_index++) {
            EsheepFrameEvent event = app->state.events[event_index];
            const EsheepAnimation *stepped_anim =
                &esheep_animations[event.animation_id - 1];
            hit = step_position(app, stepped_anim, event.frame_index);
            gboolean edge_animation_finished = FALSE;
            int floor_y = app->bounds.y + app->bounds.height - app->tile_size;
            if (event.animation_id == 37 && app->pos_y <= app->climb_target_y) {
                /* The top traversal must head away from the edge we climbed.
                 * Without this, a climb from the right edge enters top_walk2
                 * moving right and repeatedly hits the same boundary. */
                app->direction = app->pos_x <= app->bounds.x ? 1 : -1;
                esheep_init(&app->state, 38); /* vertical up -> top walk */
                edge_animation_finished = TRUE;
            } else if (event.animation_id == 39 &&
                       (app->pos_x <= app->bounds.x ||
                        app->pos_x + app->tile_size >=
                        app->bounds.x + app->bounds.width)) {
                esheep_init(&app->state, 41); /* top walk -> vertical down */
                edge_animation_finished = TRUE;
            } else if (event.animation_id == 41 && app->pos_y >= floor_y) {
                esheep_init(&app->state, 42); /* vertical down -> edge crossing */
                edge_animation_finished = TRUE;
            }
            gboolean border_changed = FALSE;
            if (!edge_animation_finished && hit[0] != 'n') {
                /* a screen edge, window, or taskbar */
                if (hit[0] == 'w' || hit[0] == 't') {
                    /* The source animation graph only has a "none"
                     * transition out of falling. A detected desktop object
                     * is a valid landing surface, so start walking directly. */
                    esheep_init(&app->state, ANIM_WALK);
                } else {
                    int border_roll = rand() % 100;
                    border_changed = esheep_border_event(&app->state, hit,
                                                         border_roll);
                    if (!border_changed && strcmp(hit, "horizontal+") == 0)
                        esheep_border_event(&app->state, "none", border_roll);
                }
            }
            if (strcmp(hit, "vertical") == 0 &&
                event.animation_id == ANIM_WALK && !edge_animation_finished &&
                !border_changed) {
                app->direction = -app->direction;
                esheep_init(&app->state, 2); /* turn before walking back */
            }
            if (edge_animation_finished || hit[0] != 'n') break;
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

    if (event->button == 1 && event->type == GDK_2BUTTON_PRESS && !app->dragging) {
        esheep_init(&app->state, 25); /* authored jump animation */
    } else if (event->button == 1) {
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
        update_monitor_bounds(app);
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

static void setup_sheep_window(App *app, GdkDisplay *display,
                               GdkMonitor *monitor) {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_POPUP);
    app->window = window;

    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen))
        gtk_widget_set_visual(window, visual);

    gtk_widget_set_app_paintable(window, TRUE);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_widget_set_double_buffered(window, FALSE);
    G_GNUC_END_IGNORE_DEPRECATIONS
    gtk_window_set_default_size(GTK_WINDOW(window), app->tile_size, app->tile_size);
    gtk_widget_set_size_request(window, app->tile_size, app->tile_size);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    gtk_window_stick(GTK_WINDOW(window));

    gdk_monitor_get_workarea(monitor, &app->bounds);
    esheep_set_environment(&app->state, app->bounds.width, app->bounds.height,
                           app->tile_size, app->tile_size);
    app->pos_x = app->bounds.x + app->bounds.width / 2;
    app->pos_y = app->bounds.y + app->bounds.height - app->tile_size;
    gtk_window_move(GTK_WINDOW(window), app->pos_x, app->pos_y);

    gtk_widget_add_events(window, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                       GDK_POINTER_MOTION_MASK);
    g_signal_connect(window, "draw", G_CALLBACK(on_draw), app);
    g_signal_connect(window, "button-press-event", G_CALLBACK(on_button_press), app);
    g_signal_connect(window, "button-release-event", G_CALLBACK(on_button_release), app);
    g_signal_connect(window, "motion-notify-event", G_CALLBACK(on_motion), app);
    gtk_widget_show_all(window);

    if (GDK_IS_X11_DISPLAY(display))
        app->xwindow = gdk_x11_window_get_xid(gtk_widget_get_window(window));
}

int main(int argc, char **argv) {
    const char *sprite_override = NULL;
    const char *character_override = NULL;
    const char *spawn_override = NULL;
    const char *config_override = NULL;
    guint tick_ms = env_uint("ESHEEP_TICK_MS", TICK_MS, 10, 1000);
    guint count = env_uint("ESHEEP_COUNT", 1, 1, MAX_SHEEP);
    gboolean window_landing = env_bool("ESHEEP_WINDOW_LANDING", TRUE);
    gboolean exclude_conky = env_bool("ESHEEP_EXCLUDE_CONKY", TRUE);
    gboolean x11_fallback = env_bool("ESHEEP_X11_FALLBACK", FALSE);
    gboolean tick_cli = FALSE;
    gboolean count_cli = FALSE;
    gboolean spawn_cli = FALSE;
    gboolean window_landing_cli = FALSE;
    gboolean exclude_conky_cli = FALSE;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            g_print("linux-esheep %s\n", ESHEEP_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--sprite") == 0 && i + 1 < argc) {
            sprite_override = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--character") == 0 && i + 1 < argc) {
            character_override = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--spawn") == 0 && i + 1 < argc) {
            spawn_override = argv[++i];
            spawn_cli = TRUE;
            continue;
        }
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            char *end = NULL;
            unsigned long parsed = strtoul(argv[++i], &end, 10);
            if (*end || parsed < 1 || parsed > MAX_SHEEP) {
                g_printerr("invalid --count value (use 1-%d)\n", MAX_SHEEP);
                return 2;
            }
            count = (guint)parsed;
            count_cli = TRUE;
            continue;
        }
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_override = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--no-window-landing") == 0) {
            window_landing = FALSE;
            window_landing_cli = TRUE;
            continue;
        }
        if (strcmp(argv[i], "--allow-conky") == 0) {
            exclude_conky = FALSE;
            exclude_conky_cli = TRUE;
            continue;
        }
        if (strcmp(argv[i], "--x11-fallback") == 0) {
            x11_fallback = TRUE;
            continue;
        }
        if (strcmp(argv[i], "--tick-ms") == 0 && i + 1 < argc) {
            char *end = NULL;
            unsigned long value = strtoul(argv[++i], &end, 10);
            if (*end || value < 10 || value > 1000) {
                g_printerr("invalid --tick-ms value\n");
                return 2;
            }
            tick_ms = (guint)value;
            tick_cli = TRUE;
            continue;
        }
        g_printerr("unknown or incomplete option: %s\n", argv[i]);
        print_usage(argv[0]);
        return 2;
    }

    if (x11_fallback && getenv("WAYLAND_DISPLAY") && getenv("DISPLAY") &&
        !getenv("GDK_BACKEND")) {
        g_setenv("GDK_BACKEND", "x11", FALSE);
    }
    gtk_init(&argc, &argv);
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    GKeyFile *config = g_key_file_new();
    gchar *default_config_path = NULL;
    gchar *config_character = NULL;
    gchar *config_sprite = NULL;
    gchar *config_spawn = NULL;
    if (!config_override) {
        default_config_path = g_build_filename(g_get_user_config_dir(),
                                                "esheep", "config", NULL);
        config_override = default_config_path;
    }
    if (g_key_file_load_from_file(config, config_override, G_KEY_FILE_NONE, NULL)) {
        if (!character_override && !getenv("ESHEEP_CHARACTER")) {
            config_character = g_key_file_get_string(config, "esheep",
                                                      "character", NULL);
            character_override = config_character;
        }
        if (!sprite_override && !getenv("ESHEEP_SPRITESHEET")) {
            config_sprite = g_key_file_get_string(config, "esheep",
                                                   "spritesheet", NULL);
            sprite_override = config_sprite;
        }
        if (!spawn_cli && !getenv("ESHEEP_SPAWN")) {
            config_spawn = g_key_file_get_string(config, "esheep", "spawn", NULL);
            spawn_override = config_spawn;
        }
        if (!tick_cli && !getenv("ESHEEP_TICK_MS") &&
            g_key_file_has_key(config, "esheep", "tick_ms", NULL)) {
            gint64 value = g_key_file_get_int64(config, "esheep", "tick_ms", NULL);
            if (value >= 10 && value <= 1000) tick_ms = (guint)value;
        }
        if (!count_cli && !getenv("ESHEEP_COUNT") &&
            g_key_file_has_key(config, "esheep", "count", NULL)) {
            gint64 value = g_key_file_get_int64(config, "esheep", "count", NULL);
            if (value >= 1 && value <= MAX_SHEEP) count = (guint)value;
        }
        if (!window_landing_cli && !getenv("ESHEEP_WINDOW_LANDING") &&
            g_key_file_has_key(config, "esheep", "window_landing", NULL))
            window_landing = g_key_file_get_boolean(config, "esheep",
                                                     "window_landing", NULL);
        if (!exclude_conky_cli && !getenv("ESHEEP_EXCLUDE_CONKY") &&
            g_key_file_has_key(config, "esheep", "exclude_conky", NULL))
            exclude_conky = g_key_file_get_boolean(config, "esheep",
                                                    "exclude_conky", NULL);
    }

    const char *character = character_override ? character_override :
                            getenv("ESHEEP_CHARACTER");
    if (character && strcasecmp(character, "sheep") != 0 &&
        strcasecmp(character, "penguin") != 0) {
        g_printerr("invalid character '%s' (use sheep or penguin)\n", character);
        return 2;
    }
    const char *sheet_path = sprite_override ? sprite_override :
                             getenv("ESHEEP_SPRITESHEET");
    char default_sheet_path[4096];
    if (!sheet_path) {
        snprintf(default_sheet_path, sizeof(default_sheet_path),
                 "%s/%s_spritesheet.png", ESHEEP_DATADIR,
                 character && strcasecmp(character, "penguin") == 0 ?
                 "penguin_ice_blue" : "sheep");
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
    App sheep[MAX_SHEEP] = {0};
    for (guint i = 0; i < count; i++) {
        App *app = &sheep[i];
        app->sheet = sheet;
        app->tile_size = tile_size;
        app->direction = rand() % 2 ? 1 : -1;
        app->tick_ms = tick_ms;
        app->window_landing = window_landing;
        app->exclude_conky = exclude_conky;
        app->spawn_on_window = spawn_override ?
                              strcasecmp(spawn_override, "window") == 0 :
                              env_equals("ESHEEP_SPAWN", "window");
        app->random_spawn = spawn_override ?
                           strcasecmp(spawn_override, "random") == 0 :
                           env_equals("ESHEEP_SPAWN", "random");
        app->siblings = sheep;
        app->sibling_count = (int)count;
        esheep_init(&app->state, ANIM_WALK);
        setup_sheep_window(app, display, monitor);
        if (!app->random_spawn && !app->spawn_on_window && count > 1) {
            int offset = (int)i * app->tile_size * 2;
            int max_x = app->bounds.x + app->bounds.width - app->tile_size;
            app->pos_x = app->bounds.x + app->bounds.width / 2 + offset;
            if (app->pos_x > max_x) app->pos_x = max_x;
            gtk_window_move(GTK_WINDOW(app->window), app->pos_x, app->pos_y);
        }
    }

    for (guint i = 0; i < count; i++) {
        App *app = &sheep[i];
        if (GDK_IS_X11_DISPLAY(display)) {
            refresh_objects(app);
            if (app->spawn_on_window) {
                for (int j = app->object_count - 1; j >= 0; j--) {
                    DesktopObject *object = &app->objects[j];
                    if (object->taskbar || object->rect.width < app->tile_size ||
                        object->rect.y - app->tile_size < app->bounds.y) continue;
                    app->pos_x = object->rect.x +
                                (object->rect.width - app->tile_size) / 2;
                    app->pos_y = object->rect.y - app->tile_size;
                    break;
                }
            } else if (app->random_spawn) {
                choose_random_spawn(app);
            }
            gtk_window_move(GTK_WINDOW(app->window), app->pos_x, app->pos_y);
        }
        set_sprite_input_region(app);
        g_timeout_add(app->tick_ms, on_tick, app);
    }

    const char *autoquit = getenv("ESHEEP_AUTOQUIT_MS");
    if (autoquit) {
        g_timeout_add((guint)atoi(autoquit), on_autoquit, NULL);
    }

    gtk_main();

    g_free(config_character);
    g_free(config_sprite);
    g_free(config_spawn);
    g_free(default_config_path);
    g_key_file_free(config);
    g_object_unref(sheet);
    return 0;
}
