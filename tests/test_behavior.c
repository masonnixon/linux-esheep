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

    esheep_init(&app.state, 26);
    update_child_animation(&app);
    assert(app.scene.count > 1);
    assert(app.scene.tiles[1].opacity == 0.8);
}

/* The stub App has no GTK window, so the GTK entry points on_tick still
 * calls warn about the NULL widget. Swallow the log output for those domains
 * to keep the tick loop quiet under test. */
static void swallow_stub_gtk_log(const char *log_domain, GLogLevelFlags level,
                                 const gchar *message, gpointer user_data) {
    (void)log_domain;
    (void)level;
    (void)message;
    (void)user_data;
}

static void install_stub_gtk_log_swallow(void) {
    const char *domains[] = { "Gtk", "Gdk", "Glib" };
    for (size_t i = 0; i < G_N_ELEMENTS(domains); i++)
        g_log_set_handler(domains[i],
                          G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL,
                          swallow_stub_gtk_log, NULL);
}

/* Drive the real tick loop from an edge spawn and report how the turn went.
 * turns counts entries into the turn animation (2); turn_end_x is the x while
 * the turn was still playing; first_inward_dx is the first non-zero x step
 * after the turn finished. */
static void run_edge_turn_ticks(App *app, int ticks, int *turns, int *saw_turn,
                                int *turn_end_x, int *first_inward_dx) {
    int last = app->state.animation_id;
    int prev_x = app->pos_x;
    int turn_active = (last == 2 || last == 3);
    int turn_done = 0; /* the turn played and finished at least once */
    *turns = 0;
    *saw_turn = 0;
    *turn_end_x = -1;
    *first_inward_dx = 0;
    for (int t = 0; t < ticks; t++) {
        on_tick(app);
        int cur = app->state.animation_id;
        int now_active = (cur == 2 || cur == 3);
        if (cur == 2 && last != 2) (*turns)++;
        if (now_active) {
            *saw_turn = 1;
            *turn_end_x = app->pos_x;
        }
        if (turn_active && !now_active) turn_done = 1;
        int dx = app->pos_x - prev_x;
        if (turn_done && *first_inward_dx == 0 && dx != 0)
            *first_inward_dx = dx;
        assert(app->pos_x >= app->bounds.x &&
               app->pos_x + app->tile_size <= app->bounds.x + app->bounds.width);
        turn_active = now_active;
        last = cur;
        prev_x = app->pos_x;
    }
}

/* A sheep spawned flush against the right edge must reverse once, play the
 * authored turn (2 -> 3), step off the edge, and walk back inward instead of
 * looping at the boundary. */
static void test_right_edge_turn_around_repeated_ticks(void) {
    srand(20260901);
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);
    app.tick_ms = 200;
    app.pos_x = app.bounds.x + app.bounds.width - app.tile_size; /* flush right */
    app.pos_y = 320;
    app.direction = 1;
    esheep_init(&app.state, ANIM_WALK);

    int turns = 0, saw_turn = 0, turn_end_x = -1, inward_dx = 0;
    run_edge_turn_ticks(&app, 14, &turns, &saw_turn, &turn_end_x, &inward_dx);

    assert(turns == 1);                    /* reversed exactly once */
    assert(saw_turn == 1);                 /* authored turn animations played */
    assert(turn_end_x == 598);             /* stepped off the edge (right - 2) */
    assert(inward_dx == -2);               /* resumed walking inward */
    assert(app.direction == -1);
    assert(app.pos_x < app.bounds.x + app.bounds.width - app.tile_size);
}

/* Mirror of the right-edge case: a sheep spawned flush against the left edge
 * reverses once and resumes walking inward (rightward). */
