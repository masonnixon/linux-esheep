#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "context.h"

static void test_left_edge_reversal(void) {
    fprintf(stderr, "test: left edge reversal\n");
    EsheepContext ctx = {0};
    ctx.pos_x = 0;
    ctx.bounds_x = 0;
    ctx.image_width = 40;
    
    ctx.surface = ESHEEP_SURFACE_LEFT_EDGE;
    int next_dir = 0;
    bool dispatched = false;
    bool handled = esheep_authored_edge_reversal(&ctx, &next_dir, &dispatched);
    assert(handled == true);
    assert(next_dir == 1);
    /* First call sets dispatched=true per contract */
    assert(dispatched == true);
    
    /* Second call with dispatch already true should not re-dispatch */
    bool handled2 = esheep_authored_edge_reversal(&ctx, &next_dir, &dispatched);
    assert(handled2 == false);
    assert(next_dir == 1);  /* direction unchanged */
}

static void test_right_edge_reversal(void) {
    fprintf(stderr, "test: right edge reversal\n");
    EsheepContext ctx = {0};
    ctx.pos_x = 1920;   /* At right edge assuming bounds 0-1920 */
    ctx.bounds_x = 0;
    ctx.bounds_width = 1920;
    ctx.image_width = 40;
    
    ctx.surface = ESHEEP_SURFACE_RIGHT_EDGE;
    int next_dir = 0;
    bool dispatched = false;
    bool handled = esheep_authored_edge_reversal(&ctx, &next_dir, &dispatched);
    assert(handled == true);
    assert(next_dir == -1);
    assert(dispatched == true);
}

static void test_repeated_edge_ticks(void) {
    fprintf(stderr, "test: repeated edge ticks prevent re-dispatch\n");
    EsheepContext ctx = {0};
    ctx.pos_x = 0;
    ctx.bounds_x = 0;
    ctx.image_width = 40;
    ctx.surface = ESHEEP_SURFACE_LEFT_EDGE;
    
    int next_dir = 5;  /* sentinel */
    bool dispatched = false;
    
    /* First tick - dispatches and reverses */
    bool handled1 = esheep_authored_edge_reversal(&ctx, &next_dir, &dispatched);
    assert(handled1 == true);
    assert(next_dir == 1);
    assert(dispatched == true);
    
    /* Second tick - should not re-dispatch since dispatched is true */
    int saved_dir = next_dir;
    bool handled2 = esheep_authored_edge_reversal(&ctx, &next_dir, &dispatched);
    assert(handled2 == false);
    assert(next_dir == saved_dir);  /* direction unchanged */
}

static void test_floor_landing(void) {
    fprintf(stderr, "test: floor landing\n");
    EsheepContext ctx = {0};
    ctx.pos_x = 100;
    ctx.pos_y = 500;
    ctx.image_width = 40;
    ctx.image_height = 40;
    ctx.bounds_x = 0;
    ctx.bounds_y = 0;
    ctx.bounds_width = 1920;
    ctx.bounds_height = 1080;
    ctx.move = ESHEEP_MOVE_FALLING;
    ctx.object_count = 0;
    ctx.window_landing_enabled = false;
    
    EsheepFallTarget target = {0};
    bool found = esheep_classify_fall(&ctx, &target);
    assert(found == false);  /* no landing surface */
    
    int new_y = -999;
    bool applied = esheep_apply_fall_through(&ctx, NULL, &new_y);
    assert(applied == true);
    /* Floor is at bounds_y + bounds_height = 1080 */
    /* Sheep should land at 1080 - 40 = 1040 */
    assert(new_y == 1040);

    /* The contact classifier must honor the same disabled-window setting. */
    EsheepSurfaceObject window = {
        .x = 50, .y = 200, .width = 400, .height = 300,
        .stack_order = 5, .taskbar = false
    };
    ctx.object_count = 1;
    ctx.objects = &window;
    ctx.pos_x = 100;
    ctx.pos_y = 160;
    esheep_classify_context(&ctx);
    assert(ctx.surface == ESHEEP_SURFACE_FLOOR);
}

static void test_window_landing(void) {
    fprintf(stderr, "test: window landing\n");
    EsheepSurfaceObject windows[2] = {
        { .x = 50, .y = 200, .width = 400, .height = 300, .stack_order = 5, .taskbar = false },
        { .x = 0, .y = 100, .width = 1920, .height = 50, .stack_order = 10, .taskbar = true }
    };
    
    EsheepContext ctx = {0};
    ctx.pos_x = 70;   /* centered within window [50, 50+400] */
    ctx.pos_y = 530;  /* bottom at 570, window top at 200, difference 370 (> 2 tolerance) */
    ctx.image_width = 40;
    ctx.image_height = 40;
    ctx.bounds_x = 0;
    ctx.bounds_y = 0;
    ctx.bounds_width = 1920;
    ctx.bounds_height = 1080;
    ctx.move = ESHEEP_MOVE_FALLING;
    ctx.object_count = 2;
    ctx.objects = windows;
    ctx.window_landing_enabled = true;
    
    /* Sheep is NOT above the window - window top at y=200, sheep bottom at 570 */
    /* The 2-pixel tolerance check requires abs(bottom - obj.y) <= 2 */
    /* 570 - 200 = 370 > 2, so no match */
    EsheepFallTarget target = {0};
    bool found = esheep_classify_fall(&ctx, &target);
    assert(found == false);
    
    /* Now test with sheep positioned to be above window */
    ctx.pos_y = 160;  /* sheep bottom at 200, which equals window top */
    memset(&target, 0, sizeof(target));
    found = esheep_classify_fall(&ctx, &target);
    assert(found == true);
    assert(target.surface == ESHEEP_SURFACE_WINDOW);
    assert(target.landed_y == 200);
}

