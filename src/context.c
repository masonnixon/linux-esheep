#include "context.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static inline bool rects_overlap_x(int x1, int w1, int x2, int w2) {
    return x1 < x2 + w2 && x1 + w1 > x2;
}

/* Shared landing detection helper for static position checks.
 * Given the current position (bottom, top), returns the best landing target.
 * If no landing target is found, returns false and leaves out unchanged.
 * This is the single authoritative implementation for:
 *   - window landing (bottom within 2px of surface top)
 *   - drop landing (overlapping surface vertically and snapping up)
 *   - taskbar priority (same stack order: taskbar wins over window)
 */
static bool find_landing_target(int pos_x, int pos_y, int image_width, int image_height,
                                 int object_count, const EsheepSurfaceObject *objects,
                                 bool drop_landing_enabled,
                                 EsheepFallTarget *out) {
    if (!out) return false;

    int bottom = pos_y + image_height;
    int top = pos_y;
    int best_stack_order = -1;
    EsheepSurface best_surface = ESHEEP_SURFACE_NONE;
    int best_y = -1;
    int best_idx = -1;

    for (int i = 0; i < object_count; i++) {
        const EsheepSurfaceObject *obj = &objects[i];
        bool at_top = abs(bottom - obj->y) <= 2;
        bool overlapping = drop_landing_enabled &&
            top <= obj->y && bottom >= obj->y &&
            top < obj->y + obj->height;
        if ((at_top || overlapping) &&
            rects_overlap_x(pos_x, image_width, obj->x, obj->width)) {
            int priority = obj->taskbar ? 0 : 1;
            if (obj->stack_order > best_stack_order ||
                (obj->stack_order == best_stack_order && priority > (best_surface == ESHEEP_SURFACE_TASKBAR ? 1 : 0))) {
                best_stack_order = obj->stack_order;
                best_surface = obj->taskbar ? ESHEEP_SURFACE_TASKBAR : ESHEEP_SURFACE_WINDOW;
                best_y = obj->y;
                best_idx = i;
            }
        }
    }

    if (best_surface != ESHEEP_SURFACE_NONE) {
        out->valid = true;
        out->landed_y = best_y;
        out->surface = best_surface;
        out->object_index = best_idx;
        return true;
    }
    return false;
}

/* Swept landing detection for esheep_apply_motion.
 * Checks if the sprite crossed a surface top during its movement.
 */
static bool find_swept_landing_target(int pos_x, int previous_bottom, int current_bottom,
                                       int image_width,
                                       int object_count, const EsheepSurfaceObject *objects,
                                       bool window_landing_enabled, bool drop_landing_enabled,
                                       EsheepFallTarget *out) {
    if (!out) return false;

    int best_stack_order = -1;
    EsheepSurface best_surface = ESHEEP_SURFACE_NONE;
    int best_y = -1;
    int best_idx = -1;

    for (int i = 0; i < object_count; i++) {
        const EsheepSurfaceObject *obj = &objects[i];
        bool at_top = abs(current_bottom - obj->y) <= 2;
        bool crossing = previous_bottom <= obj->y &&
                        current_bottom >= obj->y &&
                        pos_x + image_width > obj->x &&  /* horizontal overlap */
                        pos_x < obj->x + obj->width;
        if ((!at_top && !(crossing &&
                          (window_landing_enabled || drop_landing_enabled))) ||
            !rects_overlap_x(pos_x, image_width, obj->x, obj->width)) continue;
        if (obj->stack_order >= best_stack_order) {
            best_stack_order = obj->stack_order;
            best_surface = obj->taskbar ? ESHEEP_SURFACE_TASKBAR : ESHEEP_SURFACE_WINDOW;
            best_y = obj->y;
            best_idx = i;
        }
    }

    if (best_surface != ESHEEP_SURFACE_NONE) {
        out->valid = true;
        out->landed_y = best_y;
        out->surface = best_surface;
        out->object_index = best_idx;
        return true;
    }
    return false;
}