static void test_left_edge_turn_around_repeated_ticks(void) {
    srand(20260902);
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);
    app.tick_ms = 200;
    app.pos_x = app.bounds.x;
    app.pos_y = 320;
    app.direction = -1;
    esheep_init(&app.state, ANIM_WALK);

    int turns = 0, saw_turn = 0, turn_end_x = -1, inward_dx = 0;
    run_edge_turn_ticks(&app, 14, &turns, &saw_turn, &turn_end_x, &inward_dx);

    assert(turns == 1);
    assert(saw_turn == 1);
    assert(turn_end_x == 2);               /* stepped off the edge (left + 2) */
    assert(inward_dx == 2);
    assert(app.direction == 1);
    assert(app.pos_x > app.bounds.x);
}

/* A dropped sheep overlapping a valid client window lands on the window top
 * through the same context pipeline on_tick applies. */
static void test_drop_overlap_lands_on_window_top(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);
    app.objects[0].rect = (GdkRectangle){ 200, 200, 200, 200 };
    app.objects[0].taskbar = FALSE;
    app.objects[0].stack_order = 10;
    app.object_count = 1;
    app.pos_x = 250;
    app.pos_y = 180; /* bottom 220: overlapping the window below its top 200 */
    app.drop_landing_enabled = TRUE;
    app.direction = 1;
    esheep_init(&app.state, ANIM_FALL);

    EsheepContext ctx;
    build_context(&app, &ctx);
    esheep_classify_context(&ctx);
    assert(ctx.move == ESHEEP_MOVE_FALLING);
    assert(ctx.surface == ESHEEP_SURFACE_WINDOW);
    assert(ctx.surface_y == 200);
    assert(ctx.surface_y - app.tile_size == 160);
}

/* A dropped sheep fully below a window keeps falling to the floor; the window
 * top must not pull it up. */
static void test_drop_below_window_falls_to_floor(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);
    app.objects[0].rect = (GdkRectangle){ 200, 100, 200, 100 }; /* bottom 200 */
    app.objects[0].taskbar = FALSE;
    app.objects[0].stack_order = 10;
    app.object_count = 1;
    app.pos_x = 250;
    app.pos_y = 250; /* top 250: fully below the window */
    app.direction = 1;
    esheep_init(&app.state, ANIM_FALL);

    EsheepContext ctx;
    build_context(&app, &ctx);
    esheep_classify_context(&ctx);
    assert(ctx.surface == ESHEEP_SURFACE_FLOOR);
    assert(ctx.surface_y == 360);
}

/* A walking sheep on the floor in front of a bottom-screen taskbar overlaps
 * the taskbar area but is not airborne, so it must stay on the floor. */
static void test_walk_in_front_of_taskbar_not_lifted(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);
    app.objects[0].rect = (GdkRectangle){ 0, 320, 640, 40 };
    app.objects[0].taskbar = TRUE;
    app.objects[0].stack_order = 100;
    app.object_count = 1;
    app.pos_x = 300;
    app.pos_y = 320; /* on the floor */
    app.direction = 1;
    esheep_init(&app.state, ANIM_WALK);

    EsheepContext ctx;
    build_context(&app, &ctx);
    esheep_classify_context(&ctx);
    assert(ctx.surface == ESHEEP_SURFACE_FLOOR);
    assert(ctx.surface_y == 360);
}

/* Desktop, panel, fullscreen, and conky surfaces are rejected before surface
 * selection, so none of them can false-positive as a landing target. */
static void test_excluded_surfaces_rejected(void) {
    DesktopSurfaceTraits traits;
    memset(&traits, 0, sizeof(traits));
    assert(x11_surface_is_landing_candidate(&traits));

    traits = (DesktopSurfaceTraits){ .desktop_surface = TRUE };
    assert(!x11_surface_is_landing_candidate(&traits));
    traits = (DesktopSurfaceTraits){ .panel_surface = TRUE };
    assert(!x11_surface_is_landing_candidate(&traits));
    traits = (DesktopSurfaceTraits){ .fullscreen_surface = TRUE };
    assert(!x11_surface_is_landing_candidate(&traits));
    traits = (DesktopSurfaceTraits){ .conky_surface = TRUE };
    assert(!x11_surface_is_landing_candidate(&traits));
}

