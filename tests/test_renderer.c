#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "renderer.h"
#include "interpreter.h"
#include "animations_data.h"

static void test_renderer_basic(void) {
    EsheepRenderer r;
    esheep_renderer_init(&r, 128, 64);
    assert(esheep_renderer_valid(&r));
    assert(r.surface_width == 128);
    assert(r.surface_height == 64);
    assert(r.count == 0);
}

static void test_renderer_compose_parent_only(void) {
    EsheepRenderer r;
    esheep_renderer_init(&r, 128, 64);
    int count = esheep_renderer_compose(&r, 5, 0, 1.0, true,
                                        0, NULL, NULL, NULL, NULL, NULL, NULL);
    assert(count == 1);
    assert(r.tiles[0].tile_id == 5);
    assert(r.tiles[0].x == 0 && r.tiles[0].y == 0);
    assert(r.tiles[0].width == 128 && r.tiles[0].height == 64);
    assert(r.tiles[0].opacity == 1.0);
    assert(r.tiles[0].flipped == false);
    assert(r.tiles[0].visible == true);
    assert(esheep_renderer_valid(&r));
}

static void test_renderer_compose_with_child(void) {
    EsheepRenderer r;
    esheep_renderer_init(&r, 128, 64);
    int child_tile_ids[1] = {14};
    int child_x[1] = {10};
    int child_y[1] = {20};
    int child_flipped[1] = {0};
    double child_opacity[1] = {0.8};
    bool child_visible[1] = {true};
    int count = esheep_renderer_compose(&r, 5, 0, 1.0, true,
                                        1, child_tile_ids, child_x, child_y,
                                        child_flipped, child_opacity, child_visible);
    assert(count == 2);
    assert(r.tiles[0].tile_id == 5);
    assert(r.tiles[0].visible == true);
    assert(r.tiles[1].tile_id == 14);
    assert(r.tiles[1].x == 10);
    assert(r.tiles[1].y == 20);
    assert(r.tiles[1].opacity == 0.8);
    assert(r.tiles[1].visible == true);
    assert(esheep_renderer_valid(&r));
}

static void test_renderer_compose_clears_unused_slots(void) {
    EsheepRenderer r;
    int tile_ids[2] = {14, 15};
    int x[2] = {10, 20};
    int y[2] = {20, 30};
    int flipped[2] = {0, 1};
    double opacity[2] = {0.8, 0.6};
    bool visible[2] = {true, true};

    esheep_renderer_init(&r, 128, 64);
    assert(esheep_renderer_compose(&r, 5, 0, 1.0, true,
                                   2, tile_ids, x, y,
                                   flipped, opacity, visible) == 3);
    assert(r.tiles[2].visible == true);

    assert(esheep_renderer_compose(&r, 6, 1, 1.0, true,
                                   0, NULL, NULL, NULL,
                                   NULL, NULL, NULL) == 1);
    assert(r.tiles[0].tile_id == 6);
    assert(r.tiles[1].tile_id == 0);
    assert(r.tiles[1].visible == false);
    assert(r.tiles[2].tile_id == 0);
    assert(r.tiles[2].visible == false);
    assert(esheep_renderer_valid(&r));
}

static void test_renderer_alpha_bounds_simple(void) {
    EsheepRenderer r;
    esheep_renderer_init(&r, 100, 100);
    int min_x, min_y, max_x, max_y;
    bool any = esheep_renderer_alpha_bounds(&r, 0.5, &min_x, &min_y, &max_x, &max_y);
    assert(!any);
    /* Parent tile 0 at (0,0) opacity 1.0, child tile 2 at (10,20) opacity 1.0 */
    int tile_ids[] = {2};
    int x[] = {10}, y[] = {20};
    int flipped[] = {0};
    double opacity[] = {1.0};
    bool visible[] = {true};
    esheep_renderer_compose(&r, 1, 0, 1.0, true, 1, tile_ids, x, y, flipped, opacity, visible);
    any = esheep_renderer_alpha_bounds(&r, 0.5, &min_x, &min_y, &max_x, &max_y);
    assert(any);
    /* Bounds include the complete child tile, even beyond the parent tile. */
    assert(min_x == 0 && min_y == 0 && max_x == 109 && max_y == 119);
}

static void test_renderer_alpha_bounds_transparent(void) {
    EsheepRenderer r;
    esheep_renderer_init(&r, 100, 100);
    /* Parent invisible, child opacity 0.2 < threshold 0.5 -> no bounds */
    int tile_ids[] = {8};
    int x[] = {30}, y[] = {40};
    int flipped[] = {0};
    double opacity[] = {0.2};
    bool visible[] = {true};
    esheep_renderer_compose(&r, 0, 0, 0.0, false, 1, tile_ids, x, y, flipped, opacity, visible);
    int min_x, min_y, max_x, max_y;
    bool any = esheep_renderer_alpha_bounds(&r, 0.5, &min_x, &min_y, &max_x, &max_y);
    assert(!any);
}

static void test_renderer_alpha_bounds_negative_offset(void) {
    EsheepRenderer r;
    esheep_renderer_init(&r, 100, 100);
    int tile_ids[] = {8};
    int x[] = {-10};
    int y[] = {20};
    int flipped[] = {0};
    double opacity[] = {1.0};
    bool visible[] = {true};
    int min_x, min_y, max_x, max_y;

    esheep_renderer_compose(&r, 0, 0, 0.0, false, 1, tile_ids, x, y,
                            flipped, opacity, visible);
    assert(esheep_renderer_alpha_bounds(&r, 0.5, &min_x, &min_y, &max_x, &max_y));
    assert(min_x == -10 && min_y == 20 && max_x == 89 && max_y == 119);
    assert(esheep_renderer_valid(&r));
}

