#include "renderer.h"
#include <stddef.h>

void esheep_renderer_init(EsheepRenderer *r, int surface_width, int surface_height) {
    if (!r) return;
    if (surface_width < 1) surface_width = 1;
    if (surface_height < 1) surface_height = 1;
    r->surface_width = surface_width;
    r->surface_height = surface_height;
    r->count = 0;
    for (int i = 0; i < ESHEEP_RENDER_MAX_CHILDREN; i++) {
        r->tiles[i].tile_id = 0;
        r->tiles[i].x = 0;
        r->tiles[i].y = 0;
        r->tiles[i].width = 0;
        r->tiles[i].height = 0;
        r->tiles[i].opacity = 0.0;
        r->tiles[i].flipped = false;
        r->tiles[i].visible = false;
    }
}

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
                            const bool child_visible[]) {
    if (!r) return 0;
    if (parent_opacity < 0.0) parent_opacity = 0.0;
    if (parent_opacity > 1.0) parent_opacity = 1.0;
    r->count = 0;

    EsheepRenderTile *parent_tile = &r->tiles[r->count++];
    parent_tile->tile_id = parent_tile_id;
    parent_tile->x = 0;
    parent_tile->y = 0;
    parent_tile->width = r->surface_width;
    parent_tile->height = r->surface_height;
    parent_tile->opacity = parent_opacity;
    parent_tile->flipped = parent_flipped ? true : false;
    parent_tile->visible = parent_visible;

    if (child_count <= 0) return r->count;
    if (child_count > ESHEEP_RENDER_MAX_CHILDREN)
        child_count = ESHEEP_RENDER_MAX_CHILDREN;
    if (!child_tile_ids || !child_x || !child_y ||
        !child_flipped || !child_opacity || !child_visible) {
        return r->count;
    }

    for (int i = 0; i < child_count && r->count < ESHEEP_RENDER_MAX_CHILDREN; i++) {
        EsheepRenderTile *child_tile = &r->tiles[r->count++];
        child_tile->tile_id = child_tile_ids[i];
        child_tile->x = child_x[i];
        child_tile->y = child_y[i];
        child_tile->width = r->surface_width;
        child_tile->height = r->surface_height;
        child_tile->opacity = child_opacity[i];
        if (child_opacity[i] < 0.0) child_tile->opacity = 0.0;
        if (child_opacity[i] > 1.0) child_tile->opacity = 1.0;
        child_tile->flipped = child_flipped[i] ? true : false;
        child_tile->visible = child_visible[i];
    }
    return r->count;
}

bool esheep_renderer_alpha_bounds(const EsheepRenderer *r,
                                  double opacity_threshold,
                                  int *out_min_x, int *out_min_y,
                                  int *out_max_x, int *out_max_y) {
    if (!r) return false;
    int min_x = 0, min_y = 0, max_x = -1, max_y = -1;
    bool any = false;
    for (int i = 0; i < r->count; i++) {
        const EsheepRenderTile *tile = &r->tiles[i];
        if (!tile->visible) continue;
        if (tile->opacity < opacity_threshold) continue;
        if (tile->width <= 0 || tile->height <= 0) continue;
        int tile_min_x = tile->x;
        int tile_min_y = tile->y;
        int tile_max_x = tile->x + tile->width - 1;
        int tile_max_y = tile->y + tile->height - 1;
        if (!any) {
            min_x = tile_min_x;
            min_y = tile_min_y;
            max_x = tile_max_x;
            max_y = tile_max_y;
            any = true;
            continue;
        }
        if (tile_min_x < min_x) min_x = tile_min_x;
        if (tile_min_y < min_y) min_y = tile_min_y;
        if (tile_max_x > max_x) max_x = tile_max_x;
        if (tile_max_y > max_y) max_y = tile_max_y;
    }
    if (!any) return false;
    if (out_min_x) *out_min_x = min_x;
    if (out_min_y) *out_min_y = min_y;
    if (out_max_x) *out_max_x = max_x;
    if (out_max_y) *out_max_y = max_y;
    return true;
}

bool esheep_renderer_valid(const EsheepRenderer *r) {
    if (!r) return false;
    if (r->count < 0) return false;
    if (r->count > ESHEEP_RENDER_MAX_CHILDREN) return false;
    if (r->surface_width < 1 || r->surface_height < 1) return false;
    for (int i = 0; i < r->count; i++) {
        const EsheepRenderTile *tile = &r->tiles[i];
        if (tile->x < 0 || tile->y < 0) return false;
        if (tile->tile_id < 0) return false;
        if (tile->opacity < 0.0 || tile->opacity > 1.0) return false;
    }
    for (int i = r->count; i < ESHEEP_RENDER_MAX_CHILDREN; i++) {
        const EsheepRenderTile *tile = &r->tiles[i];
        if (tile->visible) return false;
    }
    return true;
}

void esheep_renderer_resize(EsheepRenderer *r, int surface_width, int surface_height) {
    if (!r) return;
    if (surface_width < 1) surface_width = 1;
    if (surface_height < 1) surface_height = 1;
    r->surface_width = surface_width;
    r->surface_height = surface_height;
    for (int i = 0; i < r->count; i++) {
        r->tiles[i].width = surface_width;
        r->tiles[i].height = surface_height;
    }
}
