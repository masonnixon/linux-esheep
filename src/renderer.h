#ifndef ESHEEP_RENDERER_H
#define ESHEEP_RENDERER_H

#include "animations_data.h"
#include <stdbool.h>

#define ESHEEP_RENDER_MAX_CHILDREN 8

/* One composed tile record for the composited scene. Each record describes
 * one sprite layer (parent or child) with its geometry and appearance. All
 * coordinates are local to the parent surface origin, so child offsets may
 * be negative or extend past the surface bounds. */
typedef struct {
    int tile_id;           /* spritesheet tile index; 0 = no tile (unused slot) */
    int x, y;             /* top-left corner in the composed surface */
    int width, height;     /* tile dimensions in pixels */
    double opacity;        /* 0.0 .. 1.0 */
    bool flipped;          /* mirror horizontally */
    bool visible;          /* skip if false; unused slots must have visible=false */
} EsheepRenderTile;

/* One renderer owns one parent actor and all its descendants. The surface
 * dimensions are the parent actor's tile size. Tiles are stored in draw
 * order with the parent at index 0. Child offsets may extend beyond the
 * parent tile; the host is responsible for allocating the returned scene
 * bounds. */
typedef struct {
    int surface_width;     /* parent tile width; children are clipped here */
    int surface_height;    /* parent tile height; children are clipped here */
    int count;             /* total tile records, including the parent at 0 */
    EsheepRenderTile tiles[ESHEEP_RENDER_MAX_CHILDREN];
} EsheepRenderer;

/* Initialise a renderer for a parent tile of the given pixel dimensions.
 * Child slots are zeroed and invisible. */
void esheep_renderer_init(EsheepRenderer *r, int surface_width, int surface_height);

/* Build the scene for a parent actor (tile_id, x=0, y=0, flipped, opacity,
 * visible) and all its descendants. Each descendant's x/y is the authored
 * relative offset from the parent surface origin. Flipping and opacity come
 * from the descendant's own animation state. Child slots beyond count are
 * cleared and hidden. Returns the number of tile records written, up to
 * ESHEEP_RENDER_MAX_CHILDREN total including the parent. */
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

/* Compute the visible pixel bounding box of the complete composed scene,
 * including negative or out-of-surface child offsets. Bounds are local scene
 * coordinates and must be translated by the host when allocating a surface.
 * Returns true and fills bounds when any pixels are above the threshold;
 * returns false when the scene is empty or fully transparent. */
bool esheep_renderer_alpha_bounds(const EsheepRenderer *r,
                                 double opacity_threshold,
                                 int *out_min_x, int *out_min_y,
                                 int *out_max_x, int *out_max_y);

/* Verify internal consistency: count stays in range, used tiles stay in the
 * valid opacity and size range, and unused slots remain hidden. Negative
 * child offsets are valid because local tiles may hang off the parent. */
bool esheep_renderer_valid(const EsheepRenderer *r);

/* Resize the surface. Child positions are unchanged (they remain relative). */
void esheep_renderer_resize(EsheepRenderer *r, int surface_width, int surface_height);

#endif /* ESHEEP_RENDERER_H */
