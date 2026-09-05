#include <assert.h>
#include <stdio.h>
#include <string.h>

#define main esheep_app_main
#include "../src/main.c"
#undef main

static const EsheepChild *find_child_test_record(void) {
    for (int i = 0; i < esheep_child_count; i++) {
        const EsheepChild *child = &esheep_childs[i];
        if (child->next >= 1 && child->next <= esheep_animation_count &&
            esheep_animations[child->next - 1].frame_count > 1)
            return child;
    }
    return esheep_child_count > 0 ? &esheep_childs[0] : NULL;
}

static void init_stub_app(App *app, App *siblings, int sibling_count, int ordinal,
                          int bounds_x, int bounds_y, int bounds_width,
                          int bounds_height, int tile_size) {
    memset(app, 0, sizeof(*app));
    app->siblings = siblings;
    app->sibling_count = sibling_count;
    app->ordinal = ordinal;
    app->tile_size = tile_size;
    app->tick_ms = TICK_MS;
    app->direction = ordinal % 2 == 0 ? -1 : 1;
    app->window_landing = TRUE;
    app->exclude_conky = TRUE;
    app->bounds = (GdkRectangle){ bounds_x, bounds_y, bounds_width, bounds_height };
    app->pos_y = floor_pos_y(app);
    esheep_init(&app->state, ANIM_WALK);
    esheep_set_environment(&app->state, bounds_width, bounds_height,
                           tile_size, tile_size);
    esheep_renderer_init(&app->scene, tile_size, tile_size);
}

static void test_count_bounds(void) {
    assert(clamp_sheep_count(0) == 1);
    assert(clamp_sheep_count(1) == 1);
    assert(clamp_sheep_count(7) == 7);
    assert(clamp_sheep_count(MAX_SHEEP + 9) == MAX_SHEEP);
}

static void test_group_tick_scales_without_timer_multiplication(void) {
    const int counts[] = { 1, 5, 10 };
    for (int case_index = 0; case_index < 3; case_index++) {
        int count = counts[case_index];
        App sheep[MAX_SHEEP] = {0};
        SheepGroup group = { .sheep = sheep, .count = (guint)count };
        for (int i = 0; i < count; i++) {
            sheep[i].paused = TRUE;
            sheep[i].tick_ms = TICK_MS;
            sheep[i].group = &group;
        }

        for (int tick = 0; tick < 3; tick++)
            assert(group_tick(&group) == G_SOURCE_CONTINUE);

        /* One callback services the group. This catches a benchmark that
         * only measures equivalent output while leaving one timer per sheep. */
        assert(group.group_tick_count == 3);
        assert(group.sheep_tick_count == (guint)(3 * count));
        for (int i = 0; i < count; i++)
            assert(sheep[i].tick_source_id == 0);
    }
}

static void test_sheep_random_streams_are_independent(void) {
    App sheep[2];
    memset(sheep, 0, sizeof(sheep));
    sheep[0].ordinal = 0;
    sheep[1].ordinal = 1;
    (void)app_random_0_99(&sheep[0]);
    (void)app_random_0_99(&sheep[1]);
    assert(sheep[0].random_state != sheep[1].random_state);
}

static void test_bath_scene_publication_survives_child_transition(void) {
    App app;
    memset(&app, 0, sizeof(app));
    app.tile_size = 40;
    app.direction = 1;
    app.bounds = (GdkRectangle){ 0, 0, 1920, 1080 };
    esheep_init(&app.state, 21); /* batha; authored child is bathw */
    esheep_renderer_init(&app.scene, app.tile_size, app.tile_size);

    update_child_animation(&app);
    assert(app.scene.count == 2);
    assert(app.scene_changed);

    /* An unchanged rebuild must not keep requesting work forever. */
    app.scene_changed = FALSE;
    update_child_animation(&app);
    assert(!app.scene_changed);

    /* This is the second rebuild performed by on_tick after advancing the
     * child actor. A real transition must leave a publication request
     * pending. */
    EsheepRenderer before_transition = app.scene;
    esheep_init(&app.state, 23); /* bathw */
    update_child_animation(&app);
    assert(app.scene.count == 1);
    assert(!renderers_equal(&before_transition, &app.scene));
    assert(app.scene_changed);
}

