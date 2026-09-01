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
#include "context.h"
#include "interpreter.h"
#include "renderer.h"

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

typedef enum {
    DESKTOP_BACKEND_X11 = 0,
    DESKTOP_BACKEND_X11_FALLBACK,
    DESKTOP_BACKEND_WAYLAND_UNSUPPORTED,
    DESKTOP_BACKEND_OTHER_UNSUPPORTED
} DesktopBackendMode;

typedef struct {
    DesktopBackendMode mode;
    gboolean should_force_x11_backend;
    gboolean can_position_globally;
    gboolean can_query_desktop_surfaces;
} DesktopBackendCapabilities;

typedef struct {
    gboolean desktop_surface;
    gboolean panel_surface;
    gboolean fullscreen_surface;
    gboolean conky_surface;
} DesktopSurfaceTraits;

typedef struct App App;

struct App {
    GtkWidget *window;
    EsheepRenderer scene; /* composited parent + children */
    GdkPixbuf *sheet;
    EsheepState state;
    int tile_size;
    GdkRectangle bounds; /* primary monitor geometry, the whole "world" for now */
    int pos_x, pos_y;    /* top-left of the sprite window, in screen coords */
    gboolean dragging;
    int drag_grab_x, drag_grab_y; /* pointer offset from window origin at grab time */
    Window xwindow;
    DesktopObject objects[MAX_OBJECTS];
    EsheepSurfaceObject surfaces[MAX_OBJECTS];
    int object_count;
    guint tick_ms;
    guint tick_source_id;
    gboolean window_landing;
    gboolean exclude_conky;
    gboolean spawn_on_window;
    gboolean random_spawn;
    int climb_target_y;
    int direction; /* -1 = left, +1 = right */
    int ordinal;
    bool edge_dispatched;
    gboolean cleaned_up;
    App *siblings;
    int sibling_count;
    int child_animation_id;  /* active child animation id, or 0 if none */
    int child_frame_index;   /* current frame within child animation */
    int child_elapsed_ms;    /* elapsed time for child animation frame */
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

static int eval_child_expression(const char *expr, int area_width, int area_height,
                                 int image_width, int image_height, int image_x, int image_y,
                                 int roll_0_99);
static int floor_pos_y(const App *app);

static int monitor_right(const GdkRectangle *monitor) {
    return monitor->x + monitor->width;
}

static int monitor_bottom(const GdkRectangle *monitor) {
    return monitor->y + monitor->height;
}

static int monitor_global_x(const GdkRectangle *monitor, int local_x) {
    return monitor->x + local_x;
}

static int monitor_global_y(const GdkRectangle *monitor, int local_y) {
    return monitor->y + local_y;
}

static gboolean monitor_contains_global_point(const GdkRectangle *monitor,
                                              int x, int y) {
    return x >= monitor->x && x < monitor_right(monitor) &&
           y >= monitor->y && y < monitor_bottom(monitor);
}

static gboolean rect_overlaps_monitor(const GdkRectangle *monitor,
                                      const GdkRectangle *rect) {
    return rect->x < monitor_right(monitor) &&
           rect->x + rect->width > monitor->x &&
           rect->y < monitor_bottom(monitor) &&
           rect->y + rect->height > monitor->y;
}

static int monitor_axis_gap(int point, int start, int end) {
    if (point < start) return start - point;
    if (point >= end) return point - (end - 1);
    return 0;
}

static gint64 monitor_distance_sq(const GdkRectangle *monitor, int x, int y) {
    gint64 dx = monitor_axis_gap(x, monitor->x, monitor_right(monitor));
    gint64 dy = monitor_axis_gap(y, monitor->y, monitor_bottom(monitor));
    return dx * dx + dy * dy;
}

static gboolean monitors_share_vertical_seam(const GdkRectangle *left,
                                             const GdkRectangle *right) {
    gboolean touching = monitor_right(left) == right->x ||
                        monitor_right(right) == left->x;
    return touching &&
           left->y < monitor_bottom(right) &&
           monitor_bottom(left) > right->y;
}

static gboolean monitors_overlap_vertically(const GdkRectangle *a,
                                            const GdkRectangle *b) {
    return a->y < monitor_bottom(b) && monitor_bottom(a) > b->y;
}

static gboolean seam_direction_matches(const GdkRectangle *current,
                                       const GdkRectangle *candidate,
                                       int direction) {
    if (!current || direction == 0 ||
        memcmp(current, candidate, sizeof(*current)) == 0 ||
        !monitors_overlap_vertically(current, candidate))
        return FALSE;
    if (direction > 0)
        return candidate->x >= current->x ||
               monitors_share_vertical_seam(current, candidate);
    return monitor_right(candidate) <= monitor_right(current) ||
           monitors_share_vertical_seam(current, candidate);
}

static int select_monitor_index(const GdkRectangle *monitors, int monitor_count,
                                const GdkRectangle *current, int x, int y,
                                int direction) {
    int best_index = -1;
    gint64 best_distance = G_MAXINT64;

    if (current && monitor_contains_global_point(current, x, y)) {
        for (int i = 0; i < monitor_count; i++) {
            if (memcmp(&monitors[i], current, sizeof(*current)) == 0)
                return i;
        }
    }

    for (int i = 0; i < monitor_count; i++) {
        if (monitor_contains_global_point(&monitors[i], x, y))
            return i;
    }

    for (int i = 0; i < monitor_count; i++) {
        gint64 distance = monitor_distance_sq(&monitors[i], x, y);
        if (best_index < 0 || distance < best_distance) {
            best_index = i;
            best_distance = distance;
            continue;
        }
        if (distance > best_distance) continue;

        if (current) {
            gboolean best_matches =
                seam_direction_matches(current, &monitors[best_index], direction);
            gboolean candidate_matches =
                seam_direction_matches(current, &monitors[i], direction);
            if (candidate_matches != best_matches) {
                if (candidate_matches) best_index = i;
                continue;
            }
            if (memcmp(&monitors[best_index], current, sizeof(*current)) != 0 &&
                memcmp(&monitors[i], current, sizeof(*current)) == 0) {
                best_index = i;
                continue;
            }
        }

        if (monitors[i].x < monitors[best_index].x ||
            (monitors[i].x == monitors[best_index].x &&
             monitors[i].y < monitors[best_index].y))
            best_index = i;
    }
    return best_index;
}

static gboolean select_monitor_workarea(GdkDisplay *display,
                                        const GdkRectangle *current,
                                        int x, int y, int direction,
                                        GdkRectangle *out) {
    int monitor_count = gdk_display_get_n_monitors(display);
    GdkRectangle monitors[32];

    if (!display || !out || monitor_count <= 0) return FALSE;
    if (monitor_count > (int)G_N_ELEMENTS(monitors))
        monitor_count = (int)G_N_ELEMENTS(monitors);

    for (int i = 0; i < monitor_count; i++) {
        GdkMonitor *monitor = gdk_display_get_monitor(display, i);
        if (!monitor) return FALSE;
        gdk_monitor_get_workarea(monitor, &monitors[i]);
    }

    int selected = select_monitor_index(monitors, monitor_count, current, x, y,
                                        direction);
    if (selected < 0) return FALSE;
    *out = monitors[selected];
    return TRUE;
}


/* Evaluate a child animation offset expression. Child positions are local
 * to the parent's composited surface (a single tile_size x tile_size
 * window), so the result is always returned in local coordinates -- the
 * monitor origin is intentionally NOT applied here, even for expressions
 * that look screen-relative (e.g. "areaH-imageH"). The "area" in those
 * expressions is the local composited surface area, not the full monitor. */
static int child_local_coordinate(const App *app, const char *expr,
                                  int roll_0_99) {
    /* image_x/image_y are local to the composited scene (parent tile
     * origin), not screen coordinates. Passing pos_x/pos_y here would
     * shift every child offset by the window's screen position and break
     * authored local placement such as the black sheep at -imageW-8
     * (i.e. -48 px from the parent) or the flower at -imageW*0.9. */
    return eval_child_expression(expr, app->bounds.width,
                               app->bounds.height, app->tile_size,
                               app->tile_size, 0, 0, roll_0_99);
}

static DesktopBackendCapabilities detect_backend_capabilities(
    gboolean x11_fallback_requested, const char *wayland_display,
    const char *x11_display, const char *gdk_backend, gboolean actual_x11) {
    DesktopBackendCapabilities caps = {
        .mode = DESKTOP_BACKEND_OTHER_UNSUPPORTED,
        .should_force_x11_backend = FALSE,
        .can_position_globally = FALSE,
        .can_query_desktop_surfaces = FALSE,
    };
    gboolean wayland_session = wayland_display && wayland_display[0];
    gboolean x11_available = x11_display && x11_display[0];
    gboolean backend_pinned = gdk_backend && gdk_backend[0];

    if (!actual_x11 && x11_fallback_requested && wayland_session &&
        x11_available && !backend_pinned) {
        caps.mode = DESKTOP_BACKEND_X11_FALLBACK;
        caps.should_force_x11_backend = TRUE;
        caps.can_position_globally = TRUE;
        caps.can_query_desktop_surfaces = TRUE;
        return caps;
    }

    if (actual_x11) {
        caps.mode = wayland_session ? DESKTOP_BACKEND_X11_FALLBACK :
                                      DESKTOP_BACKEND_X11;
        caps.can_position_globally = TRUE;
        caps.can_query_desktop_surfaces = TRUE;
        return caps;
    }

    if (wayland_session) {
        caps.mode = DESKTOP_BACKEND_WAYLAND_UNSUPPORTED;
        return caps;
    }

    return caps;
}

static const char *backend_mode_name(DesktopBackendMode mode) {
    switch (mode) {
    case DESKTOP_BACKEND_X11:
        return "x11";
    case DESKTOP_BACKEND_X11_FALLBACK:
        return "x11-fallback";
    case DESKTOP_BACKEND_WAYLAND_UNSUPPORTED:
        return "wayland-unsupported";
    case DESKTOP_BACKEND_OTHER_UNSUPPORTED:
    default:
        return "unsupported";
    }
}

static gboolean x11_surface_is_landing_candidate(
    const DesktopSurfaceTraits *traits) {
    return traits &&
           !traits->desktop_surface &&
           !traits->panel_surface &&
           !traits->fullscreen_surface &&
           !traits->conky_surface;
}

static guint clamp_sheep_count(guint requested) {
    if (requested < 1) return 1;
    if (requested > MAX_SHEEP) return MAX_SHEEP;
    return requested;
}

static int eval_spawn_expression(const char *expr, int area_width, int area_height,
                                  int image_width, int image_height, int roll_0_99) {
    if (!expr || !expr[0]) return 0;

    /* Simple integer literal */
    char *end = NULL;
    long value = strtol(expr, &end, 10);
    if (end != expr && *end == '\0') return (int)value;

    /* screenW (width/area_width) with optional offset */
    if (strcmp(expr, "screenW") == 0) return area_width;
    if (strcmp(expr, "screenW+10") == 0) return area_width + 10;

    /* areaH with offset */
    if (strcmp(expr, "areaH-imageH") == 0) return area_height - image_height;
    if (strcmp(expr, "areaH/2-imageH") == 0) return area_height / 2 - image_height;
    if (strcmp(expr, "areaH/2") == 0) return area_height / 2;

    /* -imageH, -imageH-N */
    if (strcmp(expr, "-imageH-20") == 0) return -image_height - 20;

    /* random*(screenW-imageW-50)/100+25 - spawn 2 y */
    if (strstr(expr, "random*(screenW-imageW-50)/100+25")) {
        int width = area_width - image_width - 50;
        if (width > 0)
            return (int)((roll_0_99 * width) / 100.0 + 0.5) + 25;
        return 25;
    }

    /* areaH/2-(randS*areaH/2)/120-imageH - spawn 3 y */
    if (strcmp(expr, "areaH/2-(randS*areaH/2)/120-imageH") == 0)
        return area_height / 2 - (roll_0_99 * area_height / 2) / 120 - image_height;

    return 0;
}

static int eval_child_expression(const char *expr, int area_width, int area_height,
                                 int image_width, int image_height, int image_x, int image_y,
                                 int roll_0_99) {
    if (!expr || !expr[0]) return 0;

    /* Simple integer literal */
    char *end = NULL;
    long value = strtol(expr, &end, 10);
    if (end != expr && *end == '\0') return (int)value;

    /* A negative image width is relative to the parent image. */
    if (strcmp(expr, "-imageW") == 0) return image_x - image_width;
    if (strcmp(expr, "-imageW-8") == 0) return image_x - image_width - 8;
    if (strcmp(expr, "imageY") == 0) return image_y;
    if (strcmp(expr, "imageX") == 0) return image_x;

    /* imageX - imageW*0.9 (flower child at 26) */
    if (strcmp(expr, "imageX-imageW*0.9") == 0) return image_x - (int)(image_width * 0.9);

    /* areaH - imageH */
    if (strcmp(expr, "areaH-imageH") == 0) return area_height - image_height;

    /* Complex spawn 21 child: screenW+10-areaH/2-(randS*areaH/2)/120 */
    if (strcmp(expr, "screenW+10-areaH/2-(randS*areaH/2)/120") == 0) {
        return area_width + 10 - area_height / 2 - (roll_0_99 * area_height / 2) / 120;
    }

    /* Fall back to spawn expression handler */
    return eval_spawn_expression(expr, area_width, area_height, image_width, image_height, roll_0_99);
}

static int select_spawn_animation(const EsheepSpawn *spawn) {
    if (!spawn || spawn->next_count == 0) return ANIM_WALK;
    int roll = rand() % 100;
    int accumulated = 0;
    for (int i = 0; i < spawn->next_count; i++) {
        accumulated += spawn->next[i].probability;
        if (roll < accumulated) return spawn->next[i].target;
    }
    return spawn->next[0].target;
}

static void choose_random_spawn(App *app) {
    /* Compute total spawn weight from generated data */
    int total_weight = 0;
    for (int i = 0; i < esheep_spawn_count; i++)
        total_weight += esheep_spawns[i].probability;

    int roll = rand() % (total_weight > 0 ? total_weight : 1);
    const EsheepSpawn *selected = NULL;
    int accumulated = 0;

    /* Select spawn by probability */
    for (int i = 0; i < esheep_spawn_count; i++) {
        accumulated += esheep_spawns[i].probability;
        if (roll < accumulated) {
            selected = &esheep_spawns[i];
            break;
        }
    }
    if (!selected && esheep_spawn_count > 0) selected = &esheep_spawns[0];
    if (!selected) {
        /* Fallback if no spawns defined */
        app->direction = -1;
        app->pos_x = monitor_global_x(&app->bounds, app->bounds.width + 10);
        app->pos_y = floor_pos_y(app);
        esheep_init(&app->state, ANIM_WALK);
        return;
    }

    /* Evaluate spawn position expressions */
    int spawn_roll = rand() % 100;
    app->pos_x = monitor_global_x(&app->bounds,
                                  eval_spawn_expression(selected->x,
                                                        app->bounds.width,
                                                        app->bounds.height,
                                                        app->tile_size,
                                                        app->tile_size,
                                                        spawn_roll));
    app->pos_y = monitor_global_y(&app->bounds,
                                  eval_spawn_expression(selected->y,
                                                        app->bounds.width,
                                                        app->bounds.height,
                                                        app->tile_size,
                                                        app->tile_size,
                                                        spawn_roll));

    /* Set direction and animation based on spawn position */
    app->direction = app->pos_x < app->bounds.x + app->bounds.width / 2 ? 1 : -1;
    int anim_id = select_spawn_animation(selected);
    esheep_init(&app->state, anim_id);
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
    g_print("  --walk-keep N          Keep walking probability (0-100, default 90).\n");
    g_print("  --review-animation N   Show animation N for transition review.\n");
    g_print("  --review-parent N      Show parent N with its authored child.\n");
}

static void update_monitor_bounds(App *app) {
    GdkDisplay *display = gtk_widget_get_display(app->window);
    int center_x = app->pos_x + app->tile_size / 2;
    int center_y = app->pos_y + app->tile_size / 2;
    GdkRectangle workarea;
    if (!select_monitor_workarea(display, &app->bounds, center_x, center_y,
                                 app->direction, &workarea))
        return;
    if (memcmp(&app->bounds, &workarea, sizeof(workarea)) != 0)
        app->bounds = workarea;
}

static gboolean rects_overlap_x(int left_a, int width_a, int left_b, int width_b) {
    return left_a < left_b + width_b && left_a + width_a > left_b;
}

static gboolean is_airborne_animation(int animation_id);
static gboolean child_tiles_use_parent_input(const App *app);

static gboolean rects_overlap(const GdkRectangle *a, const GdkRectangle *b) {
    return a->x < b->x + b->width && a->x + a->width > b->x &&
           a->y < b->y + b->height && a->y + a->height > b->y;
}

static GdkRectangle sheep_rect_at(const App *app, int pos_x, int pos_y) {
    return (GdkRectangle){ pos_x, pos_y, app->tile_size, app->tile_size };
}

static gboolean object_on_monitor(const App *app, const DesktopObject *object) {
    return rect_overlaps_monitor(&app->bounds, &object->rect);
}

static int clamp_pos_x(const App *app, int pos_x) {
    int right = monitor_right(&app->bounds) - app->tile_size;
    if (pos_x < app->bounds.x) return app->bounds.x;
    if (pos_x > right) return right;
    return pos_x;
}

static int floor_pos_y(const App *app) {
    return monitor_bottom(&app->bounds) - app->tile_size;
}

static int sync_surface_objects(App *app) {
    int surface_count = 0;
    for (int i = 0; i < app->object_count; i++) {
        const DesktopObject *src = &app->objects[i];
        if (!object_on_monitor(app, src)) continue;
        app->surfaces[surface_count].x = src->rect.x;
        app->surfaces[surface_count].y = src->rect.y;
        app->surfaces[surface_count].width = src->rect.width;
        app->surfaces[surface_count].height = src->rect.height;
        app->surfaces[surface_count].stack_order = src->stack_order;
        app->surfaces[surface_count].taskbar = src->taskbar;
        surface_count++;
    }
    return surface_count;
}

static gboolean spawn_hits_object(const App *app, int pos_x, int pos_y) {
    GdkRectangle rect = sheep_rect_at(app, pos_x, pos_y);
    for (int i = 0; i < app->object_count; i++) {
        const DesktopObject *object = &app->objects[i];
        if (!object_on_monitor(app, object)) continue;
        if (rects_overlap(&rect, &object->rect)) return TRUE;
    }
    return FALSE;
}

static gboolean spawn_hits_previous_sibling(const App *app, int pos_x, int pos_y) {
    GdkRectangle rect = sheep_rect_at(app, pos_x, pos_y);
    for (int i = 0; i < app->ordinal; i++) {
        const App *other = &app->siblings[i];
        GdkRectangle other_rect;
        if (other == app || other->cleaned_up || other->tile_size <= 0) continue;
        other_rect = sheep_rect_at(other, other->pos_x, other->pos_y);
        if (rects_overlap(&rect, &other_rect)) return TRUE;
    }
    return FALSE;
}

static void place_spawn_near(App *app, int base_x, int base_y,
                             gboolean clamp_to_bounds) {
    int step = MAX(app->tile_size + 8,
                   app->sibling_count > 0 ?
                   app->bounds.width / app->sibling_count : app->tile_size + 8);
    int candidate_y = base_y;
    int attempts = MAX(8, app->sibling_count * 3);

    if (clamp_to_bounds) {
        base_x = clamp_pos_x(app, base_x);
        if (candidate_y < app->bounds.y) candidate_y = app->bounds.y;
        if (candidate_y > floor_pos_y(app)) candidate_y = floor_pos_y(app);
    }

    for (int attempt = 0; attempt < attempts; attempt++) {
        int candidate_x = base_x;
        if (attempt > 0) {
            int distance = ((attempt + 1) / 2) * step;
            int direction = attempt % 2 ? 1 : -1;
            candidate_x = base_x + direction * distance;
        }
        if (clamp_to_bounds) candidate_x = clamp_pos_x(app, candidate_x);
        if (spawn_hits_object(app, candidate_x, candidate_y)) continue;
        if (spawn_hits_previous_sibling(app, candidate_x, candidate_y)) continue;
        app->pos_x = candidate_x;
        app->pos_y = candidate_y;
        return;
    }

    app->pos_x = clamp_to_bounds ? clamp_pos_x(app, base_x) : base_x;
    app->pos_y = candidate_y;
}

static int spawn_slot_center_x(const App *app) {
    int usable = app->bounds.width - app->tile_size;
    if (usable <= 0 || app->sibling_count <= 1)
        return app->bounds.x + usable / 2;

    double fraction = ((double)app->ordinal + 0.5) / (double)app->sibling_count;
    return app->bounds.x + (int)(fraction * usable + 0.5);
}

static const DesktopObject *select_spawn_window(const App *app) {
    int match_count = 0;
    for (int i = app->object_count - 1; i >= 0; i--) {
        const DesktopObject *object = &app->objects[i];
        if (object->taskbar || object->rect.width < app->tile_size ||
            object->rect.y - app->tile_size < app->bounds.y ||
            !object_on_monitor(app, object))
            continue;
        match_count++;
    }
    if (match_count == 0) return NULL;

    int selected_rank = app->ordinal % match_count;
    int rank = 0;
    for (int i = app->object_count - 1; i >= 0; i--) {
        const DesktopObject *object = &app->objects[i];
        if (object->taskbar || object->rect.width < app->tile_size ||
            object->rect.y - app->tile_size < app->bounds.y ||
            !object_on_monitor(app, object))
            continue;
        if (rank++ == selected_rank) return object;
    }
    return NULL;
}

static void configure_initial_spawn(App *app) {
    int floor_y = floor_pos_y(app);
    int base_x = spawn_slot_center_x(app);
    int base_y = floor_y;
    gboolean clamp_to_bounds = TRUE;

    if (app->spawn_on_window) {
        const DesktopObject *object = select_spawn_window(app);
        if (object) {
            base_x = object->rect.x + (object->rect.width - app->tile_size) / 2;
            base_y = object->rect.y - app->tile_size;
        }
    } else if (app->random_spawn) {
        choose_random_spawn(app);
        base_x = app->pos_x;
        base_y = app->pos_y;
        clamp_to_bounds = base_y >= app->bounds.y &&
                          base_x >= app->bounds.x &&
                          base_x <= app->bounds.x + app->bounds.width -
                                    app->tile_size;
    }

    place_spawn_near(app, base_x, base_y, clamp_to_bounds);
}

static gboolean apps_overlap(const App *a, const App *b) {
    GdkRectangle rect_a = sheep_rect_at(a, a->pos_x, a->pos_y);
    GdkRectangle rect_b = sheep_rect_at(b, b->pos_x, b->pos_y);
    return rects_overlap(&rect_a, &rect_b);
}

static void resolve_sheep_collisions(App *app) {
    if (app->dragging || app->sibling_count <= 1) return;

    for (int i = 0; i < app->sibling_count; i++) {
        App *other = &app->siblings[i];
        if (other == app || other->cleaned_up || other->tile_size <= 0) continue;
        if (!apps_overlap(app, other)) continue;

        GdkRectangle rect_a = sheep_rect_at(app, app->pos_x, app->pos_y);
        GdkRectangle rect_b = sheep_rect_at(other, other->pos_x, other->pos_y);
        int overlap_x = MIN(rect_a.x + rect_a.width, rect_b.x + rect_b.width) -
                        MAX(rect_a.x, rect_b.x);
        if (overlap_x <= 0) continue;

        int move_dir = -1;
        if (rect_a.x > rect_b.x) move_dir = 1;
        else if (rect_a.x == rect_b.x && app->ordinal > other->ordinal) move_dir = 1;

        app->pos_x = clamp_pos_x(app, app->pos_x + move_dir * (overlap_x + 1));
        if (apps_overlap(app, other)) {
            int right_of_other = clamp_pos_x(app, other->pos_x + app->tile_size + 1);
            int left_of_other = clamp_pos_x(app, other->pos_x - app->tile_size - 1);
            int right_gap = abs(right_of_other - app->pos_x);
            int left_gap = abs(left_of_other - app->pos_x);
            app->pos_x = left_gap <= right_gap ? left_of_other : right_of_other;
        }

        if (app->state.animation_id == ANIM_WALK) {
            app->direction = app->pos_x < other->pos_x ? -1 : 1;
            esheep_init(&app->state, 2);
        } else if (is_airborne_animation(app->state.animation_id)) {
            app->direction = app->pos_x < other->pos_x ? -1 : 1;
        }
    }
}

static gboolean child_tiles_use_parent_input(const App *app) {
    return app != NULL;
}

static void cleanup_app(App *app) {
    if (!app || app->cleaned_up) return;
    if (app->tick_source_id != 0 &&
        g_main_context_find_source_by_id(NULL, app->tick_source_id)) {
        g_source_remove(app->tick_source_id);
    }
    app->tick_source_id = 0;
    if (app->window) {
        gtk_widget_destroy(app->window);
        app->window = NULL;
    }
    app->xwindow = 0;
    app->object_count = 0;
    app->dragging = FALSE;
    app->edge_dispatched = FALSE;
    app->child_animation_id = 0;
    app->child_frame_index = 0;
    app->child_elapsed_ms = 0;
    esheep_renderer_init(&app->scene, app->tile_size, app->tile_size);
    app->cleaned_up = TRUE;
}

static gboolean has_horizontal_movement(const EsheepAnimation *anim) {
    /* Animations with authored horizontal movement should reverse based on
     * walk direction. Check if either start or end x pose is non-zero. */
    char *end = NULL;
    if (anim->start.x && anim->start.x[0]) {
        strtol(anim->start.x, &end, 10);
        if (end != anim->start.x) return TRUE;
    }
    end = NULL;
    if (anim->end.x && anim->end.x[0]) {
        strtol(anim->end.x, &end, 10);
        if (end != anim->end.x) return TRUE;
    }
    return FALSE;
}

static int horizontal_delta(const App *app, const EsheepAnimation *anim,
                            int delta) {
    if (!has_horizontal_movement(anim)) return delta;
    return app->direction < 0 ? delta : -delta;
}

static int pose_delta(const App *app, const EsheepAnimation *anim,
                      int frame_index, gboolean x_axis);

static void nudge_walk_inside_bounds(App *app) {
    app->pos_x += app->direction * 2;
    if (app->pos_x < app->bounds.x) app->pos_x = app->bounds.x;
    int right = monitor_right(&app->bounds) - app->tile_size;
    if (app->pos_x > right) app->pos_x = right;
}

static void keep_walk_inside_bounds(App *app) {
    gboolean at_left = app->pos_x <= app->bounds.x;
    gboolean at_right = app->pos_x + app->tile_size >=
                        monitor_right(&app->bounds);
    if (app->state.animation_id == 38 && at_right) {
        /* A climb from the right reaches the top while top_walk still has
         * its authored positive delta. Descend at this edge instead of
         * letting that pose push against the boundary. */
        esheep_init(&app->state, 41);
        return;
    }
    if (app->state.animation_id == 39 && (at_left || at_right)) {
        esheep_init(&app->state, 41);
        return;
    }
    if (app->state.animation_id != ANIM_WALK) return;
    const EsheepAnimation *walk = &esheep_animations[ANIM_WALK - 1];
    int dx = horizontal_delta(app, walk, pose_delta(app, walk, 0, TRUE));
    if ((at_left && dx < 0) || (at_right && dx > 0)) {
        app->direction = -app->direction;
        nudge_walk_inside_bounds(app);
        esheep_init(&app->state, 2);
    }
}

static gboolean sprite_is_flipped(const App *app, const EsheepAnimation *anim) {
    return anim->flip ||
           (has_horizontal_movement(anim) && app->direction > 0);
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

static gboolean window_atom_list_contains(Display *display, Window window,
                                          Atom property_atom, Atom wanted) {
    Atom actual_type;
    int format;
    unsigned long count, bytes_after;
    unsigned char *data = NULL;
    int result = XGetWindowProperty(display, window, property_atom, 0,
                                    MAX_OBJECTS, False, XA_ATOM, &actual_type,
                                    &format, &count, &bytes_after, &data);
    if (result != Success || !data || format != 32) {
        if (data) XFree(data);
        return FALSE;
    }

    gboolean found = FALSE;
    Atom *atoms = (Atom *)data;
    for (unsigned long i = 0; i < count; i++) {
        if (atoms[i] == wanted) {
            found = TRUE;
            break;
        }
    }
    XFree(data);
    return found;
}

static gboolean window_has_property(Display *display, Window window,
                                    Atom property_atom) {
    Atom actual_type;
    int format;
    unsigned long count, bytes_after;
    unsigned char *data = NULL;
    int result = XGetWindowProperty(display, window, property_atom, 0, 1,
                                    False, AnyPropertyType, &actual_type,
                                    &format, &count, &bytes_after, &data);
    if (data) XFree(data);
    return result == Success && actual_type != None && format != 0;
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

static DesktopSurfaceTraits inspect_x11_surface_traits(
    Display *display, Window window, Atom window_type, Atom dock_type,
    Atom desktop_type, Atom state_atom, Atom fullscreen_state, Atom strut_atom,
    Atom strut_partial_atom, gboolean exclude_conky) {
    DesktopSurfaceTraits traits = {0};

    traits.desktop_surface =
        window_has_type(display, window, window_type, desktop_type);
    traits.panel_surface =
        window_has_type(display, window, window_type, dock_type) ||
        window_has_property(display, window, strut_atom) ||
        window_has_property(display, window, strut_partial_atom);
    traits.fullscreen_surface =
        window_atom_list_contains(display, window, state_atom, fullscreen_state);
    traits.conky_surface =
        exclude_conky && is_conky_window(display, window);
    return traits;
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
    Atom state_atom = XInternAtom(display, "_NET_WM_STATE", False);
    Atom fullscreen_state = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
    Atom strut_atom = XInternAtom(display, "_NET_WM_STRUT", False);
    Atom strut_partial_atom = XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False);
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
        DesktopSurfaceTraits traits = inspect_x11_surface_traits(
            display, windows[i], window_type, dock_type, desktop_type,
            state_atom, fullscreen_state, strut_atom, strut_partial_atom,
            app->exclude_conky);
        if (!x11_surface_is_landing_candidate(&traits)) continue;
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

        DesktopObject *object = &app->objects[app->object_count++];
        object->rect = (GdkRectangle){ root_x, root_y, geometry.width,
                                       geometry.height };
        object->taskbar = FALSE;
        object->stack_order = stacking_order(stacking, stacking_count, windows[i]);
    }
    XFree(windows);
    if (stacking) XFree(stacking);
}

static void build_context(App *app, EsheepContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    int surface_count = sync_surface_objects(app);
    ctx->pos_x = app->pos_x;
    ctx->pos_y = app->pos_y;
    ctx->image_width = app->tile_size;
    ctx->image_height = app->tile_size;
    ctx->bounds_x = app->bounds.x;
    ctx->bounds_y = app->bounds.y;
    ctx->bounds_width = app->bounds.width;
    ctx->bounds_height = app->bounds.height;
    ctx->object_count = surface_count;
    ctx->objects = app->surfaces;
    ctx->window_landing_enabled = app->window_landing;
    ctx->landing_allowed = TRUE;
    if (app->state.animation_id == ANIM_WALK ||
        is_airborne_animation(app->state.animation_id)) {
        ctx->move = ESHEEP_MOVE_FALLING; /* treat airborne as falling for context */
    } else if (app->state.animation_id == 37) {
        ctx->move = ESHEEP_MOVE_CLIMBING;
    } else {
        ctx->move = ESHEEP_MOVE_WALKING;
    }
}

static const char *object_underfoot(const App *app) {
    int bottom = app->pos_y + app->tile_size;
    const DesktopObject *best = NULL;
    for (int i = 0; i < app->object_count; i++) {
        const DesktopObject *object = &app->objects[i];
        if (!object_on_monitor(app, object)) continue;
        if (abs(bottom - object->rect.y) <= 2 &&
            rects_overlap_x(app->pos_x, app->tile_size, object->rect.x,
                            object->rect.width) &&
            (!best || object->stack_order > best->stack_order))
            best = object;
    }
    return best ? (best->taskbar ? "taskbar" : "window") : NULL;
}

static gboolean start_window_climb(App *app, const EsheepAnimation *anim) {
    if (app->state.animation_id != ANIM_WALK) return FALSE;
    int dx = horizontal_delta(app, anim, pose_delta(app, anim, 0, TRUE));
    if (dx == 0) return FALSE;

    int next_x = app->pos_x + dx;
    int bottom = app->pos_y + app->tile_size;
    for (int i = 0; i < app->object_count; i++) {
        const DesktopObject *object = &app->objects[i];
        if (!object_on_monitor(app, object)) continue;
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

static gboolean is_airborne_animation(int animation_id) {
    switch (animation_id) {
    case 5: case 6: case 9: case 10:
    case 25: case 44: case 45: case 46:
    case 51: case 52: case 53: case 54:
        return TRUE;
    default:
        return FALSE;
    }
}

static gboolean is_landing_animation(int animation_id) {
    return is_airborne_animation(animation_id);
}

static void set_sprite_input_region(App *app) {
    if (!child_tiles_use_parent_input(app)) return;
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
    int dx = pose_delta(app, anim, frame_index, TRUE);
    int dy = pose_delta(app, anim, frame_index, FALSE);
    app->pos_x += dx;
    app->pos_y += dy;

    const char *context = "none";
    gboolean hit_floor = FALSE;
    if (dy > 0) {
        /* Use the platform-independent context helper to detect
         * landing on a window or taskbar during a fall. */
        EsheepContext ctx;
        int surface_count = sync_surface_objects(app);
        memset(&ctx, 0, sizeof(ctx));
        ctx.pos_x = app->pos_x;
        ctx.pos_y = app->pos_y;
        ctx.image_width = app->tile_size;
        ctx.image_height = app->tile_size;
        ctx.bounds_x = app->bounds.x;
        ctx.bounds_y = app->bounds.y;
        ctx.bounds_width = app->bounds.width;
        ctx.bounds_height = app->bounds.height;
        ctx.object_count = surface_count;
        ctx.objects = app->surfaces;
        ctx.window_landing_enabled = app->window_landing;
        ctx.move = ESHEEP_MOVE_FALLING;
        esheep_classify_context(&ctx);

        if (ctx.surface == ESHEEP_SURFACE_WINDOW ||
            ctx.surface == ESHEEP_SURFACE_TASKBAR) {
            app->pos_y = ctx.surface_y - app->tile_size;
            context = ctx.surface == ESHEEP_SURFACE_WINDOW ? "window" : "taskbar";
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

static const EsheepChild* find_child_for_animation(int parent_anim_id) {
    for (int i = 0; i < esheep_child_count; i++) {
        if (esheep_childs[i].animation_id == parent_anim_id) {
            return &esheep_childs[i];
        }
    }
    return NULL;
}

static void draw_scene_tile(cairo_t *cr, App *app, const EsheepRenderTile *tile) {
    cairo_save(cr);
    cairo_translate(cr, tile->x, tile->y);
    if (tile->flipped) {
        cairo_translate(cr, tile->width, 0);
        cairo_scale(cr, -1, 1);
    }
    int sx = (tile->tile_id % esheep_tiles_x) * tile->width;
    int sy = (tile->tile_id / esheep_tiles_x) * tile->height;
    GdkPixbuf *subtile = gdk_pixbuf_new_subpixbuf(app->sheet, sx, sy,
                                                  tile->width, tile->height);
    gdk_cairo_set_source_pixbuf(cr, subtile, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint_with_alpha(cr, tile->opacity);
    g_object_unref(subtile);
    cairo_restore(cr);
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)widget;
    App *app = user_data;
    /* CLEAR the whole frame once, then draw parent first and every visible
     * child on top so no stale child pixels or opaque backgrounds remain. */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    for (int i = 0; i < app->scene.count; i++) {
        const EsheepRenderTile *tile = &app->scene.tiles[i];
        if (!tile->visible) continue;
        draw_scene_tile(cr, app, tile);
    }
    return FALSE;
}

static int frame_interval(const EsheepAnimation *anim, int frame_index) {
    if (anim->frame_count <= 1) return anim->start.interval_ms;
    double progress = (double)frame_index / (double)(anim->frame_count - 1);
    double interval = anim->start.interval_ms +
                      (anim->end.interval_ms - anim->start.interval_ms) *
                      progress;
    return (int)(interval + 0.5);
}

static void update_child_animation(App *app) {
    /* Build the composed scene into app->scene. Parent tile 0 is drawn first;
     * the child slot uses the authored child record offset. No child window
     * is created -- the scene is rendered in the parent draw path. */
    const EsheepChild *child_record =
        find_child_for_animation(app->state.animation_id);
    int child_tile_ids[ESHEEP_RENDER_MAX_CHILDREN];
    int child_x[ESHEEP_RENDER_MAX_CHILDREN];
    int child_y[ESHEEP_RENDER_MAX_CHILDREN];
    int child_flipped[ESHEEP_RENDER_MAX_CHILDREN];
    double child_opacity[ESHEEP_RENDER_MAX_CHILDREN];
    bool child_visible[ESHEEP_RENDER_MAX_CHILDREN];
    int child_count = 0;

    if (child_record && child_record->next > 0) {
        int cid = child_record->next;
        if (cid >= 1 && cid <= esheep_animation_count) {
            const EsheepAnimation *canim = &esheep_animations[cid - 1];
            app->child_animation_id = cid;
            int frame = app->child_frame_index;
            if (frame < 0 || frame >= canim->frame_count) frame = 0;
            child_tile_ids[0] = canim->frames[frame];
            child_x[0] = child_local_coordinate(app, child_record->x, 0);
            child_y[0] = child_local_coordinate(app, child_record->y, 0);
            child_flipped[0] = sprite_is_flipped(app, &esheep_animations[app->state.animation_id - 1]);
            child_opacity[0] = 1.0;
            child_visible[0] = true;
            child_count = 1;
        }
    } else if (app->child_animation_id > 0) {
        app->child_animation_id = 0;
        app->child_frame_index = 0;
        app->child_elapsed_ms = 0;
    }

    esheep_renderer_compose(&app->scene,
        esheep_current_tile(&app->state),
        sprite_is_flipped(app, &esheep_animations[app->state.animation_id - 1]) ? 1 : 0, 1.0, true,
        child_count,
        child_tile_ids, child_x, child_y,
        child_flipped, child_opacity, child_visible);
}

static void advance_child_animation(App *app, int dt_ms) {
    if (app->child_animation_id <= 0 || app->child_animation_id > esheep_animation_count)
        return;

    const EsheepAnimation *child_anim = &esheep_animations[app->child_animation_id - 1];
    app->child_elapsed_ms += dt_ms;

    while (app->child_elapsed_ms > 0 && app->child_frame_index < child_anim->frame_count) {
        int interval = frame_interval(child_anim, app->child_frame_index);
        if (interval <= 0 || app->child_elapsed_ms < interval)
            break;

        app->child_elapsed_ms -= interval;
        app->child_frame_index++;
    }

    /* If child animation finished, reset (don't cycle). */
    if (app->child_frame_index >= child_anim->frame_count) {
        app->child_frame_index = 0;
        app->child_elapsed_ms = 0;
    }
}

static gboolean on_tick(gpointer user_data) {
    App *app = user_data;

    if (app->dragging) {
        /* Position is driven by the pointer while dragging; still let the
         * interpreter step so the drag animation's frames keep cycling. */
        int roll = rand() % 100;
        esheep_tick(&app->state, (int)app->tick_ms, "none", roll);
        update_child_animation(app);
        advance_child_animation(app, (int)app->tick_ms);
        gtk_widget_queue_draw(app->window);
        set_sprite_input_region(app);
        return G_SOURCE_CONTINUE;
    }

    update_monitor_bounds(app);
    refresh_objects(app);

    /* Build context and classify surfaces using the platform-independent helper */
    EsheepContext ctx;
    build_context(app, &ctx);
    esheep_classify_context(&ctx);

    /* Apply landing: if falling and found a surface, land on it */
    int floor_y = app->bounds.y + app->bounds.height - app->tile_size;
    if (ctx.move == ESHEEP_MOVE_FALLING && ctx.surface != ESHEEP_SURFACE_FLOOR &&
        ctx.surface != ESHEEP_SURFACE_LEFT_EDGE && ctx.surface != ESHEEP_SURFACE_RIGHT_EDGE) {
        app->pos_y = ctx.surface_y - app->tile_size;
    } else if (ctx.move == ESHEEP_MOVE_FALLING && ctx.surface == ESHEEP_SURFACE_FLOOR) {
        app->pos_y = floor_y;
    }

    gboolean climbing = FALSE;
    if (!climbing && app->state.animation_id == ANIM_WALK)
        climbing = start_window_climb(app, &esheep_animations[ANIM_WALK - 1]);

    /* Handle edge transitions: only trigger once per edge encounter */
    if (!climbing) {
        if (ctx.surface == ESHEEP_SURFACE_LEFT_EDGE ||
            ctx.surface == ESHEEP_SURFACE_RIGHT_EDGE) {
            int next_dir = app->direction;
            if (esheep_authored_edge_reversal(&ctx, &next_dir, &app->edge_dispatched)) {
                app->direction = next_dir;
                /* Nudge inside bounds to avoid repeated edge hits */
                if (ctx.surface == ESHEEP_SURFACE_LEFT_EDGE && app->pos_x < app->bounds.x)
                    app->pos_x = app->bounds.x;
                if (ctx.surface == ESHEEP_SURFACE_RIGHT_EDGE) {
                    int right = app->bounds.x + app->bounds.width - app->tile_size;
                    if (app->pos_x > right) app->pos_x = right;
                }
                /* Transition to edge turn animation (animation 2) */
                esheep_init(&app->state, 2);
            }
        }
    }

    if (!climbing && ctx.move != ESHEEP_MOVE_FALLING &&
        !is_airborne_animation(app->state.animation_id) &&
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
    const char *pretick_context = "none";
    if (ctx.surface == ESHEEP_SURFACE_LEFT_EDGE || ctx.surface == ESHEEP_SURFACE_RIGHT_EDGE)
        pretick_context = "vertical";
    else if (ctx.surface == ESHEEP_SURFACE_WINDOW || ctx.surface == ESHEEP_SURFACE_TASKBAR)
        pretick_context = ctx.surface == ESHEEP_SURFACE_WINDOW ? "window" : "taskbar";

    /* Reset edge dispatch flag when no longer at the edge so the next edge
     * encounter can trigger a fresh reversal. */
    if (ctx.surface != ESHEEP_SURFACE_LEFT_EDGE &&
        ctx.surface != ESHEEP_SURFACE_RIGHT_EDGE) {
        app->edge_dispatched = FALSE;
    }

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
                    /* Landing should start walking after a fall. A walking
                     * or window behavior frame also reports its supporting
                     * surface, so do not reset it on every frame. */
                    if (is_landing_animation(event.animation_id))
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
                nudge_walk_inside_bounds(app);
                esheep_init(&app->state, 2); /* turn before walking back */
            }
            if (edge_animation_finished || hit[0] != 'n') break;
        }
    }

    resolve_sheep_collisions(app);

    /* A completed turn can leave the sprite on the same boundary for one
     * more tick. Make the next walk direction agree with the actual pose
     * delta so it cannot repeatedly turn into the same edge. */
    keep_walk_inside_bounds(app);

    /* Update child animation state. */
    update_child_animation(app);
    advance_child_animation(app, (int)app->tick_ms);

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
    esheep_renderer_init(&app->scene, app->tile_size, app->tile_size);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    gtk_window_stick(GTK_WINDOW(window));

    if (monitor) gdk_monitor_get_workarea(monitor, &app->bounds);
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
    guint walk_keep_probability = env_uint("ESHEEP_WALK_KEEP_PROBABILITY",
                                           90, 0, 100);
    gboolean window_landing = env_bool("ESHEEP_WINDOW_LANDING", TRUE);
    gboolean exclude_conky = env_bool("ESHEEP_EXCLUDE_CONKY", TRUE);
    gboolean x11_fallback = env_bool("ESHEEP_X11_FALLBACK", FALSE);
    gboolean tick_cli = FALSE;
    gboolean count_cli = FALSE;
    gboolean spawn_cli = FALSE;
    gboolean window_landing_cli = FALSE;
    gboolean exclude_conky_cli = FALSE;
    gboolean walk_keep_cli = FALSE;
    int review_animation = 0;
    int review_parent = 0;
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
        if (strcmp(argv[i], "--walk-keep") == 0 && i + 1 < argc) {
            char *end = NULL;
            unsigned long value = strtoul(argv[++i], &end, 10);
            if (*end || value > 100) {
                g_printerr("invalid --walk-keep value (use 0-100)\n");
                return 2;
            }
            walk_keep_probability = (guint)value;
            walk_keep_cli = TRUE;
            continue;
        }
        if (strcmp(argv[i], "--review-animation") == 0 && i + 1 < argc) {
            char *end = NULL;
            long value = strtol(argv[++i], &end, 10);
            if (*end || value < 1 || value > esheep_animation_count) {
                g_printerr("invalid --review-animation value (use 1-%d)\n",
                           esheep_animation_count);
                return 2;
            }
            review_animation = (int)value;
            continue;
        }
        if (strcmp(argv[i], "--review-parent") == 0 && i + 1 < argc) {
            char *end = NULL;
            long value = strtol(argv[++i], &end, 10);
            if (*end || value < 1 || value > esheep_animation_count) {
                g_printerr("invalid --review-parent value (use 1-%d)\n",
                           esheep_animation_count);
                return 2;
            }
            review_parent = (int)value;
            continue;
        }
        g_printerr("unknown or incomplete option: %s\n", argv[i]);
        print_usage(argv[0]);
        return 2;
    }

    DesktopBackendCapabilities requested_backend = detect_backend_capabilities(
        x11_fallback, getenv("WAYLAND_DISPLAY"), getenv("DISPLAY"),
        getenv("GDK_BACKEND"), FALSE);
    if (requested_backend.should_force_x11_backend) {
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
        if (!walk_keep_cli && !getenv("ESHEEP_WALK_KEEP_PROBABILITY") &&
            g_key_file_has_key(config, "esheep", "walk_keep_probability", NULL)) {
            gint64 value = g_key_file_get_int64(config, "esheep",
                                                "walk_keep_probability", NULL);
            if (value >= 0 && value <= 100) walk_keep_probability = (guint)value;
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
    count = clamp_sheep_count(count);

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
    DesktopBackendCapabilities runtime_backend = detect_backend_capabilities(
        x11_fallback, getenv("WAYLAND_DISPLAY"), getenv("DISPLAY"),
        getenv("GDK_BACKEND"), GDK_IS_X11_DISPLAY(display));
    if (!runtime_backend.can_position_globally) {
        g_printerr("unsupported desktop backend '%s'; native Wayland window "
                   "placement is not implemented. Use --x11-fallback with "
                   "XWayland.\n", backend_mode_name(runtime_backend.mode));
        g_free(config_character);
        g_free(config_sprite);
        g_free(config_spawn);
        g_free(default_config_path);
        g_key_file_free(config);
        g_object_unref(sheet);
        return 2;
    }
    if (!runtime_backend.can_query_desktop_surfaces) {
        window_landing = FALSE;
        if (spawn_override && strcasecmp(spawn_override, "window") == 0)
            spawn_override = "bottom";
    }

    /* Spawn on whichever monitor the pointer is actually on, not GDK's
     * notion of "primary" -- on a multi-monitor setup those can easily
     * differ, and a sheep spawning on a monitor you're not looking at
     * just looks like the app did nothing. */
    GdkRectangle initial_bounds = {0};
    gboolean have_initial_bounds = FALSE;
    GdkSeat *seat = gdk_display_get_default_seat(display);
    if (seat) {
        GdkDevice *pointer = gdk_seat_get_pointer(seat);
        if (pointer) {
            GdkScreen *pointer_screen;
            int px, py;
            gdk_device_get_position(pointer, &pointer_screen, &px, &py);
            (void)pointer_screen;
            have_initial_bounds = select_monitor_workarea(display, NULL, px, py,
                                                          0, &initial_bounds);
        }
    }
    if (!have_initial_bounds) {
        GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
        if (!monitor) monitor = gdk_display_get_monitor(display, 0);
        if (!monitor) {
            g_printerr("failed to locate a monitor workarea\n");
            g_free(config_character);
            g_free(config_sprite);
            g_free(config_spawn);
            g_free(default_config_path);
            g_key_file_free(config);
            g_object_unref(sheet);
            return 1;
        }
        gdk_monitor_get_workarea(monitor, &initial_bounds);
        have_initial_bounds = TRUE;
    }
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
        app->ordinal = (int)i;
        app->siblings = sheep;
        app->sibling_count = (int)count;
        app->bounds = initial_bounds;
        esheep_init(&app->state, ANIM_WALK);
        setup_sheep_window(app, display,
                           gdk_display_get_monitor_at_point(display,
                                                            initial_bounds.x + initial_bounds.width / 2,
                                                            initial_bounds.y + initial_bounds.height / 2));
        esheep_set_walk_keep_probability(&app->state,
                                         (int)walk_keep_probability);
        if (review_parent > 0)
            esheep_init(&app->state, review_parent);
        else if (review_animation > 0)
            esheep_init(&app->state, review_animation);
    }

    for (guint i = 0; i < count; i++) {
        App *app = &sheep[i];
        if (GDK_IS_X11_DISPLAY(display)) {
            refresh_objects(app);
        }
        configure_initial_spawn(app);
        gtk_window_move(GTK_WINDOW(app->window), app->pos_x, app->pos_y);
        set_sprite_input_region(app);
        app->tick_source_id = g_timeout_add(app->tick_ms, on_tick, app);
    }

    const char *autoquit = getenv("ESHEEP_AUTOQUIT_MS");
    if (autoquit) {
        g_timeout_add((guint)atoi(autoquit), on_autoquit, NULL);
    }

    gtk_main();

    for (guint i = 0; i < count; i++)
        cleanup_app(&sheep[i]);

    g_free(config_character);
    g_free(config_sprite);
    g_free(config_spawn);
    g_free(default_config_path);
    g_key_file_free(config);
    g_object_unref(sheet);
    return 0;
}