static void test_renderer_valid_with_garbage_child_slots(void) {
    EsheepRenderer r;
    esheep_renderer_init(&r, 80, 60);
    r.count = 2;
    r.tiles[0].tile_id = 1;
    r.tiles[0].visible = true;
    r.tiles[1].tile_id = 2;
    r.tiles[1].visible = true;
    r.tiles[2].visible = true;
    assert(!esheep_renderer_valid(&r));
}

static void test_renderer_negative_child_offsets_are_valid(void) {
    EsheepRenderer r;
    int tile_ids[] = {31};
    int x[] = {-48};
    int y[] = {-5};
    int flipped[] = {1};
    double opacity[] = {1.2};
    bool visible[] = {true};

    esheep_renderer_init(&r, 40, 40);
    assert(esheep_renderer_compose(&r, 28, 1, -1.0, true, 1, tile_ids, x, y,
                                   flipped, opacity, visible) == 2);
    assert(r.tiles[0].opacity == 0.0);
    assert(r.tiles[1].x == -48);
    assert(r.tiles[1].y == -5);
    assert(r.tiles[1].opacity == 1.0);
    assert(r.tiles[1].flipped == true);
    assert(esheep_renderer_valid(&r));
}

static void test_renderer_capacity_reserves_parent_slot(void) {
    EsheepRenderer r;
    int tile_ids[ESHEEP_RENDER_MAX_CHILDREN];
    int x[ESHEEP_RENDER_MAX_CHILDREN];
    int y[ESHEEP_RENDER_MAX_CHILDREN];
    int flipped[ESHEEP_RENDER_MAX_CHILDREN];
    double opacity[ESHEEP_RENDER_MAX_CHILDREN];
    bool visible[ESHEEP_RENDER_MAX_CHILDREN];

    for (int i = 0; i < ESHEEP_RENDER_MAX_CHILDREN; i++) {
        tile_ids[i] = 100 + i;
        x[i] = i;
        y[i] = i * 2;
        flipped[i] = i % 2;
        opacity[i] = 1.0;
        visible[i] = true;
    }

    esheep_renderer_init(&r, 64, 64);
    assert(esheep_renderer_compose(&r, 5, 0, 1.0, true,
                                   ESHEEP_RENDER_MAX_CHILDREN,
                                   tile_ids, x, y, flipped, opacity, visible) ==
           ESHEEP_RENDER_MAX_CHILDREN);
    assert(r.count == ESHEEP_RENDER_MAX_CHILDREN);
    assert(r.tiles[ESHEEP_RENDER_MAX_CHILDREN - 1].tile_id ==
           tile_ids[ESHEEP_RENDER_MAX_CHILDREN - 2]);
    assert(esheep_renderer_valid(&r));
}

static void test_renderer_resize(void) {
    EsheepRenderer r;
    esheep_renderer_init(&r, 100, 100);
    esheep_renderer_compose(&r, 3, 0, 1.0, true, 0, NULL, NULL, NULL, NULL, NULL, NULL);
    esheep_renderer_resize(&r, 200, 150);
    assert(r.surface_width == 200 && r.surface_height == 150);
    assert(r.tiles[0].width == 200 && r.tiles[0].height == 150);
    assert(esheep_renderer_valid(&r));
}

static void test_composed_scene_basic(void) {
    EsheepRenderer r;
    esheep_renderer_init(&r, 64, 64);
    /* Parent tile 1; one child tile 14 at offset (20, 10) */
    int tile_ids[] = {14};
    int x[] = {20};
    int y[] = {10};
    int flipped[] = {0};
    double opacity[] = {0.9};
    bool visible[] = {true};
    int count = esheep_renderer_compose(&r, 1, 0, 1.0, true, 1,
                                        tile_ids, x, y, flipped, opacity, visible);
    assert(count == 2);
    assert(r.tiles[0].tile_id == 1);
    assert(r.tiles[1].tile_id == 14);
    assert(r.tiles[1].x == 20);
    assert(r.tiles[1].y == 10);
    assert(r.tiles[2].tile_id == 0);
    assert(r.tiles[2].visible == false);
}

static void test_composed_scene_clipping(void) {
    EsheepRenderer r;
    esheep_renderer_init(&r, 64, 64);
    int tile_ids[3] = {1, 2, 3};
    int x[] = {10, 80, 100};
    int y[] = {10, 30, 10};
    int flipped[] = {0, 0, 0};
    double opacity[] = {1.0, 1.0, 1.0};
    bool visible[] = {true, true, true};
    int count = esheep_renderer_compose(&r, 1, 0, 1.0, true, 3,
                                        tile_ids, x, y, flipped, opacity, visible);
    assert(count == 4);
    assert(r.tiles[1].x == 10 && r.tiles[1].y == 10);
    assert(r.tiles[2].x == 80 && r.tiles[2].y == 30);
    assert(r.tiles[3].x == 100 && r.tiles[3].y == 10);
}

int main(void) {
    test_renderer_basic();
    test_renderer_compose_parent_only();
    test_renderer_compose_with_child();
    test_renderer_compose_clears_unused_slots();
    test_renderer_alpha_bounds_simple();
    test_renderer_alpha_bounds_transparent();
    test_renderer_alpha_bounds_negative_offset();
    test_renderer_valid_with_garbage_child_slots();
    test_renderer_negative_child_offsets_are_valid();
    test_renderer_capacity_reserves_parent_slot();
    test_renderer_resize();
    test_composed_scene_basic();
    test_composed_scene_clipping();
    printf("All renderer tests passed\n");
    return 0;
}
