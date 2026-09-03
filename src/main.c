/* Transparent GTK window rendering the interpreter's current
 * sprite frame, with real x/y movement driven by each animation's pose
 * deltas and collision against X11 client windows, panels, and the monitor
 * workarea.
 */
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include "actor.h"
#include "context.h"
#include "interpreter.h"
#include "pet_package.h"
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
#define MAX_RUNTIME_CHILDREN (ESHEEP_RENDER_MAX_CHILDREN - 1)

typedef struct {
    GdkRectangle rect;
    gboolean taskbar;
    gboolean fullscreen;
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

static int x11_bad_window;
static XErrorHandler x11_previous_error_handler;
static uint32_t app_random_seed = 0xC0FFEE01u;

static int x11_refresh_error_handler(Display *display, XErrorEvent *error) {
    (void)display;
    if (error->error_code == BadWindow) {
        x11_bad_window = TRUE;
        return 0;
    }
    return x11_previous_error_handler ?
           x11_previous_error_handler(display, error) : 0;
}

static void x11_refresh_sync(Display *display) {
    XSync(display, False);
}

struct App {
    GtkWidget *window;
    EsheepRenderer scene; /* composited parent + children */
    GdkPixbuf *sheet;
    union {
        EsheepState state; /* compatibility view for the interpreter */
        EsheepActor actor; /* authoritative root ownership and child links */
    };
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
    gboolean drop_landing_enabled;
    int climb_target_y;
    int direction; /* -1 = left, +1 = right */
    int ordinal;
    uint32_t random_state;
    bool edge_dispatched;
    gboolean cleaned_up;
    gboolean paused;
    gboolean hidden;
    gboolean fullscreen_suppressed;
    App *siblings;
    int sibling_count;
    int child_animation_id;  /* active child animation id, or 0 if none */
    int child_frame_index;   /* current frame within child animation */
    int child_elapsed_ms;    /* elapsed time for child animation frame */
    int child_animation_ids[MAX_RUNTIME_CHILDREN];
    int child_authored_animations[MAX_RUNTIME_CHILDREN];
    int child_parent_animations[MAX_RUNTIME_CHILDREN];
    int child_frame_indices[MAX_RUNTIME_CHILDREN];
    int child_elapsed_ms_values[MAX_RUNTIME_CHILDREN];
    int child_pose_x[MAX_RUNTIME_CHILDREN];
    int child_pose_y[MAX_RUNTIME_CHILDREN];
    int child_render_x[MAX_RUNTIME_CHILDREN];
    int child_render_y[MAX_RUNTIME_CHILDREN];
    EsheepActor child_actors[MAX_RUNTIME_CHILDREN];
    int scene_origin_x;
    int scene_origin_y;
};

typedef struct {
    App *sheep;
    guint count;
    GdkDisplay *display;
    guint monitor_index;
    guint configured_count;
    char character[64];
    char spritesheet[4096];
    char package[4096];
    GKeyFile *config;
    const char *config_path;
    guint tick_ms;
    guint walk_keep_probability;
    gboolean window_landing;
    gboolean exclude_conky;
    int review_animation;
    char spawn_mode[16];
} SheepGroup;

static int clamp_pos_x(const App *app, int pos_x);
static int floor_pos_y(const App *app);

/* Each sheep owns its random stream. This prevents child animation draws or
 * a sibling's decisions from changing another sheep's behavior sequence. */
static int app_random_0_99(App *app) {
    if (!app) return 0;
    if (app->random_state == 0)
        app->random_state = app_random_seed ^ 0x9E3779B9u ^
                            (uint32_t)(app->ordinal + 1) * 0xA511E9B3u;
    app->random_state = app->random_state * 1664525u + 1013904223u;
    return (int)((app->random_state >> 8) % 100u);
}

static int actor_random_source(void *context) {
    return app_random_0_99((App *)context);
}

static gboolean on_tick(gpointer user_data);
static void update_child_animation(App *app);
static void advance_child_animation(App *app, int elapsed_ms);
static void sync_scene_window(App *app);
static void set_sprite_input_region(App *app);

static void group_set_paused(SheepGroup *group, gboolean paused) {
    if (!group || !group->sheep) return;
    for (guint i = 0; i < group->count; i++)
        group->sheep[i].paused = paused;
}

static void group_set_hidden(SheepGroup *group, gboolean hidden) {
    if (!group || !group->sheep) return;
    for (guint i = 0; i < group->count; i++) {
        group->sheep[i].hidden = hidden;
        if (hidden)
            gtk_widget_hide(group->sheep[i].window);
        else
            gtk_widget_show(group->sheep[i].window);
    }
}

static void group_present(SheepGroup *group) {
    if (!group || !group->sheep) return;
    group_set_hidden(group, FALSE);
    for (guint i = 0; i < group->count; i++)
        gtk_window_present(GTK_WINDOW(group->sheep[i].window));
}

static void group_set_tick_ms(SheepGroup *group, guint tick_ms) {
    if (!group || !group->sheep || tick_ms < 10 || tick_ms > 1000) return;
    for (guint i = 0; i < group->count; i++) {
        App *app = &group->sheep[i];
        if (app->tick_source_id != 0)
            g_source_remove(app->tick_source_id);
        app->tick_ms = tick_ms;
        app->tick_source_id = g_timeout_add(app->tick_ms, on_tick, app);
    }
    group->tick_ms = tick_ms;
}

static void group_set_walk_keep_probability(SheepGroup *group, guint probability) {
    if (!group || probability > 100) return;
    for (guint i = 0; i < group->count; i++)
        esheep_set_walk_keep_probability(&group->sheep[i].state,
                                         (int)probability);
    group->walk_keep_probability = probability;
}

static void group_set_window_landing(SheepGroup *group, gboolean enabled) {
    if (!group) return;
    for (guint i = 0; i < group->count; i++)
        group->sheep[i].window_landing = enabled;
    group->window_landing = enabled;
}

static void group_set_exclude_conky(SheepGroup *group, gboolean excluded) {
    if (!group) return;
    for (guint i = 0; i < group->count; i++)
        group->sheep[i].exclude_conky = excluded;
    group->exclude_conky = excluded;
}

static gboolean group_set_monitor(SheepGroup *group, guint monitor_index) {
    if (!group || !group->sheep || !group->display ||
        monitor_index >= (guint)gdk_display_get_n_monitors(group->display))
        return FALSE;
    GdkMonitor *monitor = gdk_display_get_monitor(group->display,
                                                   (gint)monitor_index);
    if (!monitor) return FALSE;
    GdkRectangle bounds;
    gdk_monitor_get_workarea(monitor, &bounds);

    for (guint i = 0; i < group->count; i++) {
        App *app = &group->sheep[i];
        int old_floor = floor_pos_y(app);
        gboolean grounded = app->pos_y >= old_floor;
        int old_usable = app->bounds.width - app->tile_size;
        int new_usable = bounds.width - app->tile_size;
        int relative_x = app->pos_x - app->bounds.x;
        if (old_usable > 0 && new_usable > 0)
            relative_x = (int)((gint64)relative_x * new_usable / old_usable);
        app->bounds = bounds;
        app->pos_x = clamp_pos_x(app, bounds.x + relative_x);
        app->pos_y = grounded ? floor_pos_y(app) :
                     CLAMP(app->pos_y, bounds.y, floor_pos_y(app));
        if (app->window) {
            gtk_window_move(GTK_WINDOW(app->window),
                            app->pos_x - app->scene_origin_x,
                            app->pos_y - app->scene_origin_y);
            set_sprite_input_region(app);
        }
    }
    group->monitor_index = monitor_index;
    return TRUE;
}

static gboolean group_set_review_animation(SheepGroup *group, int animation_id) {
    if (!group || !group->sheep || animation_id < 0 ||
        animation_id > esheep_animation_count) return FALSE;
    if (animation_id == 0) animation_id = ANIM_WALK;
    for (guint i = 0; i < group->count; i++) {
        App *app = &group->sheep[i];
        esheep_init(&app->state, animation_id);
        esheep_set_environment(&app->state, app->bounds.width,
                               app->bounds.height, app->tile_size,
                               app->tile_size);
        esheep_set_walk_keep_probability(&app->state,
                                         (int)group->walk_keep_probability);
        update_child_animation(app);
        advance_child_animation(app, 0);
        if (app->window) {
            sync_scene_window(app);
            gtk_widget_queue_draw(app->window);
            set_sprite_input_region(app);
        }
    }
    group->review_animation = animation_id == ANIM_WALK ? 0 : animation_id;
    return TRUE;
}

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

static gboolean fullscreen_covers_monitor(const GdkRectangle *monitor,
                                          const GdkRectangle *surface) {
    return surface && surface->x <= monitor->x && surface->y <= monitor->y &&
           surface->x + surface->width >= monitor_right(monitor) &&
           surface->y + surface->height >= monitor_bottom(monitor);
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
        if (current && direction != 0) {
            gboolean past_right = direction > 0 && x >= monitor_right(current);
            gboolean past_left = direction < 0 && x < current->x;
            if ((past_right || past_left) &&
                !(monitors_share_vertical_seam(current, &monitors[i]) &&
                  y >= MAX(current->y, monitors[i].y) &&
                  y < MIN(monitor_bottom(current), monitor_bottom(&monitors[i]))))
                continue;
        }
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


/* Retained as a small compatibility helper for the transition-review tests;
 * runtime composition now passes the parent origin explicitly. */
static int child_local_coordinate(const App *app, const char *expr,
                                  int roll_0_99) __attribute__((unused));
static int child_local_coordinate(const App *app, const char *expr,
                                  int roll_0_99) {
    return eval_child_expression(expr, app->bounds.width, app->bounds.height,
                                 app->tile_size, app->tile_size, 0, 0,
                                 roll_0_99);
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

    /* Custom packages may express child placement as a scale of the parent
     * image dimensions. Keep this evaluator aligned with the package
     * validator and with pose_value(), rather than silently treating those
     * valid expressions as zero. */
    double factor;
    if (sscanf(expr, "-imageW*%lf", &factor) == 1)
        return (int)(-image_width * factor - 0.5);
    if (sscanf(expr, "imageW*%lf", &factor) == 1)
        return (int)(image_width * factor + 0.5);
    if (sscanf(expr, "-imageH*%lf", &factor) == 1)
        return (int)(-image_height * factor - 0.5);
    if (sscanf(expr, "imageH*%lf", &factor) == 1)
        return (int)(image_height * factor + 0.5);

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

static int select_spawn_animation(App *app, const EsheepSpawn *spawn) {
    if (!spawn || spawn->next_count == 0) return ANIM_WALK;
    int roll = app_random_0_99(app);
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

    int roll = app_random_0_99(app) % (total_weight > 0 ? total_weight : 1);
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
    int spawn_roll = app_random_0_99(app);
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
    int anim_id = select_spawn_animation(app, selected);
    esheep_init(&app->state, anim_id);
}

static void print_usage(const char *program) {
    g_print("Usage: %s [options]\n\n", program);
    g_print("Options:\n");
    g_print("  --help                 Show this help.\n");
    g_print("  --version              Show the version.\n");
    g_print("  --sprite PATH          Use a spritesheet.\n");
    g_print("  --character NAME       Use sheep or penguin sprites.\n");
    g_print("  --package PATH         Load a validated XML behavior package.\n");
    g_print("  --config PATH          Load settings from an INI config file.\n");
    g_print("  --spawn MODE           Use bottom, window, or random spawn.\n");
    g_print("  --count N              Spawn N sheep (1-32).\n");
    g_print("  --monitor N            Start on monitor N (zero-based).\n");
    g_print("  --no-window-landing    Disable window and panel landing.\n");
    g_print("  --allow-conky          Allow landing on Conky.\n");
    g_print("  --x11-fallback         Use XWayland when available.\n");
    g_print("  --tick-ms N            Set the update interval (10-1000).\n");
    g_print("  --walk-keep N          Keep walking probability (0-100, default 90).\n");
    g_print("  --seed N               Set the reproducible random seed.\n");
    g_print("  --review-animation N   Show animation N for transition review.\n");
    g_print("  --review-parent N      Show parent N with its authored child.\n");
    g_print("  --list-animations      List active animation IDs and names.\n");
    g_print("  --list-transitions     List active authored transitions.\n");
}

static void print_transitions(void) {
    int index = 1;

    for (int animation_index = 0; animation_index < esheep_animation_count;
         animation_index++) {
        const EsheepAnimation *animation = &esheep_animations[animation_index];
        const EsheepTransition *lists[] = {
            animation->sequence_next, animation->border_next,
            animation->gravity_next
        };
        const int counts[] = {
            animation->sequence_next_count, animation->border_next_count,
            animation->gravity_next_count
        };
        const char *kinds[] = { "sequence", "border", "gravity" };
        for (int kind = 0; kind < 3; kind++) {
            for (int transition_index = 0;
                 transition_index < counts[kind]; transition_index++) {
                const EsheepTransition *transition =
                    &lists[kind][transition_index];
                g_print("%d\t%u\t%d\t%s\t%s\t%d\t%d\n", index++,
                        transition->stable_id, animation->id, kinds[kind],
                        transition->only ? transition->only : "any",
                        transition->probability, transition->target);
            }
        }
    }
    for (int child_index = 0; child_index < esheep_child_count; child_index++) {
        const EsheepChild *child = &esheep_childs[child_index];
        uint32_t stable_id = ((uint32_t)(child->animation_id & 0xff) << 24) |
                             (4u << 20) |
                             ((uint32_t)(child_index & 0xff) << 12) |
                             (uint32_t)(child->next & 0xfff);
        g_print("%d\t%u\t%d\tchild\tany\t100\t%d\n", index++, stable_id,
                child->animation_id, child->next);
    }
}

static void update_monitor_bounds(App *app) {
    GdkDisplay *display = gtk_widget_get_display(app->window);
    /* Probe just beyond the leading edge. Probing the center leaves a sheep
     * trapped at a monitor boundary because clamping keeps its center inside
     * the old workarea. */
    int center_x = app->direction > 0 ? app->pos_x + app->tile_size + 1 :
                   app->direction < 0 ? app->pos_x - 1 :
                   app->pos_x + app->tile_size / 2;
    int center_y = app->pos_y + app->tile_size / 2;
    GdkRectangle workarea;
    if (!select_monitor_workarea(display, &app->bounds, center_x, center_y,
                                 app->direction, &workarea))
        return;
    if (memcmp(&app->bounds, &workarea, sizeof(workarea)) != 0) {
        gboolean moving_right = workarea.x > app->bounds.x;
        app->bounds = workarea;
        app->pos_x = moving_right ? app->bounds.x :
                     app->bounds.x + app->bounds.width - app->tile_size;
        if (app->pos_y < app->bounds.y) app->pos_y = app->bounds.y;
        if (app->pos_y > floor_pos_y(app)) app->pos_y = floor_pos_y(app);
    }
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
    /* A grounded sheep can support another falling sheep. Child props are
     * intentionally excluded because they are visual-only and do not own a
     * desktop surface. Give root sheep a deterministic layer above ordinary
     * client windows for tie-breaking; the window manager still controls
     * their actual visual occlusion. */
    for (int i = 0; i < app->sibling_count && surface_count < MAX_OBJECTS; i++) {
        const App *other = &app->siblings[i];
        if (other == app || other->cleaned_up || other->dragging ||
            other->tile_size <= 0 ||
            is_airborne_animation(other->state.animation_id)) continue;
        app->surfaces[surface_count].x = other->pos_x;
        app->surfaces[surface_count].y = other->pos_y;
        app->surfaces[surface_count].width = other->tile_size;
        app->surfaces[surface_count].height = other->tile_size;
        app->surfaces[surface_count].stack_order = 100000 +
                                                    (app->sibling_count - i);
        app->surfaces[surface_count].taskbar = FALSE;
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

        /* There may be no horizontal separation when both actors are
         * clamped at the same monitor edge.  Keep the unresolved actor from
         * repeatedly dispatching the outward turn; the next walk step must
         * move inward and give the other actor room to separate. */
        if (apps_overlap(app, other)) {
            int right = app->bounds.x + app->bounds.width - app->tile_size;
            if (app->pos_x <= app->bounds.x)
                app->direction = 1;
            else if (app->pos_x >= right)
                app->direction = -1;
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
    memset(app->child_animation_ids, 0, sizeof(app->child_animation_ids));
    memset(app->child_authored_animations, 0,
           sizeof(app->child_authored_animations));
    memset(app->child_parent_animations, 0,
           sizeof(app->child_parent_animations));
    memset(app->child_frame_indices, 0, sizeof(app->child_frame_indices));
    memset(app->child_elapsed_ms_values, 0,
           sizeof(app->child_elapsed_ms_values));
    memset(app->child_pose_x, 0, sizeof(app->child_pose_x));
    memset(app->child_pose_y, 0, sizeof(app->child_pose_y));
    memset(app->child_render_x, 0, sizeof(app->child_render_x));
    memset(app->child_render_y, 0, sizeof(app->child_render_y));
    esheep_actor_detach(&app->actor);
    for (int i = 0; i < MAX_RUNTIME_CHILDREN; i++)
        esheep_actor_detach(&app->child_actors[i]);
    memset(app->child_actors, 0, sizeof(app->child_actors));
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
    x11_refresh_sync(display);
    if (x11_bad_window || result != Success || !data || count == 0 || format != 32) {
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
    x11_refresh_sync(display);
    if (x11_bad_window || result != Success || !data || format != 32) {
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
    x11_refresh_sync(display);
    if (data) XFree(data);
    return !x11_bad_window && result == Success && actual_type != None && format != 0;
}

static gboolean is_conky_window(Display *display, Window window) {
    XClassHint class_hint = {0};
    gboolean is_conky = FALSE;
    gboolean queried = XGetClassHint(display, window, &class_hint);
    x11_refresh_sync(display);
    if (queried && !x11_bad_window) {
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
    if (x11_bad_window) return traits;
    traits.panel_surface =
        window_has_type(display, window, window_type, dock_type) ||
        window_has_property(display, window, strut_atom) ||
        window_has_property(display, window, strut_partial_atom);
    if (x11_bad_window) return traits;
    traits.fullscreen_surface =
        window_atom_list_contains(display, window, state_atom, fullscreen_state);
    if (x11_bad_window) return traits;
    traits.conky_surface =
        exclude_conky && is_conky_window(display, window);
    return traits;
}

static int stacking_order(const Window *stacking, unsigned long count, Window window) {
    for (unsigned long i = 0; i < count; i++)
        if (stacking[i] == window) return (int)i;
    return -1;
}

/* A normal toplevel participates in WM stacking, but moving it does not
 * change its relative position.  If a client is raised over a sheep that is
 * standing on a different (possibly covered) client, explicitly put the
 * sheep immediately below the highest overlapping client.  This preserves
 * the useful "pet on a window" case while making foreground occlusion real.
 * The request is issued only while the refresh error handler is installed. */
static void restack_below_occluding_window(App *app, Display *display,
                                           const Window *windows,
                                           unsigned long window_count,
                                           const Window *stacking,
                                           unsigned long stacking_count) {
    if (!app || !display || !windows || !stacking || !app->xwindow ||
        window_count == 0 || stacking_count == 0) return;

    GdkRectangle sheep = { app->pos_x, app->pos_y,
                           app->tile_size, app->tile_size };
    int best_order = -1;
    Window occluding = None;
    for (unsigned long i = 0; i < window_count; i++) {
        Window candidate = windows[i];
        if (!candidate || candidate == app->xwindow) continue;
        gboolean own_window = FALSE;
        for (int sibling = 0; sibling < app->sibling_count; sibling++) {
            if (candidate == app->siblings[sibling].xwindow) {
                own_window = TRUE;
                break;
            }
        }
        if (own_window) continue;

        XWindowAttributes attributes;
        x11_bad_window = FALSE;
        if (!XGetWindowAttributes(display, candidate, &attributes)) {
            x11_refresh_sync(display);
            continue;
        }
        Window root, parent, *children = NULL;
        unsigned int child_count = 0;
        gboolean tree_ok = XQueryTree(display, candidate, &root, &parent,
                                      &children, &child_count);
        x11_refresh_sync(display);
        if (children) XFree(children);
        if (x11_bad_window || !tree_ok || attributes.map_state != IsViewable)
            continue;

        Window geometry_window = parent != DefaultRootWindow(display) ? parent : candidate;
        XWindowAttributes geometry;
        if (!XGetWindowAttributes(display, geometry_window, &geometry)) {
            x11_refresh_sync(display);
            continue;
        }
        int x, y;
        Window child;
        gboolean translated = XTranslateCoordinates(display, geometry_window,
                                                     DefaultRootWindow(display),
                                                     0, 0, &x, &y, &child);
        x11_refresh_sync(display);
        if (x11_bad_window || !translated) continue;
        GdkRectangle candidate_rect = { x, y, geometry.width, geometry.height };
        if (!rects_overlap(&sheep, &candidate_rect)) continue;

        int order = stacking_order(stacking, stacking_count, candidate);
        if (order > best_order) {
            best_order = order;
            /* Reparented clients are not siblings of the sheep; the WM frame
             * is.  For an un-reparented client geometry_window is the client
             * itself and remains the correct sibling. */
            occluding = geometry_window;
        }
    }
    if (!occluding) return;

    XWindowChanges changes = {0};
    changes.sibling = occluding;
    changes.stack_mode = Below;
    x11_bad_window = FALSE;
    XConfigureWindow(display, app->xwindow, CWSibling | CWStackMode, &changes);
    x11_refresh_sync(display);
}

static void refresh_objects(App *app) {
    if (!app->window_landing) {
        app->object_count = 0;
        app->fullscreen_suppressed = FALSE;
        return;
    }
    GdkDisplay *gdk_display = gtk_widget_get_display(app->window);
    if (!GDK_IS_X11_DISPLAY(gdk_display)) return;

    Display *display = gdk_x11_display_get_xdisplay(gdk_display);
    Window root = DefaultRootWindow(display);
    DesktopObject refreshed_objects[MAX_OBJECTS];
    int refreshed_count = 0;
    gboolean refreshed_fullscreen_suppressed = FALSE;
    /* Xlib reports invalid or disappearing client windows asynchronously.
     * Install the scoped handler before even querying the root properties so
     * a stale client-list entry cannot escape into GTK's fatal handler. */
    x11_bad_window = FALSE;
    x11_previous_error_handler = XSetErrorHandler(x11_refresh_error_handler);
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
    x11_refresh_sync(display);
    if (x11_bad_window || result != Success || !windows || format != 32 ||
        actual_type != XA_WINDOW) {
        if (windows) XFree(windows);
        XSetErrorHandler(x11_previous_error_handler);
        return;
    }

    x11_bad_window = FALSE;
    unsigned long stacking_count = 0;
    result = XGetWindowProperty(display, root, client_list_stacking, 0,
                                MAX_OBJECTS, False, XA_WINDOW, &actual_type,
                                &format, &stacking_count, &bytes_after,
                                (unsigned char **)&stacking);
    x11_refresh_sync(display);
    if (x11_bad_window) {
        if (stacking) XFree(stacking);
        XFree(windows);
        XSetErrorHandler(x11_previous_error_handler);
        return;
    }
    if (result != Success || !stacking || format != 32 ||
        actual_type != XA_WINDOW) {
        if (stacking) XFree(stacking);
        stacking = NULL;
        stacking_count = 0;
    }

    for (unsigned long i = 0; i < count && refreshed_count < MAX_OBJECTS; i++) {
        x11_bad_window = FALSE;
        gboolean own_window = windows[i] == app->xwindow;
        for (int sibling = 0; !own_window && sibling < app->sibling_count;
             sibling++)
            own_window = windows[i] == app->siblings[sibling].xwindow;
        if (own_window) continue;
        DesktopSurfaceTraits traits = inspect_x11_surface_traits(
            display, windows[i], window_type, dock_type, desktop_type,
            state_atom, fullscreen_state, strut_atom, strut_partial_atom,
            app->exclude_conky);
        if (x11_bad_window) continue;
        if (traits.fullscreen_surface) {
            XWindowAttributes fullscreen_attributes;
            if (XGetWindowAttributes(display, windows[i], &fullscreen_attributes)) {
                int fullscreen_x, fullscreen_y;
                Window fullscreen_child;
                if (XTranslateCoordinates(display, windows[i], root, 0, 0,
                                           &fullscreen_x, &fullscreen_y,
                                           &fullscreen_child)) {
                    GdkRectangle fullscreen_rect = {
                        fullscreen_x, fullscreen_y,
                        fullscreen_attributes.width, fullscreen_attributes.height
                    };
                    if (fullscreen_covers_monitor(&app->bounds,
                                                  &fullscreen_rect))
                        refreshed_fullscreen_suppressed = TRUE;
                }
            }
            x11_refresh_sync(display);
            continue;
        }
        if (!x11_surface_is_landing_candidate(&traits)) continue;
        XWindowAttributes attributes;
        gboolean attributes_ok = XGetWindowAttributes(display, windows[i], &attributes);
        x11_refresh_sync(display);
        if (x11_bad_window || !attributes_ok ||
            attributes.map_state != IsViewable || attributes.class != InputOutput ||
            attributes.width <= 1 || attributes.height <= 1) continue;

        /* _NET_CLIENT_LIST contains client windows.  Their origin starts
         * below the window-manager title bar, so use the parent frame for
         * collision geometry when one exists. */
        Window tree_root, parent, *children = NULL;
        unsigned int child_count = 0;
        Window geometry_window = windows[i];
        gboolean tree_ok = XQueryTree(display, windows[i], &tree_root, &parent,
                                      &children, &child_count);
        x11_refresh_sync(display);
        if (x11_bad_window) {
            if (children) XFree(children);
            continue;
        }
        if (tree_ok) {
            if (parent != root) geometry_window = parent;
            if (children) XFree(children);
        }

        XWindowAttributes geometry;
        gboolean geometry_ok = XGetWindowAttributes(display, geometry_window,
                                                    &geometry);
        x11_refresh_sync(display);
        if (x11_bad_window || !geometry_ok ||
            geometry.width <= 1 || geometry.height <= 1) continue;
        int root_x, root_y;
        Window child;
        gboolean translated = XTranslateCoordinates(display, geometry_window,
                                                     root, 0, 0, &root_x,
                                                     &root_y, &child);
        x11_refresh_sync(display);
        if (x11_bad_window || !translated) continue;

        DesktopObject *object = &refreshed_objects[refreshed_count++];
        object->rect = (GdkRectangle){ root_x, root_y, geometry.width,
                                       geometry.height };
        object->taskbar = FALSE;
        object->stack_order = stacking_order(stacking, stacking_count, windows[i]);
    }
    restack_below_occluding_window(app, display, windows, count, stacking,
                                   stacking_count);
    x11_refresh_sync(display);
    XSetErrorHandler(x11_previous_error_handler);
    XFree(windows);
    if (stacking) XFree(stacking);
    memcpy(app->objects, refreshed_objects,
           (size_t)refreshed_count * sizeof(refreshed_objects[0]));
    app->object_count = refreshed_count;
    app->fullscreen_suppressed = refreshed_fullscreen_suppressed;
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
    ctx->drop_landing_enabled = app->drop_landing_enabled;
    if (is_airborne_animation(app->state.animation_id)) {
        ctx->move = ESHEEP_MOVE_FALLING; /* treat airborne as falling for context */
    } else if (app->state.animation_id == 37) {
        ctx->move = ESHEEP_MOVE_CLIMBING;
    } else {
        ctx->move = ESHEEP_MOVE_WALKING;
    }
}

static const char *object_underfoot(const App *app) {
    int bottom = app->pos_y + app->tile_size;
    const EsheepSurfaceObject *best = NULL;
    App *mutable_app = (App *)app;
    int surface_count = sync_surface_objects(mutable_app);
    for (int i = 0; i < surface_count; i++) {
        const EsheepSurfaceObject *surface = &app->surfaces[i];
        if (abs(bottom - surface->y) <= 2 &&
            rects_overlap_x(app->pos_x, app->tile_size, surface->x,
                            surface->width) &&
            (!best || surface->stack_order > best->stack_order))
            best = surface;
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
        return (int)(-image_width * factor - 0.5);
    if (sscanf(expression, "imageW*%lf", &factor) == 1)
        return (int)(image_width * factor + 0.5);
    if (sscanf(expression, "-imageH*%lf", &factor) == 1)
        return (int)(-image_height * factor - 0.5);
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

/* Custom-pet package boundary: validate that a loaded pixbuf matches the
 * tile grid esheep_animations[] indexes into, and emit a clear diagnostic
 * when a non-built-in character is selected. The runtime behaviour graph is
 * hardcoded to the sheep/penguin 16x11 grid; custom sprites must follow the
 * same grid, and we refuse to silently keep a wrong-size image. */
static gboolean G_GNUC_UNUSED validate_spritesheet_pixbuf(const GdkPixbuf *sheet,
                                           const char *sheet_path,
                                           const char *character) {
    int w = gdk_pixbuf_get_width(sheet);
    int h = gdk_pixbuf_get_height(sheet);
    if (!gdk_pixbuf_get_has_alpha(sheet) ||
        gdk_pixbuf_get_n_channels(sheet) < 4) {
        g_printerr("spritesheet '%s' has no alpha channel; transparent "
                   "RGBA sprites are required to avoid a rectangular "
                   "desktop surface.\n", sheet_path);
        return FALSE;
    }
    int ts_x = w / esheep_tiles_x;
    int ts_y = h / esheep_tiles_y;
    if (ts_x < 8 || ts_y < 8 || ts_x != ts_y ||
        w != esheep_tiles_x * ts_x || h != esheep_tiles_y * ts_y) {
        g_printerr("spritesheet '%s' has dimensions %dx%d, which is not a "
                   "%d-column x %d-row grid of square tiles. Built-in "
                   "characters sheep and penguin use a %dx%d grid (640x440 "
                   "or 1280x880).\n",
                   sheet_path, w, h, esheep_tiles_x, esheep_tiles_y,
                   esheep_tiles_x * 40, esheep_tiles_y * 40);
        return FALSE;
    }
    if (character && strcasecmp(character, "sheep") != 0 &&
        strcasecmp(character, "penguin") != 0) {
        g_printerr("custom-pet package note: custom character '%s' accepted, "
                   "but runtime animation behavior is fixed to the sheep/"
                   "penguin graph (src/animations_data.c). Custom sprites must "
                   "use the %dx%d tile grid and cover all referenced frames "
                   "(see tests/test_spritesheet.py for coverage rules).\n",
                   character, esheep_tiles_x, esheep_tiles_y);
    }
    return TRUE;
}

static void set_sprite_input_region(App *app) {
    if (!child_tiles_use_parent_input(app)) return;
    GdkWindow *window = gtk_widget_get_window(app->window);
    if (!window) return;

    int rowstride = gdk_pixbuf_get_rowstride(app->sheet);
    int channels = gdk_pixbuf_get_n_channels(app->sheet);
    const guchar *pixels = gdk_pixbuf_get_pixels(app->sheet);
    cairo_region_t *region = cairo_region_create();

    for (int scene_index = 0; scene_index < app->scene.count; scene_index++) {
        const EsheepRenderTile *scene_tile = &app->scene.tiles[scene_index];
        if (!scene_tile->visible || scene_tile->opacity <= 0.0 ||
            scene_tile->tile_id < 0) continue;
        int tile = scene_tile->tile_id;
        int tile_sx = (tile % esheep_tiles_x) * app->tile_size;
        int tile_sy = (tile / esheep_tiles_x) * app->tile_size;
        for (int y = 0; y < app->tile_size; y++) {
            int run_start = -1;
            for (int x = 0; x <= app->tile_size; x++) {
                gboolean opaque = FALSE;
                if (x < app->tile_size) {
                    const guchar *pixel = pixels + (tile_sy + y) * rowstride +
                                           (tile_sx + x) * channels;
                    opaque = channels < 4 || pixel[3] > 16;
                }
                if (opaque && run_start < 0) run_start = x;
                if (!opaque && run_start >= 0) {
                    int run_width = x - run_start;
                    int region_x = scene_tile->flipped ?
                                   app->tile_size - x : run_start;
                    cairo_rectangle_int_t rect = {
                        app->scene_origin_x + scene_tile->x + region_x,
                        app->scene_origin_y + scene_tile->y + y,
                        run_width, 1
                    };
                    cairo_region_union_rectangle(region, &rect);
                    run_start = -1;
                }
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
    int surface_count = sync_surface_objects(app);
    EsheepMotion motion = {
        .pos_x = app->pos_x,
        .pos_y = app->pos_y,
        .delta_x = pose_delta(app, anim, frame_index, TRUE),
        .delta_y = pose_delta(app, anim, frame_index, FALSE),
        .image_width = app->tile_size,
        .image_height = app->tile_size,
        .bounds_x = app->bounds.x,
        .bounds_y = app->bounds.y,
        .bounds_width = app->bounds.width,
        .bounds_height = app->bounds.height,
        .object_count = surface_count,
        .objects = app->surfaces,
        .window_landing_enabled = app->window_landing,
        .drop_landing_enabled = app->drop_landing_enabled,
    };
    const char *context = esheep_apply_motion(&motion);
    app->pos_x = motion.pos_x;
    app->pos_y = motion.pos_y;
    app->drop_landing_enabled = motion.drop_landing_enabled;
    return context;
}

static const EsheepChild *find_child_for_animation(int parent_anim_id)
    __attribute__((unused));
static const EsheepChild *find_child_for_animation(int parent_anim_id) {
    for (int i = 0; i < esheep_child_count; i++) {
        if (esheep_childs[i].animation_id == parent_anim_id)
            return &esheep_childs[i];
    }
    return NULL;
}

static void draw_scene_tile(cairo_t *cr, App *app, const EsheepRenderTile *tile) {
    cairo_save(cr);
    cairo_translate(cr, app->scene_origin_x + tile->x,
                    app->scene_origin_y + tile->y);
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

/* Keep the parent tile's screen position stable while exposing the full
 * composed scene, including children with negative local offsets. */
static void sync_scene_window(App *app) {
    if (!app || !app->window || !gtk_widget_get_realized(app->window)) return;
    int min_x, min_y, max_x, max_y;
    if (!esheep_renderer_alpha_bounds(&app->scene, 0.5,
                                      &min_x, &min_y, &max_x, &max_y)) {
        min_x = 0;
        min_y = 0;
        max_x = app->tile_size - 1;
        max_y = app->tile_size - 1;
    }
    int width = MAX(app->tile_size, max_x - min_x + 1);
    int height = MAX(app->tile_size, max_y - min_y + 1);
    app->scene_origin_x = -min_x;
    app->scene_origin_y = -min_y;

    GtkAllocation allocation;
    gtk_widget_get_allocation(app->window, &allocation);
    if (allocation.width != width || allocation.height != height)
        gtk_window_resize(GTK_WINDOW(app->window), width, height);
}

static int frame_interval(const EsheepAnimation *anim, int frame_index)
    __attribute__((unused));
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
     * every authored child record gets an independent renderer slot. */
    int child_tile_ids[ESHEEP_RENDER_MAX_CHILDREN];
    int child_x[ESHEEP_RENDER_MAX_CHILDREN];
    int child_y[ESHEEP_RENDER_MAX_CHILDREN];
    int child_flipped[ESHEEP_RENDER_MAX_CHILDREN];
    double child_opacity[ESHEEP_RENDER_MAX_CHILDREN];
    bool child_visible[ESHEEP_RENDER_MAX_CHILDREN];
    int child_count = 0;
    int previous_child_authored[MAX_RUNTIME_CHILDREN];
    int previous_child_parents[MAX_RUNTIME_CHILDREN];
    memcpy(previous_child_authored, app->child_authored_animations,
           sizeof(previous_child_authored));
    memcpy(previous_child_parents, app->child_parent_animations,
           sizeof(previous_child_parents));
    gboolean legacy_child_matches = FALSE;
    if (app->child_animation_id == 0 &&
        (app->child_frame_index != 0 || app->child_elapsed_ms != 0)) {
        for (int i = 0; i < esheep_child_count; i++) {
            if (esheep_childs[i].animation_id == app->state.animation_id) {
                app->child_animation_id = esheep_childs[i].next;
                break;
            }
        }
    }
    for (int i = 0; i < esheep_child_count; i++) {
        if (esheep_childs[i].animation_id == app->state.animation_id &&
            esheep_childs[i].next == app->child_animation_id) {
            legacy_child_matches = TRUE;
            break;
        }
    }
    if (legacy_child_matches && app->child_animation_ids[0] == 0) {
        app->child_animation_ids[0] = app->child_animation_id;
        app->child_authored_animations[0] = app->child_animation_id;
        app->child_frame_indices[0] = app->child_frame_index;
        app->child_elapsed_ms_values[0] = app->child_elapsed_ms;
    } else if (!legacy_child_matches && app->child_animation_ids[0] == 0) {
        memset(app->child_frame_indices, 0, sizeof(app->child_frame_indices));
        memset(app->child_elapsed_ms_values, 0,
               sizeof(app->child_elapsed_ms_values));
    }
    memset(app->child_animation_ids, 0, sizeof(app->child_animation_ids));
    /* Process the root's records first, then each active child as a parent.
     * Package validation bounds the graph depth; the renderer bounds the
     * number of visible descendants. */
    for (int parent_slot = -1;
         parent_slot < child_count && child_count < MAX_RUNTIME_CHILDREN;
         parent_slot++) {
        int parent_animation = parent_slot < 0 ? app->state.animation_id :
                               app->child_actors[parent_slot].state.animation_id;
        int parent_x = parent_slot < 0 ? 0 : app->child_render_x[parent_slot];
        int parent_y = parent_slot < 0 ? 0 : app->child_render_y[parent_slot];
        for (int i = 0; i < esheep_child_count &&
                           child_count < MAX_RUNTIME_CHILDREN; i++) {
            const EsheepChild *record = &esheep_childs[i];
            if (record->animation_id != parent_animation ||
                record->next < 1 || record->next > esheep_animation_count)
                continue;
            int slot = child_count++;
        if (previous_child_authored[slot] != record->next ||
            previous_child_parents[slot] != parent_animation) {
            esheep_actor_detach(&app->child_actors[slot]);
            esheep_actor_init(&app->child_actors[slot], NULL, record->next,
                              0, 0, app->direction);
            esheep_actor_set_random_source(&app->child_actors[slot],
                                           actor_random_source, app);
            gboolean preserve_legacy_frame =
                slot == 0 && app->child_frame_index > 0 &&
                app->child_animation_id == record->next &&
                (previous_child_parents[0] == 0 ||
                 previous_child_parents[0] == parent_animation);
            EsheepActor *parent_actor = parent_slot < 0 ? &app->actor :
                                        &app->child_actors[parent_slot];
            esheep_actor_add_child(parent_actor, &app->child_actors[slot],
                                   record->next, 0, 0, app->direction);
            esheep_set_environment(&app->child_actors[slot].state,
                                   app->bounds.width, app->bounds.height,
                                   app->tile_size, app->tile_size);
            if (preserve_legacy_frame)
                app->child_actors[slot].state.frame_index =
                    app->child_frame_index;
            app->child_frame_indices[slot] = 0;
            app->child_elapsed_ms_values[slot] = 0;
            app->child_pose_x[slot] = 0;
            app->child_pose_y[slot] = 0;
        }
        int cid = app->child_actors[slot].state.animation_id;
        if (cid < 1 || cid > esheep_animation_count) cid = record->next;
        app->child_animation_ids[slot] = cid;
        app->child_authored_animations[slot] = record->next;
        app->child_parent_animations[slot] = parent_animation;
        const EsheepAnimation *canim = &esheep_animations[cid - 1];
        int frame = app->child_actors[slot].state.frame_index;
        if (frame < 0 || frame >= canim->frame_count) frame = 0;
        child_tile_ids[slot] = canim->frames[frame];
        int child_base_x = eval_child_expression(
            record->x, app->bounds.width, app->bounds.height,
            app->tile_size, app->tile_size, parent_x, parent_y, 0);
        int child_base_y = eval_child_expression(
            record->y, app->bounds.width, app->bounds.height,
            app->tile_size, app->tile_size, parent_x, parent_y, 0);
        child_x[slot] = child_base_x + app->child_pose_x[slot];
        child_y[slot] = child_base_y + app->child_pose_y[slot] +
                        pose_offset_y(canim, frame);
        app->child_render_x[slot] = child_x[slot];
        app->child_render_y[slot] = child_y[slot];
        child_flipped[slot] = sprite_is_flipped(app, canim);
        double opacity_progress = canim->frame_count <= 1 ? 0.0 :
            (double)frame / (double)(canim->frame_count - 1);
        child_opacity[slot] = canim->start.opacity +
            (canim->end.opacity - canim->start.opacity) * opacity_progress;
        child_visible[slot] = TRUE;
        }
    }
    for (int slot = child_count; slot < MAX_RUNTIME_CHILDREN; slot++) {
        esheep_actor_detach(&app->child_actors[slot]);
        app->child_animation_ids[slot] = 0;
        app->child_authored_animations[slot] = 0;
        app->child_parent_animations[slot] = 0;
        app->child_frame_indices[slot] = 0;
        app->child_elapsed_ms_values[slot] = 0;
        app->child_pose_x[slot] = 0;
        app->child_pose_y[slot] = 0;
        app->child_render_x[slot] = 0;
        app->child_render_y[slot] = 0;
    }
    app->child_animation_id = child_count > 0 ? app->child_animation_ids[0] : 0;
    app->child_frame_index = child_count > 0 ? app->child_frame_indices[0] : 0;
    app->child_elapsed_ms = child_count > 0 ? app->child_elapsed_ms_values[0] : 0;

    esheep_renderer_compose(&app->scene,
        esheep_current_tile(&app->state),
        sprite_is_flipped(app, &esheep_animations[app->state.animation_id - 1]) ? 1 : 0, 1.0, true,
        child_count,
        child_tile_ids, child_x, child_y,
        child_flipped, child_opacity, child_visible);
    app->scene.tiles[0].y = pose_offset_y(
        &esheep_animations[app->state.animation_id - 1],
        app->state.frame_index);
}

static void accumulate_child_events(App *app, EsheepActor *actor) {
    int slot = (int)(actor - app->child_actors);
    for (int event_index = 0;
         event_index < actor->state.event_count; event_index++) {
        EsheepFrameEvent event = actor->state.events[event_index];
        if (event.animation_id < 1 ||
            event.animation_id > esheep_animation_count) continue;
        const EsheepAnimation *animation =
            &esheep_animations[event.animation_id - 1];
        app->child_pose_x[slot] += pose_delta(app, animation,
                                              event.frame_index, TRUE);
        app->child_pose_y[slot] += pose_delta(app, animation,
                                              event.frame_index, FALSE);
    }
    for (int child_index = 0; child_index < actor->child_count;
         child_index++)
        accumulate_child_events(app, actor->children[child_index]);
}

static void advance_child_animation(App *app, int dt_ms) {
    /* Keep the legacy first-child fields usable for callers that construct an
     * App directly (including older integrations and regression fixtures). */
    if (app->child_animation_ids[0] == 0 && app->child_animation_id > 0) {
        app->child_animation_ids[0] = app->child_animation_id;
        esheep_actor_init(&app->child_actors[0], NULL,
                          app->child_animation_id, 0, 0, app->direction);
        esheep_actor_set_random_source(&app->child_actors[0],
                                       actor_random_source, app);
        esheep_set_environment(&app->child_actors[0].state,
                               app->bounds.width, app->bounds.height,
                               app->tile_size, app->tile_size);
        app->child_actors[0].state.frame_index = app->child_frame_index;
        app->child_actors[0].state.elapsed_ms = app->child_elapsed_ms;
        app->child_frame_indices[0] = app->child_frame_index;
        app->child_elapsed_ms_values[0] = app->child_elapsed_ms;
    }
    for (int slot = 0; slot < MAX_RUNTIME_CHILDREN; slot++) {
        int animation_id = app->child_animation_ids[slot];
        if (animation_id < 1 || animation_id > esheep_animation_count)
            continue;
        if (app->child_actors[slot].parent == &app->actor) {
            esheep_actor_tick(&app->child_actors[slot], dt_ms, "none");
            accumulate_child_events(app, &app->child_actors[slot]);
        }
        app->child_animation_ids[slot] =
            app->child_actors[slot].state.animation_id;
        app->child_frame_indices[slot] =
            app->child_actors[slot].state.frame_index;
        app->child_elapsed_ms_values[slot] =
            app->child_actors[slot].state.elapsed_ms;
    }
    app->child_animation_id = app->child_animation_ids[0];
    app->child_frame_index = app->child_frame_indices[0];
    app->child_elapsed_ms = app->child_elapsed_ms_values[0];
}

static gboolean on_tick(gpointer user_data) {
    App *app = user_data;
    if (app->paused && !app->dragging) return G_SOURCE_CONTINUE;

    if (app->dragging) {
        /* Position is driven by the pointer while dragging; still let the
         * interpreter step so the drag animation's frames keep cycling. */
        int roll = app_random_0_99(app);
        esheep_tick(&app->state, (int)app->tick_ms, "none", roll);
        update_child_animation(app);
        advance_child_animation(app, (int)app->tick_ms);
        update_child_animation(app);
        sync_scene_window(app);
        gtk_widget_queue_draw(app->window);
        set_sprite_input_region(app);
        return G_SOURCE_CONTINUE;
    }

    update_monitor_bounds(app);
    refresh_objects(app);

    if (app->fullscreen_suppressed) {
        if (!app->hidden)
            gtk_widget_hide(app->window);
        return G_SOURCE_CONTINUE;
    }
    if (app->fullscreen_suppressed == FALSE &&
        gtk_widget_get_visible(app->window) == FALSE && !app->hidden)
        gtk_widget_show(app->window);

    /* Build context and classify surfaces using the platform-independent helper */
    EsheepContext ctx;
    build_context(app, &ctx);
    esheep_classify_context(&ctx);

    /* Landing is resolved by step_position() after the fall animation moves
     * downward. Snapping from this pre-step context would teleport a sheep to
     * the floor or to a window it is merely passing through. */
    int floor_y = app->bounds.y + app->bounds.height - app->tile_size;

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
                /* Move strictly inward so repeated ticks cannot re-dispatch a
                 * turn at the same edge. */
                if (ctx.surface == ESHEEP_SURFACE_LEFT_EDGE) {
                    app->pos_x = app->bounds.x + 2;
                }
                if (ctx.surface == ESHEEP_SURFACE_RIGHT_EDGE) {
                    int right = app->bounds.x + app->bounds.width - app->tile_size;
                    app->pos_x = right - 2;
                }
                /* Transition to edge turn animation (animation 2) */
                esheep_init(&app->state, 2);
            }
        }
    }

    if (!climbing && ctx.move != ESHEEP_MOVE_FALLING &&
        !is_airborne_animation(app->state.animation_id) &&
        app->pos_y < floor_y &&
        !object_underfoot(app) &&
        app->state.animation_id != ANIM_FALL) {
        esheep_gravity_event(&app->state, "none", app_random_0_99(app));
        if (app->state.animation_id != ANIM_FALL)
            esheep_init(&app->state, ANIM_FALL);
    }

    /* Movement/collision context is decided by the CURRENT position, before
     * this tick's frame step -- e.g. if we're already pinned against the
     * right edge, this tick's context is "vertical" regardless of which
     * direction the current animation is trying to move. */
    const char *pretick_context = esheep_transition_context(&ctx);

    /* Reset edge dispatch flag when no longer at the edge so the next edge
     * encounter can trigger a fresh reversal. */
    if (ctx.surface != ESHEEP_SURFACE_LEFT_EDGE &&
        ctx.surface != ESHEEP_SURFACE_RIGHT_EDGE) {
        app->edge_dispatched = FALSE;
    }

    int roll = app_random_0_99(app);
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
                    int border_roll = app_random_0_99(app);
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
    update_child_animation(app);

    sync_scene_window(app);
    gtk_window_move(GTK_WINDOW(app->window),
                    app->pos_x - app->scene_origin_x,
                    app->pos_y - app->scene_origin_y);
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

static void on_pause_activate(GtkMenuItem *item, gpointer user_data) {
    App *app = user_data;
    (void)item;
    app->paused = !app->paused;
}

static void on_hide_activate(GtkMenuItem *item, gpointer user_data) {
    App *app = user_data;
    (void)item;
    app->hidden = !app->hidden;
    if (app->hidden) {
        gtk_widget_hide(app->window);
    } else {
        gtk_widget_show(app->window);
    }
}

static void on_bring_to_front_activate(GtkMenuItem *item, gpointer user_data) {
    App *app = user_data;
    (void)item;
    if (app->hidden) {
        app->hidden = FALSE;
        gtk_widget_show(app->window);
    }
    gtk_window_present(GTK_WINDOW(app->window));
}

static void on_group_pause_activate(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    SheepGroup *group = user_data;
    gboolean paused = group && group->count > 0 && group->sheep[0].paused;
    group_set_paused(group, !paused);
}

static void on_group_hide_activate(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    SheepGroup *group = user_data;
    gboolean hidden = group && group->count > 0 && group->sheep[0].hidden;
    group_set_hidden(group, !hidden);
}

static void on_group_front_activate(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    group_present(user_data);
}

static void on_review_response(GtkDialog *dialog, gint response,
                               gpointer user_data) {
    if (response == GTK_RESPONSE_OK) {
        GtkSpinButton *spin = g_object_get_data(G_OBJECT(dialog), "animation-id");
        group_set_review_animation(user_data,
                                   gtk_spin_button_get_value_as_int(spin));
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void on_review_activate(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    SheepGroup *group = user_data;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Review Animation", NULL, GTK_DIALOG_MODAL,
        "Cancel", GTK_RESPONSE_CANCEL, "Show", GTK_RESPONSE_OK, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    GtkWidget *spin = gtk_spin_button_new_with_range(1, esheep_animation_count, 1);
    GtkWidget *label = gtk_label_new("Animation ID (see --list-animations)");
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), spin, 1, 0, 1, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 1);
    g_object_set_data(G_OBJECT(dialog), "animation-id", spin);
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_container_add(GTK_CONTAINER(content), grid);
    g_signal_connect(dialog, "response", G_CALLBACK(on_review_response), group);
    gtk_widget_show_all(dialog);
}

static void on_about_activate(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    gtk_show_about_dialog(NULL,
                          "program-name", "linux-esheep",
                          "version", ESHEEP_VERSION,
                          "comments", "A sheep desktop pet for Linux",
                          NULL);
}

static void show_current_animation(App *app) {
    if (!app || app->state.animation_id < 1 ||
        app->state.animation_id > esheep_animation_count) return;
    const EsheepAnimation *animation =
        &esheep_animations[app->state.animation_id - 1];
    const char *child = app->child_animation_id > 0 ? "present" : "none";
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
        "Current animation");
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "Animation %d: %s\nFrame %d/%d\nChild: %s",
        animation->id, animation->name, app->state.frame_index + 1,
        animation->frame_count, child);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void on_current_animation_activate(GtkMenuItem *item,
                                          gpointer user_data) {
    (void)item;
    show_current_animation(user_data);
}

static void save_group_settings(SheepGroup *group) {
    if (!group || !group->config || !group->config_path) return;
    gchar *directory = g_path_get_dirname(group->config_path);
    g_mkdir_with_parents(directory, 0700);
    g_free(directory);
    g_key_file_set_integer(group->config, "esheep", "tick_ms",
                           (gint)group->tick_ms);
    g_key_file_set_integer(group->config, "esheep", "count",
                           (gint)group->configured_count);
    g_key_file_set_integer(group->config, "esheep", "monitor",
                           (gint)group->monitor_index);
    g_key_file_set_integer(group->config, "esheep", "review_animation",
                           group->review_animation);
    if (group->character[0] != '\0')
        g_key_file_set_string(group->config, "esheep", "character",
                              group->character);
    else
        g_key_file_remove_key(group->config, "esheep", "character", NULL);
    if (group->spritesheet[0] != '\0')
        g_key_file_set_string(group->config, "esheep", "spritesheet",
                              group->spritesheet);
    else
        g_key_file_remove_key(group->config, "esheep", "spritesheet", NULL);
    if (group->package[0] != '\0')
        g_key_file_set_string(group->config, "esheep", "package",
                              group->package);
    else
        g_key_file_remove_key(group->config, "esheep", "package", NULL);
    g_key_file_set_integer(group->config, "esheep", "walk_keep_probability",
                           (gint)group->walk_keep_probability);
    g_key_file_set_boolean(group->config, "esheep", "window_landing",
                           group->window_landing);
    g_key_file_set_boolean(group->config, "esheep", "exclude_conky",
                           group->exclude_conky);
    if (group->spawn_mode[0] != '\0')
        g_key_file_set_string(group->config, "esheep", "spawn",
                              group->spawn_mode);
    gsize length = 0;
    GError *error = NULL;
    gchar *data = g_key_file_to_data(group->config, &length, &error);
    if (data) {
        if (!g_file_set_contents(group->config_path, data, (gssize)length,
                                 &error))
            g_printerr("failed to save settings '%s': %s\n",
                       group->config_path, error->message);
        g_free(data);
    }
    if (error) g_error_free(error);
}

static void on_settings_response(GtkDialog *dialog, gint response,
                                 gpointer user_data) {
    SheepGroup *group = user_data;
    if (response == GTK_RESPONSE_OK) {
        GtkSpinButton *tick = g_object_get_data(G_OBJECT(dialog), "tick-ms");
        GtkSpinButton *walk = g_object_get_data(G_OBJECT(dialog), "walk-keep");
        GtkSpinButton *monitor = g_object_get_data(G_OBJECT(dialog), "monitor");
        GtkSpinButton *count = g_object_get_data(G_OBJECT(dialog), "count");
        GtkComboBoxText *review = g_object_get_data(G_OBJECT(dialog), "review");
        GtkComboBoxText *spawn = g_object_get_data(G_OBJECT(dialog), "spawn");
        GtkEntry *character = g_object_get_data(G_OBJECT(dialog), "character");
        GtkEntry *spritesheet = g_object_get_data(G_OBJECT(dialog), "spritesheet");
        GtkEntry *package = g_object_get_data(G_OBJECT(dialog), "package");
        GtkToggleButton *landing = g_object_get_data(G_OBJECT(dialog), "landing");
        GtkToggleButton *conky = g_object_get_data(G_OBJECT(dialog), "conky");
        group_set_tick_ms(group, (guint)gtk_spin_button_get_value_as_int(tick));
        group_set_walk_keep_probability(
            group, (guint)gtk_spin_button_get_value_as_int(walk));
        group_set_monitor(group,
                          (guint)gtk_spin_button_get_value_as_int(monitor));
        group->configured_count = clamp_sheep_count(
            (guint)gtk_spin_button_get_value_as_int(count));
        const char *review_id = gtk_combo_box_get_active_id(
            GTK_COMBO_BOX(review));
        char *review_end = NULL;
        long review_value = review_id ? strtol(review_id, &review_end, 10) : -1;
        int review_animation = review_end && *review_end == '\0' &&
                               review_value >= 0 &&
                               review_value <= esheep_animation_count ?
                               (int)review_value : -1;
        if (!group_set_review_animation(group, review_animation)) {
            char previous_review_id[16];
            g_snprintf(previous_review_id, sizeof(previous_review_id), "%d",
                       group->review_animation);
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(review),
                                        previous_review_id);
        }
        gchar *spawn_mode = gtk_combo_box_text_get_active_text(spawn);
        if (spawn_mode) {
            g_strlcpy(group->spawn_mode, spawn_mode,
                      sizeof(group->spawn_mode));
            g_free(spawn_mode);
        }
        g_strlcpy(group->character, gtk_entry_get_text(character),
                  sizeof(group->character));
        g_strlcpy(group->spritesheet, gtk_entry_get_text(spritesheet),
                  sizeof(group->spritesheet));
        g_strlcpy(group->package, gtk_entry_get_text(package),
                  sizeof(group->package));
        group_set_window_landing(group, gtk_toggle_button_get_active(landing));
        group_set_exclude_conky(group, gtk_toggle_button_get_active(conky));
        save_group_settings(group);
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void on_settings_activate(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    SheepGroup *group = user_data;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "eSheep Settings", NULL, GTK_DIALOG_MODAL,
        "Cancel", GTK_RESPONSE_CANCEL, "Apply", GTK_RESPONSE_OK, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    GtkWidget *tick = gtk_spin_button_new_with_range(10, 1000, 1);
    GtkWidget *walk = gtk_spin_button_new_with_range(0, 100, 1);
    GtkWidget *count = gtk_spin_button_new_with_range(1, MAX_SHEEP, 1);
    GtkWidget *review = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(review), "0", "Normal walking");
    for (int animation_index = 0; animation_index < esheep_animation_count;
         animation_index++) {
        const EsheepAnimation *animation = &esheep_animations[animation_index];
        char animation_id[16];
        g_snprintf(animation_id, sizeof(animation_id), "%d", animation->id);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(review), animation_id,
                                  animation->name && animation->name[0] ?
                                  animation->name : animation_id);
    }
    int monitor_count = group->display ?
                        gdk_display_get_n_monitors(group->display) : 1;
    GtkWidget *monitor = gtk_spin_button_new_with_range(
        0, MAX(0, monitor_count - 1), 1);
    GtkWidget *landing = gtk_check_button_new_with_label("Land on windows and panels");
    GtkWidget *conky = gtk_check_button_new_with_label("Allow Conky as a surface");
    GtkWidget *spawn = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(spawn), "bottom", "Bottom");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(spawn), "window", "Window");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(spawn), "random", "Random");
    GtkWidget *character = gtk_entry_new();
    GtkWidget *spritesheet = gtk_entry_new();
    GtkWidget *package = gtk_entry_new();
    GtkWidget *note = gtk_label_new("Character, asset paths, package, and count apply on restart.");
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Tick interval (ms)"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), tick, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Walk keep probability (%)"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), walk, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Monitor index"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), monitor, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Sheep count"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), count, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Review animation"),
                    0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), review, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Spawn mode"), 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), spawn, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Character"), 0, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), character, 1, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Spritesheet"), 0, 7, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), spritesheet, 1, 7, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Behavior package"), 0, 8, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), package, 1, 8, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), landing, 0, 9, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), conky, 0, 10, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), note, 0, 11, 2, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(tick), group->tick_ms);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(walk), group->walk_keep_probability);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(monitor), group->monitor_index);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(count), group->configured_count);
    char review_id[16];
    g_snprintf(review_id, sizeof(review_id), "%d", group->review_animation);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(review), review_id);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(spawn), group->spawn_mode);
    gtk_entry_set_text(GTK_ENTRY(character), group->character);
    gtk_entry_set_text(GTK_ENTRY(spritesheet), group->spritesheet);
    gtk_entry_set_text(GTK_ENTRY(package), group->package);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(landing), group->window_landing);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(conky), !group->exclude_conky);
    g_object_set_data(G_OBJECT(dialog), "tick-ms", tick);
    g_object_set_data(G_OBJECT(dialog), "walk-keep", walk);
    g_object_set_data(G_OBJECT(dialog), "monitor", monitor);
    g_object_set_data(G_OBJECT(dialog), "count", count);
    g_object_set_data(G_OBJECT(dialog), "review", review);
    g_object_set_data(G_OBJECT(dialog), "spawn", spawn);
    g_object_set_data(G_OBJECT(dialog), "character", character);
    g_object_set_data(G_OBJECT(dialog), "spritesheet", spritesheet);
    g_object_set_data(G_OBJECT(dialog), "package", package);
    g_object_set_data(G_OBJECT(dialog), "landing", landing);
    g_object_set_data(G_OBJECT(dialog), "conky", conky);
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_container_add(GTK_CONTAINER(content), grid);
    g_signal_connect(dialog, "response", G_CALLBACK(on_settings_response), group);
    gtk_widget_show_all(dialog);
}

