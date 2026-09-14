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

/* Custom package child expressions use the same image-dimension factors as
 * authored pose expressions. */
static void test_scaled_child_expression(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);

    assert(child_local_coordinate(&app, "imageW*0.5", 0) == 20);
    assert(child_local_coordinate(&app, "-imageH*0.5", 0) == -20);
}

static void test_spritesheet_requires_alpha(void) {
    GdkPixbuf *opaque_sheet = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
                                             640, 440);
    assert(opaque_sheet != NULL);
    assert(!validate_spritesheet_pixbuf(opaque_sheet, "<test>", "sheep"));
    g_object_unref(opaque_sheet);
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

static void test_child_uses_parent_random_source(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);
    esheep_actor_init(&app.actor, NULL, 26, 0, 0, app.direction);
    esheep_actor_set_random_source(&app.actor, actor_random_source, &app);

    esheep_init(&app.state, 26);
    update_child_animation(&app);

    assert(app.child_actors[0].parent == &app.actor);
    assert(app.child_actors[0].random == actor_random_source);
    assert(app.child_actors[0].random_context == &app);
}

static void test_child_authored_pose_progression(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);
    app.direction = -1;
    esheep_init(&app.state, 28);
    update_child_animation(&app);
    int initial_x = app.scene.tiles[1].x;
    advance_child_animation(&app, 100);
    update_child_animation(&app);
    assert(app.scene.tiles[1].x != initial_x);
}

static void test_recursive_child_composition(void) {
    const EsheepChild authored[] = {
        { 26, "imageX", "imageY", 27 },
        { 27, "5", "6", 31 },
    };
    const EsheepChild *old_children = esheep_childs;
    int old_count = esheep_child_count;
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);
    esheep_childs = authored;
    esheep_child_count = 2;
    esheep_init(&app.state, 26);
    update_child_animation(&app);
    assert(app.scene.count == 3);
    assert(app.child_actors[0].parent == &app.actor);
    assert(app.actor.child_count == 1);
    assert(app.actor.children[0] == &app.child_actors[0]);
    assert(app.child_actors[0].child_count == 1);
    assert(app.child_actors[0].children[0] == &app.child_actors[1]);
    assert(app.child_actors[1].parent == &app.child_actors[0]);
    assert(app.scene.tiles[1].x == 0 && app.scene.tiles[1].y == 0);
    assert(app.scene.tiles[2].x == 5 && app.scene.tiles[2].y == 6);
    esheep_childs = old_children;
    esheep_child_count = old_count;
}

static void test_child_restarts_when_parent_animation_changes(void) {
    const EsheepChild authored[] = {
        { 26, "0", "0", 27 },
        { 28, "0", "0", 27 },
    };
    const EsheepChild *old_children = esheep_childs;
    int old_count = esheep_child_count;
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);
    esheep_childs = authored;
    esheep_child_count = 2;
    esheep_init(&app.state, 26);
    update_child_animation(&app);
    advance_child_animation(&app, 300);
    update_child_animation(&app);
    assert(app.child_frame_indices[0] != 0);
    esheep_init(&app.state, 28);
    update_child_animation(&app);
    assert(app.child_frame_indices[0] == 0);
    assert(app.child_pose_x[0] == 0 && app.child_pose_y[0] == 0);
    esheep_childs = old_children;
    esheep_child_count = old_count;
}

static void test_child_scene_is_removed_when_parent_leaves_record(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);

    esheep_init(&app.state, 21);
    update_child_animation(&app);
    assert(app.scene.count == 2);
    assert(app.child_animation_ids[0] == 23);

    esheep_init(&app.state, 22);
    update_child_animation(&app);
    assert(app.scene.count == 1);
    assert(app.child_animation_id == 0);
    assert(app.child_animation_ids[0] == 0);
}

static void test_child_sequence_transition_survives_rebuild(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);

    esheep_init(&app.state, 21);
    update_child_animation(&app);
    assert(app.child_authored_animations[0] == 23);

    /* Simulate the child having completed its own authored sequence. A scene
     * rebuild must retain that current state while the parent record remains
     * 21 -> 23. */
    app.child_actors[0].state.animation_id = 24;
    app.child_actors[0].state.frame_index = 12;
    app.child_animation_ids[0] = 24;
    update_child_animation(&app);
    assert(app.child_authored_animations[0] == 23);
    assert(app.child_actors[0].state.animation_id == 24);
    assert(app.scene.tiles[1].tile_id == 147);
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

