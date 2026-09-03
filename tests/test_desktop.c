#include <assert.h>
#include <stdio.h>
#include <string.h>

#define main esheep_app_main
#include "../src/main.c"
#undef main

static void init_stub_app(App *app, int bounds_x, int bounds_y,
                          int bounds_width, int bounds_height, int tile_size) {
    memset(app, 0, sizeof(*app));
    app->tile_size = tile_size;
    app->direction = 1;
    app->bounds = (GdkRectangle){ bounds_x, bounds_y, bounds_width,
                                  bounds_height };
}

static void test_monitor_global_coordinates_negative_origin(void) {
    GdkRectangle monitor = { -1600, -80, 1600, 900 };

    assert(monitor_global_x(&monitor, 25) == -1575);
    assert(monitor_global_y(&monitor, 120) == 40);
    assert(monitor_contains_global_point(&monitor, -1, 0));
    assert(monitor_contains_global_point(&monitor, -1599, -79));
    assert(!monitor_contains_global_point(&monitor, 0, 0));
    assert(!monitor_contains_global_point(&monitor, -1601, 0));
}

static void test_monitor_seam_selection_prefers_direction(void) {
    GdkRectangle monitors[] = {
        { -1600, 0, 1606, 900 },
        { 6, 0, 1920, 1080 },
    };

    assert(select_monitor_index(monitors, 2, &monitors[0], 7, 100, 1) == 1);
    assert(select_monitor_index(monitors, 2, &monitors[1], 5, 100, -1) == 0);
}

static void test_monitor_gap_does_not_teleport(void) {
    GdkRectangle monitors[] = {
        { 0, 0, 100, 600 },
        { 140, 0, 100, 600 },
    };
    assert(select_monitor_index(monitors, 2, &monitors[0], 120, 200, 1) == -1);
}

static void test_offset_monitor_requires_vertical_overlap(void) {
    GdkRectangle monitors[] = {
        { 0, 0, 100, 400 },
        { 100, 200, 100, 400 },
    };
    assert(select_monitor_index(monitors, 2, &monitors[0], 101, 100, 1) == -1);
    assert(select_monitor_index(monitors, 2, &monitors[0], 101, 300, 1) == 1);
}

static void test_surface_sync_filters_other_monitors(void) {
    App app;

    init_stub_app(&app, -1600, -80, 1600, 900, 64);
    app.object_count = 2;
    app.objects[0].rect = (GdkRectangle){ -1500, 200, 300, 120 };
    app.objects[0].stack_order = 4;
    app.objects[1].rect = (GdkRectangle){ 100, 200, 300, 120 };
    app.objects[1].stack_order = 9;

    int count = sync_surface_objects(&app);
    assert(count == 1);
    assert(app.surfaces[0].x == -1500);
    assert(app.surfaces[0].width == 300);
}

/* Child animation offsets are local to the composited scene surface
 * (tile_size x tile_size), not screen coordinates. The monitor origin
 * must NOT be added. */
static void test_child_coordinates_are_local_to_scene(void) {
    App app;

    init_stub_app(&app, -1600, -80, 1600, 900, 64);
    app.pos_x = -900;
    app.pos_y = 320;

    /* areaH-imageH = 900-64 = 836, in local scene coordinates */
    assert(child_local_coordinate(&app, "areaH-imageH", 0) == 836);

    /* screenW+10-areaH/2-(randS*areaH/2)/120 with roll=0:
     * = 1600+10-450-0 = 1160, in local scene coordinates */
    assert(child_local_coordinate(
               &app, "screenW+10-areaH/2-(randS*areaH/2)/120", 0) == 1160);

    /* Spawn expressions still produce global screen coordinates, since those
     * control window positioning. */
    assert(monitor_global_x(&app.bounds,
                            eval_spawn_expression("screenW+10", app.bounds.width,
                                                  app.bounds.height,
                                                  app.tile_size, app.tile_size,
                                                  0)) == 10);
    assert(monitor_global_y(&app.bounds,
                            eval_spawn_expression("areaH-imageH",
                                                  app.bounds.width,
                                                  app.bounds.height,
                                                  app.tile_size, app.tile_size,
                                                  0)) == 756);
}