static void on_tray_popup(GtkStatusIcon *icon, guint button, guint activate_time,
                          gpointer user_data) {
    (void)button;
    (void)activate_time;
    SheepGroup *group = user_data;
    GtkWidget *menu = gtk_menu_new();
    gboolean paused = group && group->count > 0 && group->sheep[0].paused;
    gboolean hidden = group && group->count > 0 && group->sheep[0].hidden;
    GtkWidget *pause = gtk_menu_item_new_with_label(paused ? "Resume All" : "Pause All");
    GtkWidget *hide = gtk_menu_item_new_with_label(hidden ? "Show All" : "Hide All");
    GtkWidget *front = gtk_menu_item_new_with_label("Bring All to Front");
    GtkWidget *current = gtk_menu_item_new_with_label("Current Animation");
    GtkWidget *review = gtk_menu_item_new_with_label("Review Animation");
    GtkWidget *settings = gtk_menu_item_new_with_label("Settings");
    GtkWidget *about = gtk_menu_item_new_with_label("About");
    GtkWidget *quit = gtk_menu_item_new_with_label("Quit");

    g_signal_connect(pause, "activate", G_CALLBACK(on_group_pause_activate), group);
    g_signal_connect(hide, "activate", G_CALLBACK(on_group_hide_activate), group);
    g_signal_connect(front, "activate", G_CALLBACK(on_group_front_activate), group);
    g_signal_connect(current, "activate",
                     G_CALLBACK(on_current_animation_activate),
                     group && group->count > 0 ? &group->sheep[0] : NULL);
    g_signal_connect(review, "activate", G_CALLBACK(on_review_activate), group);
    g_signal_connect(settings, "activate", G_CALLBACK(on_settings_activate), group);
    g_signal_connect(about, "activate", G_CALLBACK(on_about_activate), NULL);
    g_signal_connect(quit, "activate", G_CALLBACK(on_quit_activate), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), pause);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), hide);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), front);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), current);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), review);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), settings);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), about);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);
    gtk_widget_show_all(menu);
    (void)icon;
    gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);
}

