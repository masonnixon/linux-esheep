#include <assert.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#define main esheep_app_main
#include "../src/main.c"
#undef main

static void set_client_list(Display *display, Window root, Atom property,
                            Window client) {
    XChangeProperty(display, root, property, XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&client, 1);
    XChangeProperty(display, root,
                    XInternAtom(display, "_NET_CLIENT_LIST_STACKING", False),
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&client, 1);
    XSync(display, False);
}

static int root_child_index(Display *display, Window root, Window needle) {
    Window queried_root, parent, *children = NULL;
    unsigned int count = 0;
    int result = -1;
    if (!XQueryTree(display, root, &queried_root, &parent, &children, &count))
        return -1;
    for (unsigned int i = 0; i < count; i++) {
        if (children[i] == needle) {
            result = (int)i;
            break;
        }
    }
    if (children) XFree(children);
    return result;
}

/* The group regression: five sheep sharing one desktop snapshot must
 * produce exactly one X11 client-list rescan per refresh interval, no
 * matter how many sheep tick. refresh_count is the seam that counts the
 * completed group rescans, not the equivalence of the data the sheep
 * eventually see. */
static void test_group_snapshot_shares_one_refresh(App *lead) {
    GdkDisplay *gdk_display = gdk_display_get_default();
    Display *display = gdk_x11_display_get_xdisplay(gdk_display);
    Window root = DefaultRootWindow(display);
    Atom client_list = XInternAtom(display, "_NET_CLIENT_LIST", False);

    App sheep[5];
    memset(sheep, 0, sizeof(sheep));
    sheep[0] = *lead;
    for (int i = 0; i < 5; i++) {
        sheep[i].siblings = sheep;
        sheep[i].sibling_count = 5;
        sheep[i].tile_size = lead->tile_size;
        sheep[i].bounds = lead->bounds;
        sheep[i].window_landing = TRUE;
        sheep[i].exclude_conky = lead->exclude_conky;
        sheep[i].tick_ms = TICK_MS;
        sheep[i].pos_x = lead->pos_x;
        sheep[i].pos_y = lead->pos_y;
        sheep[i].xwindow = 0; /* copies must not restack the real window */
    }
    DesktopSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    for (int i = 0; i < 5; i++) sheep[i].shared_snapshot = &snapshot;

    Window client = XCreateSimpleWindow(display, root, 100, 100, 400, 200, 0,
                                        0, 0);
    XMapWindow(display, client);
    set_client_list(display, root, client_list, client);

    /* Group tick 1: the deadline is due, so exactly one of the five sheep
     * performs the X11 rescan; the rest consume the stored generation. */
    for (int i = 0; i < 5; i++) desktop_snapshot_tick(&sheep[i]);
    assert(snapshot.refresh_count == 1);

    /* Four more full group ticks inside the same refresh interval must not
     * rescan, and every sheep must hold the same snapshot data. */
    for (int t = 0; t < 4; t++) {
        for (int i = 0; i < 5; i++) desktop_snapshot_tick(&sheep[i]);
    }
    assert(snapshot.refresh_count == 1);
    for (int i = 0; i < 5; i++) {
        assert(sheep[i].object_count == 1);
        assert(sheep[i].objects[0].rect.x == 100);
        assert(sheep[i].objects[0].rect.y == 100);
        assert(sheep[i].objects[0].rect.width == 400);
        assert(sheep[i].objects[0].rect.height == 200);
        assert(sheep[i].objects[0].stack_order == 0);
    }

    /* The snapshot must not stay stale: once the deadline passes again the
     * group performs its next rescan. */
    snapshot.next_refresh_at_us = 0;
    for (int i = 0; i < 5; i++) desktop_snapshot_tick(&sheep[i]);
    assert(snapshot.refresh_count == 2);
    for (int i = 0; i < 5; i++) assert(sheep[i].object_count == 1);

    /* The drag-release path forces an immediate group rescan. Model a real
     * refresh race by leaving the destroyed ID in _NET_CLIENT_LIST. */
    set_client_list(display, root, client_list, client);
    XDestroyWindow(display, client);
    XSync(display, False);
    refresh_objects(&sheep[0]);
    assert(snapshot.refresh_count == 3);
    assert(sheep[0].object_count == 0);
    for (int i = 1; i < 5; i++) desktop_snapshot_consume(&sheep[i], &snapshot);
    for (int i = 1; i < 5; i++) assert(sheep[i].object_count == 0);
}

/* Anti-false-positive regression for the restack cache: the five-sheep
 * consume cycle must act on the scan-time geometry, never on fresh X11
 * discovery. The client is scanned while overlapping the sheep, then moved
 * far away, and only then consumed: a consumer that re-queried geometry
 * would see no overlap and skip the restack, while the cached consumer
 * must put every sheep below the client at its scan-time position. The
 * client starts below the sheep in the root stack, so only the restack
 * request can reorder them. */
