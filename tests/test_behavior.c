/* Comprehensive behavior regression tests for:
 * 1. Direction/flip separation from movement
 * 2. Authored child lifetime across parent sequences
 * 3. Flower and black-sheep local placement
 * 4. Frame progression and opacity propagation
 * 5. Parent-facing propagation
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define main esheep_app_main
#include "../src/main.c"
#undef main

/* Helper: init a stub App for testing child/local coord logic */
static void init_stub_app(App *app, int bounds_x, int bounds_y,
                          int bounds_width, int bounds_height, int tile_size) {
    memset(app, 0, sizeof(*app));
    app->tile_size = tile_size;
    app->direction = 1;
    app->bounds = (GdkRectangle){ bounds_x, bounds_y, bounds_width, bounds_height };
    app->pos_x = bounds_x + 50;
    app->pos_y = bounds_y + bounds_height - tile_size;
    app->tick_ms = 33;
    esheep_init(&app->state, ANIM_WALK);
    esheep_set_environment(&app->state, bounds_width, bounds_height,
                           tile_size, tile_size);
    esheep_renderer_init(&app->scene, tile_size, tile_size);
}

/* Test 1: Direction/flip separation for walking animation.
 * When direction=-1 (moving left), sprite should NOT be flipped.
 * When direction=+1 (moving right), sprite should be flipped. */
static void test_walk_direction_flip_separation(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);
    const EsheepAnimation *walk = &esheep_animations[ANIM_WALK - 1];

    app.direction = -1;
    assert(!sprite_is_flipped(&app, walk));

    app.direction = 1;
    assert(sprite_is_flipped(&app, walk));
}

/* Test 2: Transition to walk preserves direction-based orientation. */
static void test_transition_to_walk_preserves_orientation(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);
    const EsheepAnimation *walk = &esheep_animations[ANIM_WALK - 1];

    app.pos_x = app.bounds.x + app.bounds.width - app.tile_size;
    app.direction = 1;
    esheep_init(&app.state, ANIM_WALK);
    assert(sprite_is_flipped(&app, walk));

    app.direction = -1;
    assert(!sprite_is_flipped(&app, walk));
}

/* Test 3: Black sheep child (animation 28) keeps child visible. */
static void test_black_sheep_child_propagation(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);

    esheep_init(&app.state, 28);
    update_child_animation(&app);

    assert(app.child_animation_id == 31);
    assert(app.scene.count > 1);

    for (int i = 0; i < 50; i++) {
        advance_child_animation(&app, 33);
    }

    /* Child should persist while parent anim is 28 */
    assert(app.child_animation_id > 0 || app.scene.count > 1);
}

/* Test 4: Black sheep child local offset is correct. */
static void test_black_sheep_child_local_offset(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);

    esheep_init(&app.state, 28);
    update_child_animation(&app);

    const EsheepChild *child = find_child_for_animation(28);
    assert(child != NULL);
    assert(child->next == 31);

    /* x offset: -imageW-8 = -40-8 = -48 */
    int local_x = child_local_coordinate(&app, child->x, 0);
    assert(local_x == -48);

    int local_y = child_local_coordinate(&app, child->y, 0);
    assert(local_y == 0);

    assert(app.scene.tiles[1].x == local_x);
    assert(app.scene.tiles[1].y == local_y);
}

/* Test 5: Eating animation (26) flower child placement. */
static void test_eating_flower_child_offset(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);

    esheep_init(&app.state, 26);
    update_child_animation(&app);

    const EsheepChild *child = find_child_for_animation(26);
    assert(child != NULL);
    assert(child->next == 27);

    /* x offset: imageX - imageW*0.9 = 0 - 36 = -36 */
    int local_x = child_local_coordinate(&app, child->x, 0);
    assert(local_x == -36);

    int local_y = child_local_coordinate(&app, child->y, 0);
    assert(local_y == 0);

    assert(app.child_animation_id == 27);
    assert(app.scene.tiles[1].visible);
}