static void on_tray_activate(GtkStatusIcon *icon, gpointer user_data) {
    (void)icon;
    group_present(user_data);
}

static GtkStatusIcon *create_tray_icon(SheepGroup *group) {
    GtkStatusIcon *icon;
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    icon = gtk_status_icon_new_from_icon_name("face-smile");
    if (!icon) return NULL;
    gtk_status_icon_set_title(icon, "linux-esheep");
    gtk_status_icon_set_tooltip_text(icon, "linux-esheep");
    G_GNUC_END_IGNORE_DEPRECATIONS
    g_signal_connect(icon, "popup-menu", G_CALLBACK(on_tray_popup), group);
    g_signal_connect(icon, "activate", G_CALLBACK(on_tray_activate), group);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_status_icon_set_visible(icon, TRUE);
    G_GNUC_END_IGNORE_DEPRECATIONS
    return icon;
}

static void show_pet_menu(App *app, GdkEventButton *event) {
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *pause_item = gtk_check_menu_item_new_with_label(
        app->paused ? "Resume" : "Pause");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(pause_item), app->paused);
    g_signal_connect(pause_item, "activate",
                     G_CALLBACK(on_pause_activate), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), pause_item);

    GtkWidget *hide_item = gtk_check_menu_item_new_with_label(
        app->hidden ? "Show" : "Hide");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(hide_item), app->hidden);
    g_signal_connect(hide_item, "activate",
                     G_CALLBACK(on_hide_activate), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), hide_item);

    GtkWidget *front_item = gtk_menu_item_new_with_label("Bring to Front");
    g_signal_connect(front_item, "activate",
                     G_CALLBACK(on_bring_to_front_activate), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), front_item);

    GtkWidget *current_item = gtk_menu_item_new_with_label("Current Animation");
    g_signal_connect(current_item, "activate",
                     G_CALLBACK(on_current_animation_activate), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), current_item);

    GtkWidget *separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);

    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate",
                     G_CALLBACK(on_quit_activate), NULL);
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
        show_pet_menu(app, event);
    }
    return TRUE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    App *app = user_data;
    (void)widget;

    if (event->button == 1 && app->dragging) {
        app->dragging = FALSE;
        app->pos_x = (int)event->x_root - app->drag_grab_x;
        app->pos_y = (int)event->y_root - app->drag_grab_y;
        update_monitor_bounds(app);
        int floor_y = app->bounds.y + app->bounds.height - app->tile_size;
        esheep_init(&app->state, app->pos_y < floor_y ? ANIM_FALL : ANIM_WALK);
        app->drop_landing_enabled = app->pos_y < floor_y;
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
        gtk_window_move(GTK_WINDOW(app->window),
                        app->pos_x - app->scene_origin_x,
                        app->pos_y - app->scene_origin_y);
    }
    return TRUE;
}