static void test_consume_cycle_uses_cached_restack_geometry(void) {
    GdkDisplay *gdk_display = gdk_display_get_default();
    Display *display = gdk_x11_display_get_xdisplay(gdk_display);
    Window root = DefaultRootWindow(display);
    Atom client_list = XInternAtom(display, "_NET_CLIENT_LIST", False);

    Window client = XCreateSimpleWindow(display, root, 100, 100, 200, 120, 0,
                                        0, 0);
    XMapWindow(display, client);

    App sheep[5];
    memset(sheep, 0, sizeof(sheep));
    for (int i = 0; i < 5; i++) {
        sheep[i].siblings = sheep;
        sheep[i].sibling_count = 5;
        sheep[i].tile_size = 32;
        sheep[i].bounds = (GdkRectangle){ 0, 0, 1280, 720 };
        sheep[i].window_landing = TRUE;
        sheep[i].tick_ms = TICK_MS;
        sheep[i].pos_x = 100;
        sheep[i].pos_y = 100;
        sheep[i].xwindow = XCreateSimpleWindow(display, root, 100, 100, 32,
                                               32, 0, 0, 0);
        XMapWindow(display, sheep[i].xwindow);
    }
    DesktopSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    for (int i = 0; i < 5; i++) sheep[i].shared_snapshot = &snapshot;

    set_client_list(display, root, client_list, client);
    desktop_snapshot_scan(&snapshot, &sheep[0]);
    assert(snapshot.refresh_count == 1);

    /* Move the client where no sheep stands, then let the whole group
     * consume the generation that was scanned before the move. */
    XMoveResizeWindow(display, client, 1400, 600, 200, 120);
    XSync(display, False);
    for (int i = 0; i < 5; i++) desktop_snapshot_consume(&sheep[i], &snapshot);
    for (int i = 0; i < 5; i++)
        assert(root_child_index(display, root, sheep[i].xwindow) <
               root_child_index(display, root, client));

    /* The landing view is cached in the same generation: the sheep still
     * see the scan-time position, and a second consume pass stays a no-op
     * thanks to the generation gate (no rescan). */
    for (int i = 0; i < 5; i++) {
        assert(sheep[i].object_count == 1);
        assert(sheep[i].objects[0].rect.x == 100);
        assert(sheep[i].objects[0].rect.y == 100);
        desktop_snapshot_consume(&sheep[i], &snapshot);
    }
    assert(snapshot.refresh_count == 1);

    for (int i = 0; i < 5; i++) XDestroyWindow(display, sheep[i].xwindow);
    XDestroyWindow(display, client);
    XSync(display, False);
}