static void test_seed_reproduces_a_sheep_stream(void) {
    App sheep;
    uint32_t saved_seed = app_random_seed;
    memset(&sheep, 0, sizeof(sheep));
    sheep.ordinal = 3;
    app_random_seed = 12345;
    int first = app_random_0_99(&sheep);
    int second = app_random_0_99(&sheep);
    sheep.random_state = 0;
    assert(app_random_0_99(&sheep) == first);
    assert(app_random_0_99(&sheep) == second);
    app_random_seed = saved_seed;
}

static void test_monitor_seam_selection(void) {
    const GdkRectangle monitors[] = {
        { -1920, 0, 1920, 1080 },
        { 0, 200, 1280, 880 },
        { 1280, 0, 1920, 1080 },
    };
    assert(select_monitor_index(monitors, 3, &monitors[0], -1900, 500, -1) == 0);
    assert(select_monitor_index(monitors, 3, &monitors[0], 1, 300, 1) == 1);
    assert(select_monitor_index(monitors, 3, &monitors[1], 1279, 300, 1) == 1);
    assert(select_monitor_index(monitors, 3, &monitors[1], 1281, 300, 1) == 2);
}

static void test_spawn_spacing_on_monitor(void) {
    App sheep[3];
    for (int i = 0; i < 3; i++) {
        init_stub_app(&sheep[i], sheep, 3, i, 1000, 40, 480, 220, 64);
        configure_initial_spawn(&sheep[i]);
        assert(sheep[i].pos_x >= 1000);
        assert(sheep[i].pos_x + sheep[i].tile_size <= 1480);
        assert(sheep[i].pos_y == 40 + 220 - 64);
    }
    assert(sheep[0].pos_x < sheep[1].pos_x);
    assert(sheep[1].pos_x < sheep[2].pos_x);
    assert(!apps_overlap(&sheep[0], &sheep[1]));
    assert(!apps_overlap(&sheep[1], &sheep[2]));
    assert(!apps_overlap(&sheep[0], &sheep[2]));
}

static void test_window_spawn_skips_taskbar_and_overlap(void) {
    App sheep[2];
    int expected_y[2];

    for (int i = 0; i < 2; i++) {
        init_stub_app(&sheep[i], sheep, 2, i, 800, 20, 500, 260, 64);
        sheep[i].spawn_on_window = TRUE;
        sheep[i].object_count = 3;
        sheep[i].objects[0].rect = (GdkRectangle){ 800, 216, 500, 64 };
        sheep[i].objects[0].taskbar = TRUE;
        sheep[i].objects[1].rect = (GdkRectangle){ 840, 120, 200, 80 };
        sheep[i].objects[1].taskbar = FALSE;
        sheep[i].objects[1].stack_order = 1;
        sheep[i].objects[2].rect = (GdkRectangle){ 1060, 90, 180, 90 };
        sheep[i].objects[2].taskbar = FALSE;
        sheep[i].objects[2].stack_order = 2;
        configure_initial_spawn(&sheep[i]);
    }
    expected_y[0] = 120 - sheep[0].tile_size;
    expected_y[1] = 90 - sheep[0].tile_size;
    assert(sheep[0].pos_y != 216 - sheep[0].tile_size);
    assert(sheep[1].pos_y != 216 - sheep[1].tile_size);
    assert((sheep[0].pos_y == expected_y[0] || sheep[0].pos_y == expected_y[1]));
    assert((sheep[1].pos_y == expected_y[0] || sheep[1].pos_y == expected_y[1]));
    assert(sheep[0].pos_y != sheep[1].pos_y);
    assert(!apps_overlap(&sheep[0], &sheep[1]));
}

/* Group sheep share one desktop snapshot. This headless test pins the
 * consume/invalidation rules: a generation is applied once per sheep,
 * mutations of a consumed generation are invisible, and fullscreen
 * suppression is judged against each sheep's own monitor. */