static void test_taskbar_landing(void) {
    fprintf(stderr, "test: taskbar landing\n");
    EsheepSurfaceObject taskbar = {
        .x = 0, .y = 1000, .width = 1920, .height = 50,
        .stack_order = 100, .taskbar = true
    };
    
    EsheepContext ctx = {0};
    ctx.pos_x = 960;
    ctx.pos_y = 1030;  /* bottom at 1070, taskbar top at 1000, diff 70 > 2 */
    ctx.image_width = 40;
    ctx.image_height = 40;
    ctx.bounds_x = 0;
    ctx.bounds_y = 0;
    ctx.bounds_width = 1920;
    ctx.bounds_height = 1080;
    ctx.move = ESHEEP_MOVE_FALLING;
    ctx.object_count = 1;
    ctx.objects = &taskbar;
    ctx.window_landing_enabled = true;
    
    /* Sheep not yet landing on taskbar */
    EsheepFallTarget target = {0};
    bool found = esheep_classify_fall(&ctx, &target);
    assert(found == false);
    
    /* Position sheep directly above taskbar */
    ctx.pos_y = 960;  /* bottom at 1000, equals taskbar top */
    memset(&target, 0, sizeof(target));
    found = esheep_classify_fall(&ctx, &target);
    assert(found == true);
    assert(target.surface == ESHEEP_SURFACE_TASKBAR);
    assert(target.landed_y == 1000);
}

static void test_unsupported_surface(void) {
    fprintf(stderr, "test: unsupported surface classification\n");
    EsheepContext ctx = {0};
    ctx.pos_x = 100;
    ctx.pos_y = 100;
    ctx.image_width = 40;
    ctx.image_height = 40;
    ctx.bounds_x = 0;
    ctx.bounds_y = 0;
    ctx.bounds_width = 1920;
    ctx.bounds_height = 1080;
    ctx.move = ESHEEP_MOVE_WALKING;
    
    esheep_classify_context(&ctx);
    /* Not at edge, not falling -> should be floor */
    assert(ctx.surface == ESHEEP_SURFACE_FLOOR);
    assert(ctx.move == ESHEEP_MOVE_WALKING);
}

static void test_transition_context_mapping(void) {
    EsheepContext ctx = {0};
    assert(strcmp(esheep_transition_context(&ctx), "none") == 0);

    ctx.surface = ESHEEP_SURFACE_LEFT_EDGE;
    assert(strcmp(esheep_transition_context(&ctx), "vertical") == 0);
    ctx.surface = ESHEEP_SURFACE_RIGHT_EDGE;
    assert(strcmp(esheep_transition_context(&ctx), "vertical") == 0);

    ctx.move = ESHEEP_MOVE_FALLING;
    ctx.surface = ESHEEP_SURFACE_WINDOW;
    assert(strcmp(esheep_transition_context(&ctx), "window") == 0);
    ctx.surface = ESHEEP_SURFACE_TASKBAR;
    assert(strcmp(esheep_transition_context(&ctx), "taskbar") == 0);
    ctx.surface = ESHEEP_SURFACE_FLOOR;
    assert(strcmp(esheep_transition_context(&ctx), "none") == 0);
}

static void test_apply_motion_swept_window_landing(void) {
    EsheepSurfaceObject window = {
        .x = 100, .y = 200, .width = 300, .height = 300,
        .stack_order = 4, .taskbar = false
    };
    EsheepMotion motion = {
        .pos_x = 120, .pos_y = 140, .delta_y = 80,
        .image_width = 40, .image_height = 40,
        .bounds_width = 800, .bounds_height = 600,
        .object_count = 1, .objects = &window,
        .window_landing_enabled = true,
    };
    assert(strcmp(esheep_apply_motion(&motion), "window") == 0);
    assert(motion.pos_y == 160);
}

static void test_apply_motion_floor_and_edge(void) {
    EsheepMotion motion = {
        .pos_x = 10, .pos_y = 550, .delta_x = -20, .delta_y = 80,
        .image_width = 40, .image_height = 40,
        .bounds_width = 800, .bounds_height = 600,
    };
    assert(strcmp(esheep_apply_motion(&motion), "horizontal+") == 0);
    assert(motion.pos_x == -10);
    assert(motion.pos_y == 560);

    motion.pos_x = 0;
    motion.pos_y = 0;
    motion.delta_x = -1;
    motion.delta_y = 0;
    assert(strcmp(esheep_apply_motion(&motion), "vertical") == 0);
    assert(motion.pos_x == 0);
}

