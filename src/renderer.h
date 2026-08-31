#ifndef ESHEEP_RENDERER_H
#define ESHEEP_RENDERER_H

#include "animations_data.h"
#include <stdbool.h>

#define ESHEEP_RENDER_MAX_CHILDREN 8

/* One composed tile record for the composited scene. Each record describes
 * one sprite layer (parent or child) with its geometry and appearance. */
typedef struct {
    int tile_id;           /* spritesheet tile index; 0 = no tile (unused slot) */
    int x, y;             /* top-left corner in the composed surface */
    int width, height;     /* tile dimensions in pixels */
    double opacity;        /* 0.0 .. 1.0 */
    bool flipped;          /* mirror horizontally */
    bool visible;          /* skip if false; unused slots must have visible=false */
} EsheepRenderTile;

/* One renderer owns one parent actor and all its descendants. The surface
 * dimensions are the parent actor's tile size; children are clipped at the
 * surface boundary. Child tile positions are relative to the surface origin. */
typedef struct {
    int surface_width;     /* parent tile width; children are clipped here */
    int surface_height;    /* parent tile height; children are clipped here */
    int count;             /* 1 .. ESHEEP_RENDER_MAX_CHILDREN, 0 = empty */
    EsheepRenderTile tiles[ESHEEP_RENDER_MAX_CHILDREN];
} EsheepRenderer;

/* Initialise a renderer for a parent tile of the given pixel dimensions.
 * Child slots are zeroed and invisible. */
void esheep_renderer_init(EsheepRenderer *r, int surface_width, int surface_height);

/* Build the scene for a parent actor (tile_id, x=0, y=0, flipped, opacity,
 * visible) and all its descendants. Each descendant's x/y is the authored
 * relative offset from the parent surface origin. Flipping and opacity come
 * from the descendant's own animation state. Child slots beyond count are
 * untouched. Returns the number of tile records written (1 + descendant count).
 */
int esheep_renderer_compose(EsheepRenderer *r,
                            int parent_tile_id,
                            int parent_flipped,
                            double parent_opacity,
                            bool parent_visible,
                            int child_count,
                            const int child_tile_ids[],
                            const int child_x[],
                            const int child_y[],
                            const int child_flipped[],
                            const double child_opacity[],
                            const bool child_visible[]);

/* Compute the visible pixel bounding box of a composed scene, respecting
 * opacity thresholds. Returns true and fills bounds when any pixels are
 * above the given threshold; returns false when the scene is empty or
 * fully transparent. */
bool esheep_renderer_alpha_bounds(const EsheepRenderer *r,
                                 double opacity_threshold,
                                 int *out_min_x, int *out_min_y,
                                 int *out_max_x, int *out_max_y);

/* Verify internal consistency: count matches used slots, no index overflow.
 * Returns true when the renderer state is well-formed. */
bool esheep_renderer_valid(const EsheepRenderer *r);

/* Resize the surface. Child positions are unchanged (they remain relative). */
void esheep_renderer_resize(EsheepRenderer *r, int surface_width, int surface_height);

#endif /* ESHEEP_RENDERER_H */