static void test_shared_snapshot_consumption(void) {
    App sheep[5];
    DesktopSnapshot snapshot = {0};
    for (int i = 0; i < 5; i++) {
        init_stub_app(&sheep[i], sheep, 5, i, 0, 0, 640, 360, 64);
        sheep[i].shared_snapshot = &snapshot;
    }

    /* No X display in this binary: a due deadline must not rescan or
     * corrupt, and an unconsumed snapshot leaves the sheep unchanged. */
    desktop_snapshot_tick(&sheep[0]);
    assert(snapshot.refresh_count == 0);
    assert(sheep[0].object_count == 0);
    for (int i = 0; i < 5; i++) desktop_snapshot_consume(&sheep[i], &snapshot);
    for (int i = 0; i < 5; i++) assert(sheep[i].object_count == 0);

    /* One completed generation: all five sheep consume the same surfaces. */
    snapshot.objects[0].rect = (GdkRectangle){ 12, 34, 400, 80 };
    snapshot.objects[0].stack_order = 3;
    snapshot.object_count = 1;
    snapshot.valid = TRUE;
    snapshot.refresh_count = 1;
    for (int i = 0; i < 5; i++) desktop_snapshot_consume(&sheep[i], &snapshot);
    for (int i = 0; i < 5; i++) {
        assert(sheep[i].object_count == 1);
        assert(sheep[i].objects[0].rect.x == 12);
        assert(sheep[i].objects[0].stack_order == 3);
    }

    /* Mutating the generation the sheep already consumed is invisible until
     * a new generation is published. */
    snapshot.objects[0].rect.x = 999;
    for (int i = 0; i < 5; i++) desktop_snapshot_consume(&sheep[i], &snapshot);
    for (int i = 0; i < 5; i++) assert(sheep[i].objects[0].rect.x == 12);

    snapshot.refresh_count = 2;
    for (int i = 0; i < 5; i++) desktop_snapshot_consume(&sheep[i], &snapshot);
    for (int i = 0; i < 5; i++) assert(sheep[i].objects[0].rect.x == 999);

    /* Fullscreen suppression is per sheep: only monitors actually covered
     * by the fullscreen surface are suppressed. */
    snapshot.fullscreen_rects[0] = (GdkRectangle){ 0, 0, 640, 360 };
    snapshot.fullscreen_count = 1;
    snapshot.refresh_count = 3;
    desktop_snapshot_consume(&sheep[0], &snapshot);
    assert(sheep[0].fullscreen_suppressed);
    sheep[1].bounds = (GdkRectangle){ 1000, 0, 640, 360 };
    desktop_snapshot_consume(&sheep[1], &snapshot);
    assert(!sheep[1].fullscreen_suppressed);
}

/* Restack target selection must run on cached snapshot data alone. This
 * headless test pins the filters (stale, own, sibling, desktop,
 * unviewable, non-overlapping) and the highest-stack-order-wins rule
 * without any X11 access. */
static void test_restack_selection_uses_cached_data_only(void) {
    DesktopSnapshot snapshot = {0};
    snapshot.restack_targets[0] = (RestackTarget){
        .client = 101, .stack_window = 201,
        .rect = (GdkRectangle){ 0, 0, 100, 100 },
        .viewable = TRUE, .desktop = FALSE, .stack_order = 0 };
    snapshot.restack_targets[1] = (RestackTarget){
        .client = 102, .stack_window = 202,
        .rect = (GdkRectangle){ 20, 20, 100, 100 },
        .viewable = TRUE, .desktop = FALSE, .stack_order = 2 };
    snapshot.restack_targets[2] = (RestackTarget){
        .client = 103, .stack_window = 203,
        .rect = (GdkRectangle){ 20, 20, 100, 100 },
        .viewable = TRUE, .desktop = TRUE, .stack_order = 9 };
    snapshot.restack_targets[3] = (RestackTarget){
        .client = 104, .stack_window = None,
        .rect = (GdkRectangle){ 20, 20, 100, 100 },
        .viewable = TRUE, .desktop = FALSE, .stack_order = 9 };
    snapshot.restack_targets[4] = (RestackTarget){
        .client = 105, .stack_window = 205,
        .rect = (GdkRectangle){ 500, 500, 10, 10 },
        .viewable = TRUE, .desktop = FALSE, .stack_order = 9 };
    snapshot.restack_targets[5] = (RestackTarget){
        .client = 106, .stack_window = 206,
        .rect = (GdkRectangle){ 20, 20, 100, 100 },
        .viewable = FALSE, .desktop = FALSE, .stack_order = 9 };
    snapshot.restack_target_count = 6;
    snapshot.own_clients[0] = 107;
    snapshot.own_stack[0] = 207;
    snapshot.own_count = 1;

    App sheep;
    memset(&sheep, 0, sizeof(sheep));
    sheep.xwindow = 107;
    sheep.pos_x = 30;
    sheep.pos_y = 30;
    sheep.tile_size = 32;

    /* Desktop, stale, unviewable, and non-overlapping entries carry the
     * highest stack order but must lose to the best valid entry. */
    assert(restack_select_cached_target(&sheep, &snapshot) == 202);

    /* A sheep whose own root-level window is unknown (or that has no
     * window at all) never restacks. */
    sheep.xwindow = 999;
    assert(restack_select_cached_target(&sheep, &snapshot) == None);
    sheep.xwindow = 0;
    assert(restack_select_cached_target(&sheep, &snapshot) == None);

    /* A sibling's window is never an occluder, even with the best
     * remaining stack order. */
    sheep.xwindow = 107;
    App sibling;
    memset(&sibling, 0, sizeof(sibling));
    sibling.xwindow = 102;
    sheep.siblings = &sibling;
    sheep.sibling_count = 1;
    assert(restack_select_cached_target(&sheep, &snapshot) == 201);

    /* If the sheep's own client ever appears in the target list it is
     * skipped as well. */
    sheep.siblings = NULL;
    sheep.sibling_count = 0;
    sheep.xwindow = 101;
    snapshot.own_clients[0] = 101;
    snapshot.own_stack[0] = 211;
    assert(restack_select_cached_target(&sheep, &snapshot) == 202);
}