const char *esheep_apply_motion(EsheepMotion *motion) {
    if (!motion) return "none";
    int previous_bottom = motion->pos_y + motion->image_height;
    motion->pos_x += motion->delta_x;
    motion->pos_y += motion->delta_y;
    const char *result = "none";
    bool landed = false;

    if (motion->delta_y > 0 &&
        (motion->window_landing_enabled || motion->drop_landing_enabled)) {
        int current_bottom = motion->pos_y + motion->image_height;
        EsheepFallTarget target;
        if (find_swept_landing_target(motion->pos_x, previous_bottom, current_bottom,
                                       motion->image_width,
                                       motion->object_count, motion->objects,
                                       motion->window_landing_enabled, motion->drop_landing_enabled,
                                       &target)) {
            motion->pos_y = target.landed_y - motion->image_height;
            motion->drop_landing_enabled = false;
            landed = true;
            result = target.surface == ESHEEP_SURFACE_TASKBAR ? "taskbar" : "window";
        }
    }

    int floor_y = motion->bounds_y + motion->bounds_height -
                  motion->image_height;
    bool hit_floor = false;
    if (motion->pos_y > floor_y) {
        motion->pos_y = floor_y;
        hit_floor = motion->delta_y > 0;
        if (hit_floor) motion->drop_landing_enabled = false;
    }
    if (motion->pos_y < motion->bounds_y) motion->pos_y = motion->bounds_y;
    if (landed) return result;
    if (hit_floor) return "horizontal+";
    if (motion->pos_x <= motion->bounds_x) {
        motion->pos_x = motion->bounds_x;
        return "vertical";
    }
    if (motion->pos_x + motion->image_width >=
        motion->bounds_x + motion->bounds_width) {
        motion->pos_x = motion->bounds_x + motion->bounds_width -
                        motion->image_width;
        return "vertical";
    }
    for (int i = 0; i < motion->object_count; i++) {
        const EsheepSurfaceObject *surface = &motion->objects[i];
        if (abs(motion->pos_y + motion->image_height - surface->y) <= 2 &&
            rects_overlap_x(motion->pos_x, motion->image_width,
                            surface->x, surface->width))
            return surface->taskbar ? "taskbar" : "window";
    }
    return result;
}

void esheep_classify_context(EsheepContext *ctx) {
    if (!ctx) return;

    /* When airborne (falling), prioritize landing surfaces over edges.
     * A falling sheep must continue falling until it hits a collision. */
    if (ctx->move == ESHEEP_MOVE_FALLING) {
        /* Ordinary falling honors the window-landing setting. An explicit
         * drag/drop landing remains enabled by its separate opt-in flag. Keep
         * this consistent with the detailed fall-target query because the GTK
         * tick loop uses this classifier before stepping. */
        if (!ctx->window_landing_enabled && !ctx->drop_landing_enabled) {
            ctx->surface = ESHEEP_SURFACE_FLOOR;
            ctx->surface_y = ctx->bounds_y + ctx->bounds_height;
            return;
        }
        EsheepFallTarget target;
        if (find_landing_target(ctx->pos_x, ctx->pos_y, ctx->image_width, ctx->image_height,
                                 ctx->object_count, ctx->objects,
                                 ctx->drop_landing_enabled,
                                 &target)) {
            ctx->surface = target.surface;
            ctx->surface_y = target.landed_y;
            ctx->move = ESHEEP_MOVE_FALLING;
            return;
        }

        /* No window/taskbar found below - fall through to the floor. */
        ctx->surface = ESHEEP_SURFACE_FLOOR;
        ctx->surface_y = ctx->bounds_y + ctx->bounds_height;
        ctx->move = ESHEEP_MOVE_FALLING;
        return;
    }

    /* Classify horizontal edges for non-falling sprites. A sprite at the left
     * edge has its left side (pos_x) flush with the monitor left edge (bounds_x).
     * The right edge is when the sprite's right side reaches or passes the
     * monitor's right edge. */
    if (ctx->pos_x <= ctx->bounds_x) {
        ctx->surface = ESHEEP_SURFACE_LEFT_EDGE;
        ctx->move = ESHEEP_MOVE_WALKING;
        return;
    }
    int right_edge = ctx->bounds_x + ctx->bounds_width - ctx->image_width;
    if (ctx->pos_x >= right_edge) {
        ctx->surface = ESHEEP_SURFACE_RIGHT_EDGE;
        ctx->move = ESHEEP_MOVE_WALKING;
        return;
    }

    /* Default: walking on the floor. */
    ctx->surface = ESHEEP_SURFACE_FLOOR;
    ctx->move = ESHEEP_MOVE_WALKING;
    ctx->surface_y = ctx->bounds_y + ctx->bounds_height;
}

