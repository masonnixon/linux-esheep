#include "context.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static inline bool rects_overlap_x(int x1, int w1, int x2, int w2) {
    return x1 < x2 + w2 && x1 + w1 > x2;
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
        int bottom = ctx->pos_y + ctx->image_height;
        int top = ctx->pos_y;
        int best_stack_order = -1;
        EsheepSurface best_surface = ESHEEP_SURFACE_NONE;
        int best_y = -1;

        for (int i = 0; i < ctx->object_count; i++) {
            const EsheepSurfaceObject *obj = &ctx->objects[i];
            /* A surface supports the sprite when its top is within 2 pixels of
             * the sprite bottom (the existing conky/panel tolerance), or, for
             * a dropped sprite, when the sprite still overlaps the surface
             * vertically and snaps up to its top. A sprite fully below the
             * surface never matches, so a drop past a window keeps falling to
             * the floor instead of teleporting onto it. */
            bool at_top = abs(bottom - obj->y) <= 2;
            bool overlapping = ctx->drop_landing_enabled &&
                top <= obj->y && bottom >= obj->y &&
                top < obj->y + obj->height;
            if ((at_top || overlapping) &&
                rects_overlap_x(ctx->pos_x, ctx->image_width, obj->x, obj->width)) {
                int priority = obj->taskbar ? 0 : 1;
                if (obj->stack_order > best_stack_order ||
                    (obj->stack_order == best_stack_order && priority > (best_surface == ESHEEP_SURFACE_TASKBAR ? 1 : 0))) {
                    best_stack_order = obj->stack_order;
                    best_surface = obj->taskbar ? ESHEEP_SURFACE_TASKBAR : ESHEEP_SURFACE_WINDOW;
                    best_y = obj->y;
                }
            }
        }

        if (best_surface != ESHEEP_SURFACE_NONE) {
            ctx->surface = best_surface;
            ctx->surface_y = best_y;
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

bool esheep_classify_fall(const EsheepContext *ctx, EsheepFallTarget *out) {
    if (!ctx || !out) return false;
    memset(out, 0, sizeof(*out));

    if (!ctx->window_landing_enabled || ctx->move != ESHEEP_MOVE_FALLING)
        return false;

    int bottom = ctx->pos_y + ctx->image_height;
    int top = ctx->pos_y;
    int best_stack_order = -1;
    EsheepSurface best_surface = ESHEEP_SURFACE_NONE;
    int best_y = -1;
    int best_idx = -1;

    for (int i = 0; i < ctx->object_count; i++) {
        const EsheepSurfaceObject *obj = &ctx->objects[i];
        bool at_top = abs(bottom - obj->y) <= 2;
        bool overlapping = ctx->drop_landing_enabled &&
            top <= obj->y && bottom >= obj->y &&
            top < obj->y + obj->height;
        if ((at_top || overlapping) &&
            rects_overlap_x(ctx->pos_x, ctx->image_width, obj->x, obj->width)) {
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