static void test_window_on_monitor_edge(void) {
    fprintf(stderr, "test: window landing on monitor edge\n");
    /* Window spanning the right side of the monitor */
    EsheepSurfaceObject window_right = {
        .x = 1880, .y = 200, .width = 40, .height = 200,
        .stack_order = 5, .taskbar = false
    };
    
    /* Mouse at x=1900, on the right edge of monitor */
    EsheepContext ctx = {0};
    ctx.pos_x = 1900;
    ctx.pos_y = 1000;
    ctx.image_width = 40;
    ctx.image_height = 40;
    ctx.bounds_x = 0;
    ctx.bounds_y = 0;
    ctx.bounds_width = 1920;
    ctx.bounds_height = 1080;
    ctx.move = ESHEEP_MOVE_WALKING;
    
    esheep_classify_context(&ctx);
    /* At right horizontal edge */
    assert(ctx.surface == ESHEEP_SURFACE_RIGHT_EDGE);
    
    /* Now test falling onto window at edge */
    ctx.move = ESHEEP_MOVE_FALLING;
    ctx.object_count = 1;
    ctx.objects = &window_right;
    ctx.window_landing_enabled = true;
    ctx.pos_y = 160;  /* fall onto window at y=200 (sheep bottom at 200) */
    
    EsheepFallTarget target = {0};
    bool found = esheep_classify_fall(&ctx, &target);
    /* Window top at y=200, sheep bottom at 200, diff = 0 <= 2 */
    assert(found == true);
    assert(target.surface == ESHEEP_SURFACE_WINDOW);
    assert(target.landed_y == 200);
}

static void test_no_repeated_edge_dispatch(void) {
    fprintf(stderr, "test: no repeated edge dispatch per tick cycle\n");
    EsheepContext ctx = {0};
    ctx.pos_x = 0;
    ctx.bounds_x = 0;
    ctx.image_width = 40;
    ctx.surface = ESHEEP_SURFACE_LEFT_EDGE;
    
    int next_dir = 42;  /* sentinel - must not be set when dispatched */
    bool edge_dispatched = true;  /* Already dispatched this cycle */
    
    /* Once dispatched, should return false and not change direction */
    bool handled = esheep_authored_edge_reversal(&ctx, &next_dir, &edge_dispatched);
    assert(handled == false);
    assert(next_dir == 42);  /* Should be unchanged */
}

static void test_walking_animation_not_falling(void) {
    fprintf(stderr, "test: walking animation is not falling\n");
    /* Regression: build_context should NOT treat ANIM_WALK as falling.
     * A walking sheep on the floor is grounded, not airborne. */
    EsheepContext ctx = {0};
    ctx.pos_x = 100;
    ctx.pos_y = 500;
    ctx.image_width = 40;
    ctx.image_height = 40;
    ctx.bounds_x = 0;
    ctx.bounds_y = 0;
    ctx.bounds_width = 1920;
    ctx.bounds_height = 1080;
    ctx.move = ESHEEP_MOVE_WALKING;  /* ANIM_WALK should produce WALKING, not FALLING */
    ctx.object_count = 0;
    ctx.window_landing_enabled = true;

    esheep_classify_context(&ctx);
    /* A walking (grounded) sheep should be on the floor or edge, NOT airborne */
    assert(ctx.move == ESHEEP_MOVE_WALKING);
    /* It should NOT be classified as falling to a surface below */
    assert(ctx.surface != ESHEEP_SURFACE_WINDOW);
    assert(ctx.surface != ESHEEP_SURFACE_TASKBAR);
}

static void test_dropped_sprite_inside_window_does_not_snap_up(void) {
    fprintf(stderr, "test: dropped sprite below window top is not pulled upward\n");
    EsheepSurfaceObject object = {
        .x = 100, .y = 100, .width = 300, .height = 300,
        .stack_order = 1, .taskbar = false
    };
    EsheepContext ctx = {0};
    ctx.pos_x = 160;
    ctx.pos_y = 140; /* top is below the window top, already inside it */
    ctx.image_width = 40;
    ctx.image_height = 40;
    ctx.bounds_width = 640;
    ctx.bounds_height = 480;
    ctx.object_count = 1;
    ctx.objects = &object;
    ctx.window_landing_enabled = true;
    ctx.drop_landing_enabled = true;
    ctx.move = ESHEEP_MOVE_FALLING;

    esheep_classify_context(&ctx);
    assert(ctx.surface == ESHEEP_SURFACE_FLOOR);
    assert(ctx.surface_y == 480);
}

int main(void) {
    test_left_edge_reversal();
    test_right_edge_reversal();
    test_repeated_edge_ticks();
    test_floor_landing();
    test_window_landing();
    test_taskbar_landing();
    test_unsupported_surface();
    test_transition_context_mapping();
    test_apply_motion_swept_window_landing();
    test_apply_motion_floor_and_edge();
    test_window_on_monitor_edge();
    test_no_repeated_edge_dispatch();
    test_walking_animation_not_falling();
    test_dropped_sprite_inside_window_does_not_snap_up();
    
    printf("All context tests passed.\n");
    return 0;
}