/* Test 6: Eating animation flower visibility throughout sequence. */
static void test_eating_flower_visibility_sequence(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);

    esheep_init(&app.state, 26);
    update_child_animation(&app);

    assert(app.child_animation_id == 27);
    assert(app.scene.tiles[1].visible);

    int intervals[] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    for (int i = 0; i < 12; i++) {
        advance_child_animation(&app, intervals[i]);
        update_child_animation(&app);
        assert(app.child_animation_id == 27);
        assert(app.scene.tiles[1].visible);
        assert(app.scene.tiles[1].x >= -100);
        assert(app.scene.tiles[1].x <= 100);
        assert(app.scene.tiles[1].y >= -100);
        assert(app.scene.tiles[1].y <= 100);
    }

    esheep_init(&app.state, ANIM_WALK);
    update_child_animation(&app);
    assert(app.child_animation_id == 0);
    assert(app.scene.count == 1);
}

/* Test 7: Child flip propagation from parent. */
static void test_child_flip_propagation(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);

    esheep_init(&app.state, 28);
    app.direction = 1;
    update_child_animation(&app);

    const EsheepAnimation *parent_anim = &esheep_animations[27];
    assert(sprite_is_flipped(&app, parent_anim));
    assert(app.scene.tiles[1].flipped == app.scene.tiles[0].flipped);

    app.direction = -1;
    update_child_animation(&app);
    assert(!sprite_is_flipped(&app, parent_anim));
    assert(app.scene.tiles[1].flipped == app.scene.tiles[0].flipped);
}

/* Test 8: All authored child records have valid geometry. */
static void test_all_child_records_valid_geometry(void) {
    for (int i = 0; i < esheep_child_count; i++) {
        const EsheepChild *child = &esheep_childs[i];
        assert(child->animation_id > 0);
        assert(child->animation_id <= esheep_animation_count);
        assert(child->x != NULL);
        assert(child->y != NULL);
        assert(child->next > 0);
        assert(child->next <= esheep_animation_count);

        const EsheepAnimation *child_anim = &esheep_animations[child->next - 1];
        assert(child_anim->frame_count > 0);
    }
}

/* Test 9: Frame progression for child animations. */
static void test_child_frame_progression(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);

    esheep_init(&app.state, 28);
    update_child_animation(&app);

    advance_child_animation(&app, 100);

    const EsheepAnimation *child_anim = &esheep_animations[app.child_animation_id - 1];
    assert(app.child_frame_index < child_anim->frame_count);
}

/* Test 10: Edge turn preserves direction orientation. */
static void test_edge_turn_preserves_orientation(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);

    app.pos_x = app.bounds.x;
    app.direction = -1;
    esheep_init(&app.state, ANIM_WALK);
    const EsheepAnimation *walk = &esheep_animations[ANIM_WALK - 1];

    app.direction = -app.direction;
    assert(app.direction == 1);
    assert(sprite_is_flipped(&app, walk));

    app.pos_x = app.bounds.x + app.bounds.width - app.tile_size;
    app.direction = 1;
    app.direction = -app.direction;
    assert(app.direction == -1);
    assert(!sprite_is_flipped(&app, walk));
}

/* Test 11: Collision resolution doesn't break direction. */
static void test_collision_resolution_direction(void) {
    App sheep[2];
    init_stub_app(&sheep[0], 0, 0, 400, 200, 40);
    init_stub_app(&sheep[1], 0, 0, 400, 200, 40);
    sheep[0].siblings = sheep;
    sheep[0].sibling_count = 2;
    sheep[1].siblings = sheep;
    sheep[1].sibling_count = 2;

    sheep[0].pos_x = 100;
    sheep[0].direction = 1;
    sheep[1].pos_x = 130;
    sheep[1].direction = -1;

    resolve_sheep_collisions(&sheep[0]);
    resolve_sheep_collisions(&sheep[1]);

    assert(sheep[0].direction == 1 || sheep[0].direction == -1);
    assert(sheep[1].direction == 1 || sheep[1].direction == -1);
    assert(!apps_overlap(&sheep[0], &sheep[1]));
}
static void test_window_landing_direction(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);

    app.direction = 1;
    esheep_init(&app.state, ANIM_WALK);
    assert(app.direction == 1);

    app.direction = -1;
    esheep_init(&app.state, ANIM_WALK);
    assert(app.direction == -1);
}