int main(int argc, char **argv) {
    assert(gtk_init_check(&argc, &argv));
    GdkDisplay *gdk_display = gdk_display_get_default();
    assert(GDK_IS_X11_DISPLAY(gdk_display));
    Display *display = gdk_x11_display_get_xdisplay(gdk_display);
    Window root = DefaultRootWindow(display);
    Atom client_list = XInternAtom(display, "_NET_CLIENT_LIST", False);

    App app = {0};
    app.tile_size = 32;
    app.window_landing = TRUE;
    app.exclude_conky = TRUE;
    app.direction = 1;
    setup_sheep_window(&app, gdk_display,
                       gdk_display_get_monitor(gdk_display, 0));
    while (gtk_events_pending()) gtk_main_iteration();

    Window client = XCreateSimpleWindow(display, root, 100, 100, 400, 200, 0,
                                        0, 0);
    XMapWindow(display, client);
    set_client_list(display, root, client_list, client);
    refresh_objects(&app);
    assert(app.object_count == 1);

    /* A foreground client overlapping the pet must be above the pet in the
     * actual X11 tree, while the pet remains a normal managed window. */
    XMoveWindow(display, client, app.pos_x, app.pos_y);
    XRaiseWindow(display, client);
    XSync(display, False);
    refresh_objects(&app);
    assert(root_child_index(display, root, app.xwindow) <
           root_child_index(display, root, client));

    /* A malformed root property is an incomplete refresh, not an empty
     * desktop. Preserve the last complete surface snapshot. */
    XChangeProperty(display, root, client_list, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&client, 1);
    XSync(display, False);
    refresh_objects(&app);
    assert(app.object_count == 1);

    /* Leave the destroyed ID in _NET_CLIENT_LIST to model a real refresh race.
     * The scoped X11 handler must discard it instead of reaching GDK's fatal
     * error path. */
    set_client_list(display, root, client_list, client);
    XDestroyWindow(display, client);
    XSync(display, False);
    refresh_objects(&app);
    assert(app.object_count == 0);

    /* Most X11 WMs reparent clients into a frame. The client-list entry is
     * still the inner window, but collision geometry and occlusion must use
     * the frame that is actually a sibling of the sheep. */
    Window frame = XCreateSimpleWindow(display, root, app.pos_x, app.pos_y,
                                       420, 240, 0, 0, 0);
    Window reparented_client = XCreateSimpleWindow(display, frame, 8, 24,
                                                   400, 200, 0, 0, 0);
    XMapWindow(display, reparented_client);
    XMapWindow(display, frame);
    set_client_list(display, root, client_list, reparented_client);
    XRaiseWindow(display, frame);
    XSync(display, False);
    refresh_objects(&app);
    assert(app.object_count == 1);
    assert(app.objects[0].rect.x == app.pos_x);
    assert(app.objects[0].rect.y == app.pos_y);
    assert(app.objects[0].rect.width == 420);
    assert(app.objects[0].rect.height == 240);
    assert(root_child_index(display, root, app.xwindow) <
           root_child_index(display, root, frame));

    XDestroyWindow(display, frame);
    XSync(display, False);
    set_client_list(display, root, client_list, reparented_client);
    refresh_objects(&app);
    assert(app.object_count == 0);

    /* Restacking must use the sheep's WM frame too.  Using the reparented
     * client as the ConfigureWindow target/sibling produces BadMatch under a
     * real WM because those windows do not share the root parent. */
    Window sheep_frame = XCreateSimpleWindow(display, root, 220, 180, 64, 64,
                                             0, 0, 0);
    Window sheep_client = XCreateSimpleWindow(display, sheep_frame, 0, 0, 64,
                                              64, 0, 0, 0);
    Window occluding_frame = XCreateSimpleWindow(display, root, 220, 180,
                                                  320, 180, 0, 0, 0);
    Window occluding_client = XCreateSimpleWindow(display, occluding_frame, 0,
                                                   24, 320, 156, 0, 0, 0);
    XMapWindow(display, sheep_client);
    XMapWindow(display, sheep_frame);
    XMapWindow(display, occluding_client);
    XMapWindow(display, occluding_frame);
    XRaiseWindow(display, occluding_frame);
    XSync(display, False);

    App reparented_sheep = {0};
    reparented_sheep.xwindow = sheep_client;
    reparented_sheep.tile_size = 32;
    reparented_sheep.pos_x = 220;
    reparented_sheep.pos_y = 180;
    Window candidates[] = { occluding_client };
    Window stacking[] = { occluding_client };
    Atom window_type_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom desktop_type_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP",
                                         False);
    restack_below_occluding_window(&reparented_sheep, display, candidates, 1,
                                   stacking, 1, window_type_atom,
                                   desktop_type_atom);
    XSync(display, False);
    assert(root_child_index(display, root, sheep_frame) <
           root_child_index(display, root, occluding_frame));

    XDestroyWindow(display, sheep_frame);
    XDestroyWindow(display, occluding_frame);
    XSync(display, False);

    /* A desktop-background window (xfdesktop's per-workspace "Desktop"
     * window on this WM, but any _NET_WM_WINDOW_TYPE_DESKTOP window is the
     * same case) covers the whole monitor and is part of _NET_CLIENT_LIST
     * on this WM, yet sits at the very bottom of the stack. Without
     * excluding it, a sheep standing anywhere with no real application
     * window overlapping it still finds an "occluder" -- the wallpaper --
     * and gets pushed below it, i.e. below everything, which made a sheep
     * invisible almost everywhere it could stand. It must never be treated
     * as an occluder. */
    Window desktop_sheep_frame = XCreateSimpleWindow(display, root, 100, 100,
                                                      32, 32, 0, 0, 0);
    Window desktop_sheep_client = XCreateSimpleWindow(display, desktop_sheep_frame,
                                                       0, 0, 32, 32, 0, 0, 0);
    Window desktop_frame = XCreateSimpleWindow(display, root, 0, 0, 1920, 1080,
                                               0, 0, 0);
    XChangeProperty(display, desktop_frame, window_type_atom, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&desktop_type_atom, 1);
    XMapWindow(display, desktop_frame);
    XMapWindow(display, desktop_sheep_client);
    XMapWindow(display, desktop_sheep_frame);
    XRaiseWindow(display, desktop_sheep_frame);
    XSync(display, False);

    int sheep_index_before = root_child_index(display, root,
                                              desktop_sheep_frame);
    int desktop_index_before = root_child_index(display, root, desktop_frame);
    assert(sheep_index_before > desktop_index_before);

    App desktop_test_sheep = {0};
    desktop_test_sheep.xwindow = desktop_sheep_client;
    desktop_test_sheep.tile_size = 32;
    desktop_test_sheep.pos_x = 100;
    desktop_test_sheep.pos_y = 100;
    Window desktop_candidates[] = { desktop_frame };
    Window desktop_stacking[] = { desktop_frame, desktop_sheep_client };
    restack_below_occluding_window(&desktop_test_sheep, display,
                                   desktop_candidates, 1, desktop_stacking, 2,
                                   window_type_atom, desktop_type_atom);
    XSync(display, False);
    assert(root_child_index(display, root, desktop_sheep_frame) >
           root_child_index(display, root, desktop_frame));

    XDestroyWindow(display, desktop_sheep_frame);
    XDestroyWindow(display, desktop_frame);
    XSync(display, False);

    /* Group regression: all sheep in the group share one desktop snapshot
     * refresh per interval (the seam counts the rescans). */
    test_group_snapshot_shares_one_refresh(&app);

    /* Anti-false-positive regression: the five-sheep consume cycle must
     * restack from the cached scan, not a fresh discovery. */
    test_consume_cycle_uses_cached_restack_geometry();

    cleanup_app(&app);
    return 0;
}
