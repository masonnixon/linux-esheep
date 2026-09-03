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

static void test_sheep_random_streams_are_independent(void) {
    App sheep[2];
    memset(sheep, 0, sizeof(sheep));
    sheep[0].ordinal = 0;
    sheep[1].ordinal = 1;
    (void)app_random_0_99(&sheep[0]);
    (void)app_random_0_99(&sheep[1]);
    assert(sheep[0].random_state != sheep[1].random_state);
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
    test_sheep_random_streams_are_independent();
    test_monitor_seam_selection();
    test_spawn_spacing_on_monitor();
    test_window_spawn_skips_taskbar_and_overlap();
    test_independent_child_instances();
    test_collision_breaks_deadlock();
    test_drag_and_fall_isolation();
    test_child_input_owner_is_parent_only();
    test_cleanup_is_per_instance();
    printf("All multi-sheep tests passed\n");
    return 0;
}