/* Normal managed stacking lets the window manager occlude the sheep when an
 * application window is raised. Keep this policy in one place so tests can
 * verify the contract without pretending to be a compositor. */
static GdkWindowTypeHint sheep_window_type_hint(void) {
    return GDK_WINDOW_TYPE_HINT_NORMAL;
}

static void setup_sheep_window(App *app, GdkDisplay *display,
                               GdkMonitor *monitor) {
    /* Managed undecorated toplevel using normal WM stacking:
     * - GTK_WINDOW_TOPLEVEL + GDK_WINDOW_TYPE_HINT_NORMAL gives the WM full
     *   control; the sheep participates in normal window occlusion and will
     *   be covered by any newly-raised application window.
     * - skip_taskbar + skip_pager keep it out of the pager and taskbar.
     * - Transparency, parent-only input, dragging, and scene-origin
     *   positioning are preserved via gtk_widget_set_visual, event masks,
     *   and gtk_window_move.
     *
     * Limitation: with a compositor that always keeps "active" or "focused"
     * windows above everything else, NORMAL-type windows may still receive
     * unexpected raise requests from that compositor. The sheep cannot
     * prevent this without reverting to notification-type or unconditional
     * keep-above. */
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
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
    gtk_window_set_type_hint(GTK_WINDOW(window), sheep_window_type_hint());
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    /* A desktop pet must not become the active window merely by appearing.
     * Let the window manager keep application windows above it for genuine
     * occlusion; the explicit tray/menu "Bring to Front" action is the only
     * path that intentionally raises the pet. */
    gtk_window_set_focus_on_map(GTK_WINDOW(window), FALSE);
    gtk_window_set_accept_focus(GTK_WINDOW(window), FALSE);

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
    const char *package_override = NULL;
    const char *spawn_override = NULL;
    const char *config_override = NULL;
    guint tick_ms = env_uint("ESHEEP_TICK_MS", TICK_MS, 10, 1000);
    guint count = env_uint("ESHEEP_COUNT", 1, 1, MAX_SHEEP);
    guint monitor_index = env_uint("ESHEEP_MONITOR", G_MAXUINT, 0, G_MAXUINT);
    guint walk_keep_probability = env_uint("ESHEEP_WALK_KEEP_PROBABILITY",
                                           90, 0, 100);
    guint random_seed = env_uint("ESHEEP_SEED", 0, 1, G_MAXUINT);
    gboolean window_landing = env_bool("ESHEEP_WINDOW_LANDING", TRUE);
    gboolean exclude_conky = env_bool("ESHEEP_EXCLUDE_CONKY", TRUE);
    gboolean x11_fallback = env_bool("ESHEEP_X11_FALLBACK", FALSE);
    gboolean tick_cli = FALSE;
    gboolean count_cli = FALSE;
    gboolean monitor_cli = FALSE;
    gboolean spawn_cli = FALSE;
    gboolean window_landing_cli = FALSE;
    gboolean exclude_conky_cli = FALSE;
    gboolean walk_keep_cli = FALSE;
    gboolean seed_cli = FALSE;
    int review_animation = 0;
    int review_parent = 0;
    gboolean list_animations = FALSE;
    gboolean list_transitions = FALSE;
    gboolean review_cli = FALSE;
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
        if (strcmp(argv[i], "--package") == 0 && i + 1 < argc) {
            package_override = argv[++i];
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
        if (strcmp(argv[i], "--monitor") == 0 && i + 1 < argc) {
            char *end = NULL;
            unsigned long long value = strtoull(argv[++i], &end, 10);
            if (*end || value > G_MAXUINT) {
                g_printerr("invalid --monitor value (use a non-negative index)\n");
                return 2;
            }
            monitor_index = (guint)value;
            monitor_cli = TRUE;
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
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            char *end = NULL;
            unsigned long long value = strtoull(argv[++i], &end, 10);
            if (*end || value == 0 || value > G_MAXUINT) {
                g_printerr("invalid --seed value (use 1-%u)\n", G_MAXUINT);
                return 2;
            }
            random_seed = (guint)value;
            seed_cli = TRUE;
            continue;
        }
        if (strcmp(argv[i], "--review-animation") == 0 && i + 1 < argc) {
            char *end = NULL;
            long value = strtol(argv[++i], &end, 10);
            if (*end || value < 0) {
                g_printerr("invalid --review-animation value (use 0-%d)\n",
                           esheep_animation_count);
                return 2;
            }
            review_animation = (int)value;
            review_cli = TRUE;
            continue;
        }
        if (strcmp(argv[i], "--review-parent") == 0 && i + 1 < argc) {
            char *end = NULL;
            long value = strtol(argv[++i], &end, 10);
            if (*end || value < 1) {
                g_printerr("invalid --review-parent value (use a positive ID)\n");
                return 2;
            }
            review_parent = (int)value;
            continue;
        }
        if (strcmp(argv[i], "--list-animations") == 0) {
            list_animations = TRUE;
            continue;
        }
        if (strcmp(argv[i], "--list-transitions") == 0) {
            list_transitions = TRUE;
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
    GKeyFile *config = g_key_file_new();
    gchar *default_config_path = NULL;
    gchar *config_character = NULL;
    gchar *config_sprite = NULL;
    gchar *config_spawn = NULL;
    gchar *config_package = NULL;
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
        if (!package_override && !getenv("ESHEEP_PACKAGE")) {
            config_package = g_key_file_get_string(config, "esheep",
                                                   "package", NULL);
            package_override = config_package;
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
        if (!seed_cli && !getenv("ESHEEP_SEED") &&
            g_key_file_has_key(config, "esheep", "seed", NULL)) {
            gint64 value = g_key_file_get_int64(config, "esheep", "seed", NULL);
            if (value > 0 && value <= G_MAXUINT) random_seed = (guint)value;
        }
        if (!monitor_cli && !getenv("ESHEEP_MONITOR") &&
            g_key_file_has_key(config, "esheep", "monitor", NULL)) {
            gint64 value = g_key_file_get_int64(config, "esheep", "monitor", NULL);
            if (value >= 0 && value <= G_MAXUINT) monitor_index = (guint)value;
        }
        if (!review_cli && g_key_file_has_key(config, "esheep",
                                              "review_animation", NULL)) {
            gint64 value = g_key_file_get_int64(config, "esheep",
                                                "review_animation", NULL);
            if (value >= 0 && value <= esheep_animation_count)
                review_animation = (int)value;
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
    if (random_seed != 0)
        app_random_seed = (uint32_t)random_seed;
    else
        app_random_seed = (uint32_t)time(NULL) ^ (uint32_t)getpid();
    if (app_random_seed == 0) app_random_seed = 0xC0FFEE01u;
    count = clamp_sheep_count(count);

    GError *error = NULL;
    EsheepPetPackage *runtime_package = NULL;
    const char *package_path = package_override ? package_override :
                               getenv("ESHEEP_PACKAGE");
    if (package_path && !esheep_pet_package_load(package_path, &runtime_package,
                                                  &error)) {
        g_printerr("failed to load behavior package '%s': %s\n", package_path,
                   error ? error->message : "invalid package");
        if (error) g_error_free(error);
        g_free(config_character); g_free(config_sprite); g_free(config_spawn);
        g_free(config_package); g_free(default_config_path);
        g_key_file_free(config);
        return 2;
    }
    if (runtime_package) esheep_pet_package_activate(runtime_package);

    if ((review_animation > 0 && review_animation > esheep_animation_count) ||
        (review_parent > 0 && review_parent > esheep_animation_count)) {
        g_printerr("review animation ID is outside the active graph (use 1-%d)\n",
                   esheep_animation_count);
        esheep_pet_package_free(runtime_package);
        g_free(config_character); g_free(config_sprite); g_free(config_spawn);
        g_free(config_package); g_free(default_config_path);
        g_key_file_free(config);
        return 2;
    }

    if (list_animations || list_transitions) {
        if (list_animations) {
            for (int i = 0; i < esheep_animation_count; i++)
                g_print("%d\t%s\n", esheep_animations[i].id,
                        esheep_animations[i].name ? esheep_animations[i].name : "");
        }
        if (list_transitions) print_transitions();
        esheep_pet_package_free(runtime_package);
        g_free(config_character); g_free(config_sprite); g_free(config_spawn);
        g_free(config_package); g_free(default_config_path);
        g_key_file_free(config);
        return 0;
    }

    const char *character = character_override ? character_override :
                            getenv("ESHEEP_CHARACTER");
    gboolean custom_sprite_selected = sprite_override ||
                                      getenv("ESHEEP_SPRITESHEET") ||
                                      config_sprite;
    if (character && strcasecmp(character, "sheep") != 0 &&
        strcasecmp(character, "penguin") != 0 && !custom_sprite_selected) {
        g_printerr("invalid character '%s' (use sheep or penguin, or provide "
                   "a custom spritesheet)\n", character);
        esheep_pet_package_free(runtime_package);
        g_free(config_character); g_free(config_sprite); g_free(config_spawn);
        g_free(config_package); g_free(default_config_path);
        g_key_file_free(config);
        return 2;
    }
    /* Custom-pet package: resolve spritesheet path respecting precedence.
     * An explicit --sprite takes absolute priority; then ESHEEP_SPRITESHEET
     * env var; then the config file spritesheet key; finally the built-in
     * default for the selected character. */
    const char *sheet_path;
    char default_sheet_path_buf[4096];
    if (sprite_override) {
        sheet_path = sprite_override;
    } else {
        const char *env_path = getenv("ESHEEP_SPRITESHEET");
        sheet_path = env_path ? env_path : config_sprite;
        if (!sheet_path && runtime_package)
            sheet_path = esheep_pet_package_spritesheet(runtime_package);
        if (!sheet_path) {
            snprintf(default_sheet_path_buf, sizeof(default_sheet_path_buf),
                     "%s/%s_spritesheet.png", ESHEEP_DATADIR,
                     character && strcasecmp(character, "penguin") == 0 ?
                     "penguin_ice_blue" : "sheep");
            sheet_path = default_sheet_path_buf;
        }
    }

    GdkPixbuf *sheet = gdk_pixbuf_new_from_file(sheet_path, &error);
    if (!sheet) {
        g_printerr("failed to load spritesheet '%s': %s\n", sheet_path,
                   error ? error->message : "unknown error");
        if (character && strcasecmp(character, "sheep") != 0 &&
            strcasecmp(character, "penguin") != 0) {
            g_printerr("custom-pet: character '%s' resolved to path '%s', which "
                       "could not be read. Verify the path or use --sprite to "
                       "select a valid spritesheet.\n", character, sheet_path);
        }
        g_free(config_character);
        g_free(config_sprite);
        g_free(config_spawn);
        g_free(config_package);
        g_free(default_config_path);
        g_key_file_free(config);
        esheep_pet_package_free(runtime_package);
        return 1;
    }

    int tile_size = gdk_pixbuf_get_width(sheet) / esheep_tiles_x;
    if (!validate_spritesheet_pixbuf(sheet, sheet_path, character)) {
        g_object_unref(sheet);
        g_free(config_character);
        g_free(config_sprite);
        g_free(config_spawn);
        g_free(config_package);
        g_free(default_config_path);
        g_key_file_free(config);
        esheep_pet_package_free(runtime_package);
        return 2;
    }

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
        g_free(config_package);
        g_free(default_config_path);
        g_key_file_free(config);
        esheep_pet_package_free(runtime_package);
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
    if (monitor_index != G_MAXUINT) {
        int monitor_count = gdk_display_get_n_monitors(display);
        if (monitor_index >= (guint)monitor_count) {
            g_printerr("monitor index %u is out of range (available: 0-%d)\n",
                       monitor_index, monitor_count > 0 ? monitor_count - 1 : 0);
            g_free(config_character);
            g_free(config_sprite);
            g_free(config_spawn);
            g_free(config_package);
            g_free(default_config_path);
            g_key_file_free(config);
            esheep_pet_package_free(runtime_package);
            g_object_unref(sheet);
            return 2;
        }
        GdkMonitor *monitor = gdk_display_get_monitor(display,
                                                       (gint)monitor_index);
        if (monitor) {
            gdk_monitor_get_workarea(monitor, &initial_bounds);
            have_initial_bounds = TRUE;
        }
    } else {
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
    }
    if (!have_initial_bounds) {
        GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
        if (!monitor) monitor = gdk_display_get_monitor(display, 0);
        if (!monitor) {
            g_printerr("failed to locate a monitor workarea\n");
            g_free(config_character);
            g_free(config_sprite);
            g_free(config_spawn);
            g_free(config_package);
            g_free(default_config_path);
            g_key_file_free(config);
            esheep_pet_package_free(runtime_package);
            g_object_unref(sheet);
            return 1;
        }
        gdk_monitor_get_workarea(monitor, &initial_bounds);
        have_initial_bounds = TRUE;
    }
    guint active_monitor_index = monitor_index != G_MAXUINT ? monitor_index : 0;
    GdkMonitor *active_monitor = gdk_display_get_monitor_at_point(
        display, initial_bounds.x + initial_bounds.width / 2,
        initial_bounds.y + initial_bounds.height / 2);
    for (int i = 0; i < gdk_display_get_n_monitors(display); i++) {
        if (gdk_display_get_monitor(display, i) == active_monitor) {
            active_monitor_index = (guint)i;
            break;
        }
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
        app->ordinal = (int)i;
        app->direction = app_random_0_99(app) < 50 ? -1 : 1;
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
        app->bounds = initial_bounds;
        esheep_actor_init(&app->actor, NULL, ANIM_WALK, app->pos_x,
                          app->pos_y, app->direction);
        esheep_actor_set_random_source(&app->actor, actor_random_source, app);
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
        gtk_window_move(GTK_WINDOW(app->window),
                        app->pos_x - app->scene_origin_x,
                        app->pos_y - app->scene_origin_y);
        set_sprite_input_region(app);
        app->tick_source_id = g_timeout_add(app->tick_ms, on_tick, app);
    }

    if (env_bool("ESHEEP_PAUSED", FALSE)) {
        for (guint i = 0; i < count; i++)
            sheep[i].paused = TRUE;
    }
    if (env_bool("ESHEEP_HIDDEN", FALSE)) {
        for (guint i = 0; i < count; i++) {
            sheep[i].hidden = TRUE;
            gtk_widget_hide(sheep[i].window);
        }
    }

    const char *autoquit = getenv("ESHEEP_AUTOQUIT_MS");
    if (autoquit) {
        g_timeout_add((guint)atoi(autoquit), on_autoquit, NULL);
    }

    SheepGroup group = {
        .sheep = sheep,
        .count = count,
        .display = display,
        .monitor_index = active_monitor_index,
        .configured_count = count,
        .config = config,
        .config_path = config_override,
        .tick_ms = tick_ms,
        .walk_keep_probability = walk_keep_probability,
        .window_landing = window_landing,
        .exclude_conky = exclude_conky,
        .review_animation = review_animation,
    };
    g_strlcpy(group.character, character ? character : "sheep",
              sizeof(group.character));
    g_strlcpy(group.spritesheet, sheet_path, sizeof(group.spritesheet));
    if (package_path)
        g_strlcpy(group.package, package_path, sizeof(group.package));
    g_strlcpy(group.spawn_mode,
              spawn_override ? spawn_override :
              (getenv("ESHEEP_SPAWN") ? getenv("ESHEEP_SPAWN") : "bottom"),
              sizeof(group.spawn_mode));
    GtkStatusIcon *tray_icon = create_tray_icon(&group);

    gtk_main();

    if (tray_icon) {
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        gtk_status_icon_set_visible(tray_icon, FALSE);
        G_GNUC_END_IGNORE_DEPRECATIONS
        g_object_unref(tray_icon);
    }

    for (guint i = 0; i < count; i++)
        cleanup_app(&sheep[i]);

    g_free(config_character);
    g_free(config_sprite);
    g_free(config_spawn);
    g_free(config_package);
    g_free(default_config_path);
    g_key_file_free(config);
    g_object_unref(sheet);
    esheep_pet_package_free(runtime_package);
    return 0;
}