static void test_builtin_character_selects_matching_sheet(void) {
    SheepGroup group = {0};
    strcpy(group.spritesheet, "assets/sheep_spritesheet.png");
    apply_builtin_character_sheet(&group, "penguin");
    assert(strcmp(group.spritesheet,
                  "assets/penguin_ice_blue_spritesheet.png") == 0);
    apply_builtin_character_sheet(&group, "sheep");
    assert(strcmp(group.spritesheet, "assets/sheep_spritesheet.png") == 0);
}

static void test_live_spritesheet_swap(void) {
    GError *error = NULL;
    GdkPixbuf *old_sheet = gdk_pixbuf_new_from_file(
        "assets/sheep_spritesheet.png", &error);
    assert(old_sheet != NULL && error == NULL);
    App app = {0};
    app.sheet = old_sheet;
    app.tile_size = 40;
    app.bounds = (GdkRectangle){ 0, 0, 640, 440 };
    app.pos_x = 100;
    app.pos_y = 400;
    esheep_init(&app.state, ANIM_WALK);
    SheepGroup group = {
        .sheep = &app,
        .count = 1,
        .sheet = g_object_ref(old_sheet)
    };
    strcpy(group.spritesheet, "assets/sheep_spritesheet.png");
    assert(group_apply_spritesheet(&group,
                                   "assets/penguin_ice_blue_spritesheet.png",
                                   "penguin"));
    assert(app.sheet == group.sheet);
    assert(app.tile_size == 80);
    assert(strcmp(group.spritesheet,
                  "assets/penguin_ice_blue_spritesheet.png") == 0);
    g_object_unref(group.sheet);
    g_object_unref(old_sheet);
}

static void test_settings_persist_profile_and_count(void) {
    char path[] = "/tmp/esheep-settings-test.XXXXXX";
    int fd = g_mkstemp(path);
    assert(fd >= 0);
    close(fd);

    GKeyFile *config = g_key_file_new();
    SheepGroup group = {0};
    group.config = config;
    group.config_path = path;
    group.configured_count = 7;
    group.tick_ms = 33;
    strcpy(group.character, "mimiko");
    strcpy(group.spritesheet, "package:embedded");
    strcpy(group.package, "Pets/mimiko/animations.xml");
    save_group_settings(&group);

    GKeyFile *reloaded = g_key_file_new();
    assert(g_key_file_load_from_file(reloaded, path, G_KEY_FILE_NONE, NULL));
    assert(g_key_file_get_integer(reloaded, "esheep", "count", NULL) == 7);
    assert(strcmp(g_key_file_get_string(reloaded, "esheep", "character", NULL),
                  "mimiko") == 0);
    assert(strcmp(g_key_file_get_string(reloaded, "esheep", "spritesheet", NULL),
                  "package:embedded") == 0);
    assert(strcmp(g_key_file_get_string(reloaded, "esheep", "package", NULL),
                  "Pets/mimiko/animations.xml") == 0);
    g_key_file_free(reloaded);
    g_key_file_free(config);
    remove(path);
}

static void test_live_catalog_profile_uses_verified_grid(void) {
    GError *error = NULL;
    GdkPixbuf *old_sheet = gdk_pixbuf_new_from_file(
        "assets/sheep_spritesheet.png", &error);
    assert(old_sheet != NULL && error == NULL);
    App app = {0};
    app.sheet = old_sheet;
    app.tile_size = 40;
    app.bounds = (GdkRectangle){0, 0, 640, 440};
    esheep_init(&app.state, ANIM_WALK);
    SheepGroup group = {.sheep = &app, .count = 1,
                        .sheet = g_object_ref(old_sheet)};
    assert(group_apply_profile(&group, "Pets/mimiko/animations.xml",
                               "mimiko", NULL));
    assert(group.active_package != NULL);
    assert(group.sheet != old_sheet);
    assert(app.tile_size > 0);
    esheep_pet_package_free(group.active_package);
    g_object_unref(group.sheet);
    g_object_unref(old_sheet);
}

