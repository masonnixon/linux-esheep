#ifndef ESHEEP_CONTEXT_H
#define ESHEEP_CONTEXT_H

#include <stdbool.h>

typedef enum {
    ESHEEP_SURFACE_NONE = 0,
    ESHEEP_SURFACE_FLOOR,
    ESHEEP_SURFACE_WINDOW,
    ESHEEP_SURFACE_TASKBAR,
    ESHEEP_SURFACE_LEFT_EDGE,
    ESHEEP_SURFACE_RIGHT_EDGE,
    ESHEEP_SURFACE_UNSUPPORTED
} EsheepSurface;

typedef enum {
    ESHEEP_MOVE_FALLING = 0,
    ESHEEP_MOVE_WALKING,
    ESHEEP_MOVE_CLIMBING
} EsheepMoveState;

typedef struct {
    int x;
    int y;
    int width;
    int height;
    int stack_order;
    bool taskbar;
} EsheepSurfaceObject;

typedef struct {
    EsheepSurface surface;
    EsheepMoveState move;
    int surface_y;
    int pos_x;
    int pos_y;
    int image_width;
    int image_height;
    int bounds_x;
    int bounds_y;
    int bounds_width;
    int bounds_height;
    int object_count;
    const EsheepSurfaceObject *objects;
    bool window_landing_enabled;
    bool landing_allowed; /* true if window landing is enabled */
    /* Snap a dropped (airborne) sprite that overlaps a surface up to that
     * surface's top. Keep this false for walking sprites, or a sheep on the
     * floor in front of a tall window or taskbar would be lifted onto it. */
    bool drop_landing_enabled;
} EsheepContext;

typedef struct {
    bool valid;
    int landed_y;
    EsheepSurface surface;
    int object_index;
} EsheepFallTarget;

typedef struct {
    int pos_x;
    int pos_y;
    int delta_x;
    int delta_y;
    int image_width;
    int image_height;
    int bounds_x;
    int bounds_y;
    int bounds_width;
    int bounds_height;
    int object_count;
    const EsheepSurfaceObject *objects;
    bool window_landing_enabled;
    bool drop_landing_enabled;
} EsheepMotion;

void esheep_classify_context(EsheepContext *ctx);

/* Apply one resolved pose delta and return the resulting authored context.
 * This is the platform-independent movement/collision boundary; callers own
 * the input/output position and may clear drop_landing_enabled after landing. */
const char *esheep_apply_motion(EsheepMotion *motion);

/* Convert a classified pre-step context into the transition context used by
 * the authored interpreter. The returned string is static and remains valid
 * until the next call. */
const char *esheep_transition_context(const EsheepContext *ctx);

/* If falling and a window/taskbar is directly below, fill the fall target and
 * return true. Otherwise leave *out untouched and return false. */
bool esheep_classify_fall(const EsheepContext *ctx, EsheepFallTarget *out);

/* Detect whether the sprite is at the left or right horizontal edge of the
 * bounds. Returns true if either edge was tripped. */
bool esheep_edges_tripped(const EsheepContext *ctx, bool *left, bool *right);

/* When no landing target is available during a fall, snap the sprite to
 * the floor (bounds bottom) and return true. */
bool esheep_apply_fall_through(const EsheepContext *ctx, int *new_pos_x,
                               int *new_pos_y);

/* Compute the authored edge reversal: at the left edge direction becomes
 * +1, at the right edge direction becomes -1. Marks the edge as
 * dispatched so repeated ticks do not re-dispatch. */
bool esheep_authored_edge_reversal(const EsheepContext *ctx,
                                   int *next_direction,
                                   bool *edge_dispatched);

#endif /* ESHEEP_CONTEXT_H */