static void test_independent_child_instances(void) {
    const EsheepChild *child = find_child_test_record();
    App sheep[2];

    assert(child != NULL);
    init_stub_app(&sheep[0], sheep, 2, 0, 0, 0, 640, 360, 64);
    init_stub_app(&sheep[1], sheep, 2, 1, 0, 0, 640, 360, 64);

    sheep[0].pos_x = 120;
    sheep[0].pos_y = 180;
    sheep[0].child_frame_index = 1;
    esheep_init(&sheep[0].state, child->animation_id);

    sheep[1].pos_x = 260;
    sheep[1].pos_y = 180;
    esheep_init(&sheep[1].state, ANIM_WALK);

    update_child_animation(&sheep[0]);
    update_child_animation(&sheep[1]);

    assert(sheep[0].child_animation_id == child->next);
    assert(sheep[0].scene.count > 1);
    assert(sheep[1].child_animation_id == 0);
    assert(sheep[1].scene.count == 1);

    int interval = frame_interval(&esheep_animations[child->next - 1],
                                  sheep[0].child_frame_index);
    advance_child_animation(&sheep[0], interval + 1);
    assert(sheep[0].child_frame_index != 1 || sheep[0].child_elapsed_ms == 0);
    assert(sheep[1].child_frame_index == 0);
    assert(sheep[1].child_elapsed_ms == 0);
}

static void test_collision_breaks_deadlock(void) {
    App sheep[2];
    int start_x0 = 140;
    int start_x1 = 170;

    init_stub_app(&sheep[0], sheep, 2, 0, 0, 0, 400, 200, 64);
    init_stub_app(&sheep[1], sheep, 2, 1, 0, 0, 400, 200, 64);
    sheep[0].pos_x = start_x0;
    sheep[1].pos_x = start_x1;
    sheep[0].direction = 1;
    sheep[1].direction = -1;

    resolve_sheep_collisions(&sheep[0]);
    resolve_sheep_collisions(&sheep[1]);

    assert(!apps_overlap(&sheep[0], &sheep[1]));
    assert(sheep[0].state.animation_id == 2 || sheep[1].state.animation_id == 2);
    assert(sheep[0].pos_x != start_x0 || sheep[1].pos_x != start_x1);
}

static void test_unresolved_edge_overlap_turns_inward(void) {
    App sheep[2];
    init_stub_app(&sheep[0], sheep, 2, 0, 0, 0, 128, 200, 64);
    init_stub_app(&sheep[1], sheep, 2, 1, 0, 0, 128, 200, 64);
    sheep[0].pos_x = 0;
    sheep[1].pos_x = 1;
    sheep[0].direction = -1;
    sheep[1].direction = -1;
    resolve_sheep_collisions(&sheep[0]);
    assert(sheep[0].direction == 1);

    sheep[0].pos_x = 64;
    sheep[1].pos_x = 63;
    sheep[0].direction = 1;
    sheep[1].direction = 1;
    resolve_sheep_collisions(&sheep[0]);
    assert(sheep[0].direction == -1);
}