static void test_fullscreen_and_panel_surfaces_are_excluded(void) {
    DesktopSurfaceTraits normal = {0};
    DesktopSurfaceTraits fullscreen = { .fullscreen_surface = TRUE };
    DesktopSurfaceTraits panel = { .panel_surface = TRUE };
    DesktopSurfaceTraits desktop = { .desktop_surface = TRUE };
    DesktopSurfaceTraits conky = { .conky_surface = TRUE };

    assert(x11_surface_is_landing_candidate(&normal));
    assert(!x11_surface_is_landing_candidate(&fullscreen));
    assert(!x11_surface_is_landing_candidate(&panel));
    assert(!x11_surface_is_landing_candidate(&desktop));
    assert(!x11_surface_is_landing_candidate(&conky));
}

static void test_x11_fallback_capabilities(void) {
    DesktopBackendCapabilities requested = detect_backend_capabilities(
        TRUE, "wayland-0", ":1", NULL, FALSE);
    DesktopBackendCapabilities runtime_wayland = detect_backend_capabilities(
        FALSE, "wayland-0", ":1", NULL, FALSE);
    DesktopBackendCapabilities runtime_x11 = detect_backend_capabilities(
        FALSE, NULL, ":1", NULL, TRUE);

    assert(requested.mode == DESKTOP_BACKEND_X11_FALLBACK);
    assert(requested.should_force_x11_backend);
    assert(requested.can_position_globally);
    assert(requested.can_query_desktop_surfaces);

    assert(runtime_wayland.mode == DESKTOP_BACKEND_WAYLAND_UNSUPPORTED);
    assert(!runtime_wayland.can_position_globally);
    assert(!runtime_wayland.can_query_desktop_surfaces);

    assert(runtime_x11.mode == DESKTOP_BACKEND_X11);
    assert(runtime_x11.can_position_globally);
    assert(runtime_x11.can_query_desktop_surfaces);
}



static void test_x11_occlusion_stacking_policy(void) {
    /* The sheep window must NOT request an unconditional keep-above
     * stacking state, and must NOT pin itself to all desktops via stick().
     * Both of those prior behaviors forced the sheep back on top of any
     * freshly-raised application window every tick. The idiomatic GTK3
     * policy is GDK_WINDOW_TYPE_HINT_NORMAL on a GTK_WINDOW_TOPLEVEL: the
     * WM controls stacking and newly-raised application windows occlude the
     * sheep. */
    assert(sheep_window_type_hint() == GDK_WINDOW_TYPE_HINT_NORMAL);
}

static void test_fullscreen_surface_coverage(void) {
    GdkRectangle monitor = { -1920, 0, 1920, 1080 };
    GdkRectangle full = { -1920, 0, 1920, 1080 };
    GdkRectangle partial = { -1920, 0, 1920, 1000 };
    GdkRectangle offset = { -1919, 0, 1920, 1080 };
    assert(fullscreen_covers_monitor(&monitor, &full));
    assert(!fullscreen_covers_monitor(&monitor, &partial));
    assert(!fullscreen_covers_monitor(&monitor, &offset));
}
int main(void) {
    test_monitor_global_coordinates_negative_origin();
    test_monitor_seam_selection_prefers_direction();
    test_monitor_gap_does_not_teleport();
    test_offset_monitor_requires_vertical_overlap();
    test_surface_sync_filters_other_monitors();
    test_child_coordinates_are_local_to_scene();
    test_fullscreen_and_panel_surfaces_are_excluded();
    test_x11_fallback_capabilities();
    test_x11_occlusion_stacking_policy();
    test_fullscreen_surface_coverage();
    printf("All desktop backend tests passed\n");
    return 0;
}