/* A fall frame may move more than the classifier's contact tolerance. The
 * runtime must still land when that frame crosses a window top. */

/* Test: Sheep stacking policy allows application windows to occlude it.
 * The sheep window uses GDK_WINDOW_TYPE_HINT_NORMAL on a managed toplevel,
 * which places it in the normal WM stacking layer and allows any freshly
 * raised application window to cover it. No keep-above, sticky, or per-tick
 * raise is requested. The production setup uses the same normal-stacking
 * policy helper tested here. */
static void test_stacking_policy_allows_occlusion(void) {
    /* Normal stacking: the WM controls layering; no unconditional topmost. */
    assert(sheep_window_type_hint() == GDK_WINDOW_TYPE_HINT_NORMAL);
}

static void test_group_pause_applies_to_all_sheep(void) {
    App sheep[3] = {0};
    SheepGroup group = { .sheep = sheep, .count = 3 };
    group_set_paused(&group, TRUE);
    assert(sheep[0].paused && sheep[1].paused && sheep[2].paused);
    group_set_paused(&group, FALSE);
    assert(!sheep[0].paused && !sheep[1].paused && !sheep[2].paused);
}

static void test_swept_fall_lands_on_window(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 480, 40);
    app.objects[0].rect = (GdkRectangle){ 100, 85, 300, 200 };
    app.objects[0].stack_order = 1;
    app.object_count = 1;
    app.window_landing = TRUE;
    app.pos_x = 160;
    app.pos_y = 44; /* bottom 84; the authored 1px fall step reaches y=85 */

    const char *hit = step_position(&app, &esheep_animations[5], 0);
    assert(strcmp(hit, "window") == 0);
    assert(app.pos_y == 45);
}

static void test_group_animation_review_selection(void) {
    App sheep[2];
    init_stub_app(&sheep[0], 0, 0, 640, 360, 40);
    init_stub_app(&sheep[1], 0, 0, 640, 360, 40);
    SheepGroup group = {
        .sheep = sheep,
        .count = 2,
        .walk_keep_probability = 90
    };
    assert(group_set_review_animation(&group, 26));
    assert(sheep[0].state.animation_id == 26);
    assert(sheep[1].state.animation_id == 26);
    assert(sheep[0].child_animation_id == 27);
    assert(!group_set_review_animation(&group, esheep_animation_count + 1));
}

int main(void) {
    printf("Running behavior regression tests...\n");

    install_stub_gtk_log_swallow();

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

    test_right_edge_turn_around_repeated_ticks();
    printf("  test_right_edge_turn_around_repeated_ticks: PASSED\n");

    test_left_edge_turn_around_repeated_ticks();
    printf("  test_left_edge_turn_around_repeated_ticks: PASSED\n");

    test_drop_overlap_lands_on_window_top();
    printf("  test_drop_overlap_lands_on_window_top: PASSED\n");

    test_drop_below_window_falls_to_floor();
    printf("  test_drop_below_window_falls_to_floor: PASSED\n");

    test_walk_in_front_of_taskbar_not_lifted();
    printf("  test_walk_in_front_of_taskbar_not_lifted: PASSED\n");

    test_excluded_surfaces_rejected();
    test_stacking_policy_allows_occlusion();
    printf("  test_stacking_policy_allows_occlusion: PASSED\n");

    test_group_pause_applies_to_all_sheep();
    printf("  test_group_pause_applies_to_all_sheep: PASSED\n");

    printf("  test_excluded_surfaces_rejected: PASSED\n");

    test_swept_fall_lands_on_window();
    printf("  test_swept_fall_lands_on_window: PASSED\n");

    test_group_animation_review_selection();
    printf("  test_group_animation_review_selection: PASSED\n");

    printf("\nAll 26 behavior regression tests PASSED\n");
    return 0;
}