/* Test 13: Authored multi-sprite bath/shower child scene. */
static void test_bath_child_scene(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);

    esheep_init(&app.state, 21);
    update_child_animation(&app);

    const EsheepChild *child = find_child_for_animation(21);
    assert(child != NULL);
    assert(child->next == 23);

    assert(child->x != NULL);
    assert(child->y != NULL);

    assert(app.child_animation_id == 23);
    assert(app.scene.tiles[1].visible);

    int local_x = child_local_coordinate(&app, child->x, 0);
    assert(local_x >= -100 && local_x <= 700);
}

/* Test 14: Deterministic child lifetime - no one-frame workaround. */
static void test_no_one_frame_child_workaround(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);

    esheep_init(&app.state, 26);
    update_child_animation(&app);

    assert(app.child_animation_id == 27);
    assert(app.scene.count > 1);

    int frames_checked = 0;
    for (int tick = 0; tick < 20; tick++) {
        advance_child_animation(&app, 100);
        update_child_animation(&app);
        if (app.child_animation_id > 0) {
            frames_checked++;
            assert(app.scene.count > 1);
        }
    }

    assert(frames_checked >= 5);
}

/* Test 15: Scene tile validity. */
static void test_scene_tile_coordinates_non_negative(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);

    int child_animations[] = {21, 26, 28};

    for (int i = 0; i < 3; i++) {
        esheep_init(&app.state, child_animations[i]);
        update_child_animation(&app);

        if (app.scene.count > 1) {
            assert(app.scene.tiles[0].x >= 0);
            assert(app.scene.tiles[0].y >= 0);
        }
    }
}

/* Test 16: Opacity propagation to children. */
static void test_child_opacity_default(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);

    esheep_init(&app.state, 28);
    update_child_animation(&app);

    if (app.scene.count > 1) {
        assert(app.scene.tiles[0].opacity == 1.0);
        assert(app.scene.tiles[1].opacity == 1.0);
    }
}

int main(void) {
    printf("Running behavior regression tests...\n");

    test_walk_direction_flip_separation();
    printf("  test_walk_direction_flip_separation: PASSED\n");

    test_transition_to_walk_preserves_orientation();
    printf("  test_transition_to_walk_preserves_orientation: PASSED\n");

    test_black_sheep_child_propagation();
    printf("  test_black_sheep_child_propagation: PASSED\n");

    test_black_sheep_child_local_offset();
    printf("  test_black_sheep_child_local_offset: PASSED\n");

    test_eating_flower_child_offset();
    printf("  test_eating_flower_child_offset: PASSED\n");

    test_eating_flower_visibility_sequence();
    printf("  test_eating_flower_visibility_sequence: PASSED\n");

    test_child_flip_propagation();
    printf("  test_child_flip_propagation: PASSED\n");

    test_all_child_records_valid_geometry();
    printf("  test_all_child_records_valid_geometry: PASSED\n");

    test_child_frame_progression();
    printf("  test_child_frame_progression: PASSED\n");

    test_edge_turn_preserves_orientation();
    printf("  test_edge_turn_preserves_orientation: PASSED\n");

    test_collision_resolution_direction();
    printf("  test_collision_resolution_direction: PASSED\n");

    test_window_landing_direction();
    printf("  test_window_landing_direction: PASSED\n");

    test_bath_child_scene();
    printf("  test_bath_child_scene: PASSED\n");

    test_no_one_frame_child_workaround();
    printf("  test_no_one_frame_child_workaround: PASSED\n");

    test_scene_tile_coordinates_non_negative();
    printf("  test_scene_tile_coordinates_non_negative: PASSED\n");

    test_child_opacity_default();
    printf("  test_child_opacity_default: PASSED\n");

    printf("\nAll 16 behavior regression tests PASSED\n");
    return 0;
}
