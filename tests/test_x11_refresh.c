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

    cleanup_app(&app);
    return 0;
}