const char *esheep_transition_context(const EsheepContext *ctx) {
    if (!ctx) return "none";
    if (ctx->surface == ESHEEP_SURFACE_LEFT_EDGE ||
        ctx->surface == ESHEEP_SURFACE_RIGHT_EDGE)
        return "vertical";
    if (ctx->move == ESHEEP_MOVE_FALLING &&
        ctx->surface == ESHEEP_SURFACE_WINDOW)
        return "window";
    if (ctx->move == ESHEEP_MOVE_FALLING &&
        ctx->surface == ESHEEP_SURFACE_TASKBAR)
        return "taskbar";
    return "none";
}

bool esheep_classify_fall(const EsheepContext *ctx, EsheepFallTarget *out) {
    if (!ctx || !out) return false;
    memset(out, 0, sizeof(*out));

    if (!ctx->window_landing_enabled || ctx->move != ESHEEP_MOVE_FALLING)
        return false;

    return find_landing_target(ctx->pos_x, ctx->pos_y, ctx->image_width, ctx->image_height,
                                 ctx->object_count, ctx->objects,
                                 ctx->drop_landing_enabled,
                                 out);
}

bool esheep_edges_tripped(const EsheepContext *ctx, bool *left, bool *right) {
    if (!ctx) return false;
    if (left) *left = false;
    if (right) *right = false;

    if (ctx->pos_x <= ctx->bounds_x) {
        if (left) *left = true;
        return true;
    }
    int right_edge = ctx->bounds_x + ctx->bounds_width - ctx->image_width;
    if (ctx->pos_x >= right_edge) {
        if (right) *right = true;
        return true;
    }
    return false;
}

bool esheep_apply_fall_through(const EsheepContext *ctx, int *new_pos_x,
                               int *new_pos_y) {
    if (!ctx || ctx->move != ESHEEP_MOVE_FALLING) return false;
    if (new_pos_x) *new_pos_x = ctx->pos_x;
    /* Land at floor: bottom of the display bounds minus sprite height. */
    int floor_y = ctx->bounds_y + ctx->bounds_height - ctx->image_height;
    if (new_pos_y) *new_pos_y = floor_y;
    return true;
}

bool esheep_authored_edge_reversal(const EsheepContext *ctx,
                                   int *next_direction,
                                   bool *edge_dispatched) {
    if (!ctx || !next_direction) return false;

    /* Do not re-dispatch an already-dispatched edge event in the same
     * tick sequence. */
    if (edge_dispatched && *edge_dispatched) return false;

    if (ctx->surface == ESHEEP_SURFACE_LEFT_EDGE) {
        /* At the left edge the authored animation expects positive delta.
         * After the turn animation runs, direction should point right (+1). */
        *next_direction = 1;
        if (edge_dispatched) *edge_dispatched = true;
        return true;
    }
    if (ctx->surface == ESHEEP_SURFACE_RIGHT_EDGE) {
        /* At the right edge the authored animation expects negative delta.
         * After the turn animation runs, direction should point left (-1). */
        *next_direction = -1;
        if (edge_dispatched) *edge_dispatched = true;
        return true;
    }
    return false;
}