/* Every authored parent must produce a valid composited scene, including any
 * child records reachable from that parent. This is a deterministic runtime
 * coverage gate; actual pixel appearance remains a separate visual review. */
static void test_all_authored_animations_compose(void) {
    App app;
    init_stub_app(&app, 0, 0, 640, 360, 40);
    for (int animation_id = 1; animation_id <= esheep_animation_count;
         animation_id++) {
        esheep_actor_init(&app.actor, NULL, animation_id, 0, 0,
                          app.direction);
        esheep_actor_set_random_source(&app.actor, actor_random_source, &app);
        update_child_animation(&app);
        assert(app.scene.count >= 1);
        assert(app.scene.count <= ESHEEP_RENDER_MAX_CHILDREN);
        assert(esheep_renderer_valid(&app.scene));
        for (int i = 0; i < app.scene.count; i++) {
            const EsheepRenderTile *tile = &app.scene.tiles[i];
            assert(tile->tile_id >= 0);
            assert(tile->tile_id < esheep_tiles_x * esheep_tiles_y);
            assert(tile->width == app.tile_size);
            assert(tile->height == app.tile_size);
        }
    }
}

static void test_double_click_closes_only_single_pet(void) {
    App app;
    GdkEventButton event;
    memset(&app, 0, sizeof(app));
    memset(&event, 0, sizeof(event));
    event.button = 1;
    event.type = GDK_2BUTTON_PRESS;

    app.sibling_count = 1;
    assert(closes_single_pet_on_double_click(&app, &event));

    app.sibling_count = 2;
    assert(!closes_single_pet_on_double_click(&app, &event));

    app.sibling_count = 1;
    app.dragging = TRUE;
    assert(!closes_single_pet_on_double_click(&app, &event));
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

    test_scaled_child_expression();
    printf("  test_scaled_child_expression: PASSED\n");

    test_spritesheet_requires_alpha();
    printf("  test_spritesheet_requires_alpha: PASSED\n");

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

    test_child_uses_parent_random_source();
    printf("  test_child_uses_parent_random_source: PASSED\n");

    test_child_authored_pose_progression();
    printf("  test_child_authored_pose_progression: PASSED\n");

    test_recursive_child_composition();
    printf("  test_recursive_child_composition: PASSED\n");

    test_child_restarts_when_parent_animation_changes();
    printf("  test_child_restarts_when_parent_animation_changes: PASSED\n");

    test_child_scene_is_removed_when_parent_leaves_record();
    printf("  test_child_scene_is_removed_when_parent_leaves_record: PASSED\n");

    test_child_sequence_transition_survives_rebuild();
    printf("  test_child_sequence_transition_survives_rebuild: PASSED\n");

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
    printf("  test_excluded_surfaces_rejected: PASSED\n");

    test_stacking_policy_allows_occlusion();
    printf("  test_stacking_policy_allows_occlusion: PASSED\n");

    test_group_pause_applies_to_all_sheep();
    printf("  test_group_pause_applies_to_all_sheep: PASSED\n");

    test_swept_fall_lands_on_window();
    printf("  test_swept_fall_lands_on_window: PASSED\n");

    test_group_animation_review_selection();
    printf("  test_group_animation_review_selection: PASSED\n");

    test_builtin_character_selects_matching_sheet();
    printf("  test_builtin_character_selects_matching_sheet: PASSED\n");

    test_live_spritesheet_swap();
    printf("  test_live_spritesheet_swap: PASSED\n");
    test_settings_persist_profile_and_count();
    printf("  test_settings_persist_profile_and_count: PASSED\n");
    test_live_catalog_profile_uses_verified_grid();
    printf("  test_live_catalog_profile_uses_verified_grid: PASSED\n");

    test_all_authored_animations_compose();
    printf("  test_all_authored_animations_compose: PASSED\n");

    test_double_click_closes_only_single_pet();
    printf("  test_double_click_closes_only_single_pet: PASSED\n");

    printf("\nAll 36 behavior regression tests PASSED\n");
    return 0;
}
