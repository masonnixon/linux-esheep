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

    cleanup_app(&app);
    return 0;
}