static void test_falling_sheep_can_land_on_grounded_sheep(void) {
    App sheep[2];
    init_stub_app(&sheep[0], sheep, 2, 0, 0, 0, 400, 300, 40);
    init_stub_app(&sheep[1], sheep, 2, 1, 0, 0, 400, 300, 40);
    sheep[0].pos_x = 100;
    sheep[0].pos_y = 120;
    sheep[1].pos_x = 100;
    sheep[1].pos_y = 160;
    esheep_init(&sheep[0].state, ANIM_FALL);
    esheep_init(&sheep[1].state, ANIM_WALK);

    const char *hit = step_position(&sheep[0], &esheep_animations[4], 0);
    assert(strcmp(hit, "window") == 0);
    assert(sheep[0].pos_y == 120);
    assert(!apps_overlap(&sheep[0], &sheep[1]));
}

static void test_drag_and_fall_isolation(void) {
    App sheep[2];
    int dragged_x;
    int dragged_y;

    init_stub_app(&sheep[0], sheep, 2, 0, 0, 0, 420, 220, 64);
    init_stub_app(&sheep[1], sheep, 2, 1, 0, 0, 420, 220, 64);

    sheep[0].pos_x = 150;
    sheep[0].pos_y = 80;
    sheep[0].dragging = TRUE;
    esheep_init(&sheep[0].state, ANIM_DRAG);

    sheep[1].pos_x = 175;
    sheep[1].pos_y = 92;
    sheep[1].direction = -1;
    esheep_init(&sheep[1].state, ANIM_FALL);

    dragged_x = sheep[0].pos_x;
    dragged_y = sheep[0].pos_y;
    resolve_sheep_collisions(&sheep[1]);

    assert(sheep[0].dragging);
    assert(sheep[0].pos_x == dragged_x);
    assert(sheep[0].pos_y == dragged_y);
    assert(sheep[0].state.animation_id == ANIM_DRAG);
    assert(sheep[1].state.animation_id == ANIM_FALL);
    assert(!apps_overlap(&sheep[0], &sheep[1]));
}

static void test_child_input_owner_is_parent_only(void) {
    const EsheepChild *child = find_child_test_record();
    App sheep[2];

    assert(child != NULL);
    init_stub_app(&sheep[0], sheep, 2, 0, 0, 0, 640, 360, 64);
    init_stub_app(&sheep[1], sheep, 2, 1, 0, 0, 640, 360, 64);
    esheep_init(&sheep[0].state, child->animation_id);
    update_child_animation(&sheep[0]);

    assert(child_tiles_use_parent_input(&sheep[0]));
    assert(sheep[0].scene.count > 1);
    assert(sheep[0].window == NULL);
    assert(sheep[1].window == NULL);
}

static void test_cleanup_is_per_instance(void) {
    App sheep[2];

    init_stub_app(&sheep[0], sheep, 2, 0, 0, 0, 640, 360, 64);
    init_stub_app(&sheep[1], sheep, 2, 1, 0, 0, 640, 360, 64);

    sheep[0].object_count = 2;
    sheep[0].dragging = TRUE;
    sheep[0].child_animation_id = 9;
    sheep[0].child_frame_index = 3;
    sheep[0].child_elapsed_ms = 120;
    sheep[0].scene.count = 2;
    sheep[1].pos_x = 222;
    sheep[1].pos_y = 111;

    cleanup_app(&sheep[0]);

    assert(sheep[0].cleaned_up);
    assert(sheep[0].object_count == 0);
    assert(!sheep[0].dragging);
    assert(sheep[0].child_animation_id == 0);
    assert(sheep[0].child_frame_index == 0);
    assert(sheep[0].child_elapsed_ms == 0);
    assert(sheep[0].scene.count == 0);
    assert(!sheep[1].cleaned_up);
    assert(sheep[1].pos_x == 222);
    assert(sheep[1].pos_y == 111);
}

int main(void) {
    test_count_bounds();
    test_group_tick_scales_without_timer_multiplication();
    test_sheep_random_streams_are_independent();
    test_bath_scene_publication_survives_child_transition();
    test_seed_reproduces_a_sheep_stream();
    test_monitor_seam_selection();
    test_spawn_spacing_on_monitor();
    test_window_spawn_skips_taskbar_and_overlap();
    test_shared_snapshot_consumption();
    test_restack_selection_uses_cached_data_only();
    test_independent_child_instances();
    test_collision_breaks_deadlock();
    test_unresolved_edge_overlap_turns_inward();
    test_falling_sheep_can_land_on_grounded_sheep();
    test_drag_and_fall_isolation();
    test_child_input_owner_is_parent_only();
    test_cleanup_is_per_instance();
    printf("All multi-sheep tests passed\n");
    return 0;
}
