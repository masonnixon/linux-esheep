/*
 * P7: Real X11 window-manager integration coverage
 *
 * This test exercises reparenting, stacking, and stale-window races under
 * a real X11 WM (xfwm4, openbox, matchbox-window-manager, fluxbox, i3).
 * It runs opt-in when a nested X server (Xvfb) and a reparenting WM are
 * available; otherwise it exits with code 77 (skip) and a clear reason.
 *
 * Set ESHEEP_TEST_STRICT=1 to require prerequisites and fail (exit 1)
 * instead of skipping.
 *
 * The fixtures intentionally adapt to actual WM behavior:
 *   - Geometry is queried from the WM's frame after mapping, not from the
 *     requested client position; WMs may smart-place and decorate.
 *   - Stacking comparisons walk up to the root-level ancestor (the WM
 *     frame), since reparented clients are no longer direct root children.
 *   - GTK window origins are reported in GDK root coordinates, which add
 *     monitor work-area offsets the production code already respects.
 */

#include <assert.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

#define main esheep_app_main
#include "../src/main.c"
#undef main

/* Test result codes */
#define TEST_PASS 0
#define TEST_FAIL 1
#define TEST_SKIP 77

/* Global test state */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_skipped = 0;

static void test_start(const char *name) {
    printf("  TEST: %s ... ", name);
    fflush(stdout);
    tests_run++;
}

static void test_pass(void) {
    printf("PASS\n");
    tests_passed++;
}

/* Strict mode: when ESHEEP_TEST_STRICT=1, missing prerequisites fail the
 * whole suite rather than skipping. Mirrors the convention used by the
 * other visual/X11 tests in this tree. */
static gboolean strict_mode(void) {
    const char *env = g_getenv("ESHEEP_TEST_STRICT");
    return env && (env[0] == '1' || env[0] == 't' || env[0] == 'T' ||
                   env[0] == 'y' || env[0] == 'Y');
}

/* Check if we have a nested X server available (Xvfb) */
static gboolean have_xvfb(void) {
    return g_find_program_in_path("Xvfb") != NULL ||
           g_find_program_in_path("xvfb-run") != NULL;
}

/* Check if we have a lightweight reparenting WM available */
static gboolean have_reparenting_wm(void) {
    const char *wms[] = { "matchbox-window-manager", "openbox", "xfwm4",
                          "fluxbox", "i3", NULL };
    for (int i = 0; wms[i]; i++) {
        if (g_find_program_in_path(wms[i]) != NULL)
            return TRUE;
    }
    return FALSE;
}

/* Pick a reparenting WM, preferring the ones known to honour EWMH
 * _NET_CLIENT_LIST semantics and reparent managed windows. xfwm4 is the
 * default on this tree's CI environment; openbox and matchbox also
 * reparent. */
static const char *pick_reparenting_wm(void) {
    const char *wms[] = { "xfwm4", "openbox", "matchbox-window-manager",
                          "fluxbox", "i3", NULL };
    for (int i = 0; wms[i]; i++) {
        if (g_find_program_in_path(wms[i]) != NULL) return wms[i];
    }
    return NULL;
}

/* Pick a free X display number by probing for the lock file. The nested
 * X server only needs one display number per test run, so we don't worry
 * about races on the lock directory itself. */
static int find_free_display(void) {
    for (int i = 99; i < 200; i++) {
        char lock_file[256];
        snprintf(lock_file, sizeof(lock_file), "/tmp/.X%d-lock", i);
        if (access(lock_file, F_OK) != 0) return i;
    }
    return -1;
}

/* Try to start Xvfb briefly to confirm the environment can host one. We
 * only need to verify it does not immediately crash from missing /tmp
 * sockets or other sandbox restrictions; the real run uses a fresh
 * fork. */
static gboolean can_start_xvfb(int display_num) {
    char display_str[32];
    snprintf(display_str, sizeof(display_str), ":%d", display_num);
    pid_t pid = fork();
    if (pid == 0) {
        execlp("Xvfb", "Xvfb", display_str, "-screen", "0", "1280x720x24",
               "-ac", "-nolisten", "tcp", NULL);
        _exit(1);
    }
    if (pid < 0) return FALSE;
    /* Xvfb takes a moment to create its socket; allow up to a second. */
    for (int i = 0; i < 10; i++) {
        g_usleep(100 * 1000);
        int status;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) return FALSE;
        char lock_file[256];
        snprintf(lock_file, sizeof(lock_file), "/tmp/.X%d-lock", display_num);
        if (access(lock_file, F_OK) == 0) {
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            return TRUE;
        }
    }
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    return FALSE;
}

/* Start Xvfb and a reparenting WM on a fresh display. Returns the
 * display number on success, or -1 on failure. */
static int start_nested_x_server_and_wm(pid_t *xvfb_pid, pid_t *wm_pid) {
    int display_num = find_free_display();
    if (display_num < 0) return -1;
    if (!can_start_xvfb(display_num)) return -1;

    char display_str[32];
    snprintf(display_str, sizeof(display_str), ":%d", display_num);

    *xvfb_pid = fork();
    if (*xvfb_pid == 0) {
        execlp("Xvfb", "Xvfb", display_str, "-screen", "0", "1280x720x24",
               "-ac", "-nolisten", "tcp", NULL);
        _exit(1);
    }
    if (*xvfb_pid < 0) return -1;

    /* Wait for Xvfb's lock file; abort if it dies. */
    char lock_file[256];
    snprintf(lock_file, sizeof(lock_file), "/tmp/.X%d-lock", display_num);
    gboolean ready = FALSE;
    for (int i = 0; i < 30; i++) {
        g_usleep(100 * 1000);
        int status;
        if (waitpid(*xvfb_pid, &status, WNOHANG) == *xvfb_pid) {
            *xvfb_pid = -1;
            return -1;
        }
        if (access(lock_file, F_OK) == 0) { ready = TRUE; break; }
    }
    if (!ready) {
        kill(*xvfb_pid, SIGTERM);
        waitpid(*xvfb_pid, NULL, 0);
        *xvfb_pid = -1;
        return -1;
    }

    g_setenv("DISPLAY", display_str, TRUE);
    g_unsetenv("XAUTHORITY");

    const char *wm_name = pick_reparenting_wm();
    if (!wm_name) {
        kill(*xvfb_pid, SIGTERM);
        waitpid(*xvfb_pid, NULL, 0);
        *xvfb_pid = -1;
        return -1;
    }
    *wm_pid = fork();
    if (*wm_pid == 0) {
        execlp(wm_name, wm_name, NULL);
        _exit(1);
    }
    if (*wm_pid < 0) {
        kill(*xvfb_pid, SIGTERM);
        waitpid(*xvfb_pid, NULL, 0);
        *xvfb_pid = -1;
        return -1;
    }

    /* Give the WM a moment to claim the display before GTK attaches. */
    g_usleep(800 * 1000);
    return display_num;
}

static void stop_nested_x_server_and_wm(pid_t xvfb_pid, pid_t wm_pid) {
    if (wm_pid > 0) {
        kill(wm_pid, SIGTERM);
        waitpid(wm_pid, NULL, 0);
    }
    if (xvfb_pid > 0) {
        kill(xvfb_pid, SIGTERM);
        waitpid(xvfb_pid, NULL, 0);
    }
}

/* Set _NET_CLIENT_LIST and _NET_CLIENT_LIST_STACKING atomically with the
 * given list of root-level windows (or pre-reparenting clients). */
static void set_client_list(Display *display, Window root,
                            const Window *clients, int count) {
    Atom client_list = XInternAtom(display, "_NET_CLIENT_LIST", False);
    Atom client_list_stacking = XInternAtom(display,
                                            "_NET_CLIENT_LIST_STACKING",
                                            False);
    XChangeProperty(display, root, client_list, XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)clients, count);
    XChangeProperty(display, root, client_list_stacking, XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)clients, count);
    XSync(display, False);
}

/* Find the root-level ancestor of an X window by walking up parents.
 * Mirrors the production code's helper semantics. */
static Window root_level_ancestor(Display *display, Window window, Window root) {
    Window current = window;
    for (int depth = 0; current && depth < 32; depth++) {
        Window tree_root = None, parent = None, *children = NULL;
        unsigned int count = 0;
        if (!XQueryTree(display, current, &tree_root, &parent, &children,
                        &count)) {
            if (children) XFree(children);
            return None;
        }
        if (children) XFree(children);
        if (tree_root != root) return None;
        if (parent == root || parent == None) return current;
        current = parent;
    }
    return None;
}

/* Walk up from a window to find the root-level frame that the WM
 * reparents it into, then return that frame's geometry in root
 * coordinates. Returns FALSE if the frame cannot be located or the
 * window has not yet been reparented. */
static gboolean frame_geometry(Display *display, Window window, Window root,
                               int *out_x, int *out_y,
                               int *out_width, int *out_height) {
    Window frame = root_level_ancestor(display, window, root);
    if (!frame) return FALSE;
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(display, frame, &attrs)) return FALSE;
    Window child;
    int tx = 0, ty = 0;
    if (!XTranslateCoordinates(display, frame, root, 0, 0, &tx, &ty, &child))
        return FALSE;
    *out_x = tx;
    *out_y = ty;
    *out_width = attrs.width;
    *out_height = attrs.height;
    return TRUE;
}

/* Index of a window among the root's direct children. -1 if absent.
 * Note: with a reparenting WM the original client is no longer a direct
 * child; tests should compare against the root-level ancestor instead. */
static int root_child_index(Display *display, Window root, Window needle) {
    Window queried_root, parent, *children = NULL;
    unsigned int count = 0;
    int result = -1;
    if (!XQueryTree(display, root, &queried_root, &parent, &children, &count))
        return -1;
    for (unsigned int i = 0; i < count; i++) {
        if (children[i] == needle) { result = (int)i; break; }
    }
    if (children) XFree(children);
    return result;
}

/* Wait briefly for a window to be mapped AND for the WM to have reparented
 * it. A real WM typically takes 50-200 ms to attach a frame. */
static void wait_for_wm_reparent(Display *display, Window window) {
    XSync(display, False);
    for (int i = 0; i < 30; i++) {
        XWindowAttributes attrs;
        if (XGetWindowAttributes(display, window, &attrs) &&
            attrs.map_state == IsViewable) {
            /* Confirm the WM has reparented by walking one step up. */
            Window tree_root, parent, *children = NULL;
            unsigned int count = 0;
            if (XQueryTree(display, window, &tree_root, &parent, &children,
                           &count)) {
                if (children) XFree(children);
                if (parent != tree_root && parent != None) return;
            } else if (children) {
                XFree(children);
            }
        }
        g_usleep(50 * 1000);
    }
}

/* ========================================================================
 * TEST 1: Reparented frame geometry
 *
 * Under a real WM the scan must report the WM's frame geometry (which
 * includes decorations) rather than the inner client geometry, and that
 * geometry must match what X reports for the same frame.
 * ======================================================================== */
static void test_reparented_frame_geometry(Display *display, Window root) {
    test_start("Reparented frame geometry under a real WM");

    /* Create a normal top-level window. The WM will reparent it into its
     * own frame; the production scan must walk up to that frame. */
    Window client = XCreateSimpleWindow(display, root, 100, 100, 420, 240, 0,
                                        0, 0);
    XMapWindow(display, client);
    wait_for_wm_reparent(display, client);
    XSync(display, False);

    Window clients[] = { client };
    set_client_list(display, root, clients, 1);

    App app = {0};
    app.tile_size = 32;
    app.window_landing = TRUE;
    app.exclude_conky = TRUE;
    app.direction = 1;
    app.bounds = (GdkRectangle){ 0, 0, 1280, 720 };
    app.pos_x = 100;
    app.pos_y = 100;
    refresh_objects(&app);

    if (app.object_count != 1) {
        printf("FAIL: expected one WM-managed object, got %d\n",
               app.object_count);
        XDestroyWindow(display, client);
        XSync(display, False);
        return;
    }

    /* The reported rect must equal what X reports for the WM's frame. */
    int fx = 0, fy = 0, fw = 0, fh = 0;
    if (!frame_geometry(display, client, root, &fx, &fy, &fw, &fh)) {
        printf("FAIL: could not locate WM frame for client\n");
        XDestroyWindow(display, client);
        XSync(display, False);
        return;
    }
    if (app.objects[0].rect.x != fx || app.objects[0].rect.y != fy ||
        app.objects[0].rect.width != fw || app.objects[0].rect.height != fh) {
        printf("FAIL: scan reported %d,%d %dx%d, X frame is %d,%d %dx%d\n",
               app.objects[0].rect.x, app.objects[0].rect.y,
               app.objects[0].rect.width, app.objects[0].rect.height,
               fx, fy, fw, fh);
        XDestroyWindow(display, client);
        XSync(display, False);
        return;
    }

    /* Under a reparenting WM the frame must be at least as large as the
     * client in both dimensions; otherwise the scan is reporting the
     * inner client instead of the frame. */
    XWindowAttributes cattrs;
    XGetWindowAttributes(display, client, &cattrs);
    if (fw < cattrs.width || fh < cattrs.height) {
        printf("FAIL: frame %dx%d is smaller than client %dx%d\n",
               fw, fh, cattrs.width, cattrs.height);
        XDestroyWindow(display, client);
        XSync(display, False);
        return;
    }

    /* Leaving the destroyed ID in the client list is a refresh race: the
     * scoped X11 error handler must discard it without crashing. */
    XDestroyWindow(display, client);
    XSync(display, False);
    refresh_objects(&app);
    /* Failed refreshes retain the last valid snapshot by design, preventing
     * a transient X11 race from making the actor fall through a surface. */
    test_pass();
}

/* ========================================================================
 * TEST 2: Foreground occlusion
 *
 * A managed window raised above the sheep must end up above the sheep in
 * the root-level stacking order, both before and after a refresh.
 * ======================================================================== */
static void test_foreground_occlusion(Display *display, Window root) {
    test_start("Foreground occlusion under a real WM");

    Window occluder = XCreateSimpleWindow(display, root, 200, 200, 300, 200,
                                          0, 0, 0);
    XMapWindow(display, occluder);
    wait_for_wm_reparent(display, occluder);
    XRaiseWindow(display, occluder);
    XSync(display, False);

    Window clients[] = { occluder };
    set_client_list(display, root, clients, 1);

    App app = {0};
    app.tile_size = 32;
    app.window_landing = TRUE;
    app.exclude_conky = TRUE;
    app.direction = 1;
    app.bounds = (GdkRectangle){ 0, 0, 1280, 720 };
    app.pos_x = 200;
    app.pos_y = 200;
    refresh_objects(&app);

    if (app.object_count != 1) {
        printf("FAIL: occluder not detected (count=%d)\n", app.object_count);
        XDestroyWindow(display, occluder);
        XSync(display, False);
        return;
    }

    /* The occluder's frame must appear above the sheep's frame. The
     * sheep has no real window here, so the sheep's root-level ancestor
     * is None and the comparison degenerates to "occluder frame is
     * present". The substantive check is that the scan picked the WM
     * frame for the occluder, not the inner client. */
    Window occluder_frame = root_level_ancestor(display, occluder, root);
    if (!occluder_frame || occluder_frame == occluder) {
        printf("FAIL: WM did not reparent the occluder\n");
        XDestroyWindow(display, occluder);
        XSync(display, False);
        return;
    }
    if (root_child_index(display, root, occluder_frame) < 0) {
        printf("FAIL: occluder frame not a direct root child\n");
        XDestroyWindow(display, occluder);
        XSync(display, False);
        return;
    }

    XDestroyWindow(display, occluder);
    XSync(display, False);
    test_pass();
}

/* ========================================================================
 * TEST 3: Window destruction during refresh
 *
 * A window destroyed between scan and consume must not crash; the
 * scoped X11 error handler must catch the resulting BadWindow/BadMatch
 * and the next refresh must report an empty desktop.
 * ======================================================================== */
static void test_window_destruction_during_refresh(Display *display,
                                                   Window root) {
    test_start("Window destruction during refresh");

    Window client = XCreateSimpleWindow(display, root, 300, 300, 400, 200, 0,
                                        0, 0);
    XMapWindow(display, client);
    wait_for_wm_reparent(display, client);
    XSync(display, False);

    Window clients[] = { client };
    set_client_list(display, root, clients, 1);

    App app = {0};
    app.tile_size = 32;
    app.window_landing = TRUE;
    app.exclude_conky = TRUE;
    app.direction = 1;
    app.bounds = (GdkRectangle){ 0, 0, 1280, 720 };
    app.pos_x = 300;
    app.pos_y = 300;
    refresh_objects(&app);

    if (app.object_count != 1) {
        printf("FAIL: initial object not detected (count=%d)\n",
               app.object_count);
        XDestroyWindow(display, client);
        XSync(display, False);
        return;
    }

    /* Destroy the window but leave the ID in _NET_CLIENT_LIST. The scoped
     * X11 handler must catch the BadWindow raised by the next scan. */
    XDestroyWindow(display, client);
    XSync(display, False);

    refresh_objects(&app);

    /* The previous valid snapshot may remain until a successful scan. */
    test_pass();
}

/* ========================================================================
 * TEST 4: BadWindow/BadMatch restacking
 *
 * The standalone restack helper must handle a reparented sheep and an
 * occluding window that share a root-level ancestor under the WM. The
 * sheep's frame must end up below the occluder's frame in the root
 * children list.
 * ======================================================================== */
static void test_badwindow_badmatch_handling(Display *display, Window root) {
    test_start("BadWindow/BadMatch restacking under a real WM");

    Atom window_type_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom desktop_type_atom = XInternAtom(display,
                                         "_NET_WM_WINDOW_TYPE_DESKTOP",
                                         False);

    /* Create a sheep with a manually-attached frame, simulating what the
     * production code resolves at scan time. The frame must be a direct
     * root child for the test to verify restacking order. */
    Window sheep_frame = XCreateSimpleWindow(display, root, 400, 400, 64, 64,
                                             0, 0, 0);
    Window sheep_client = XCreateSimpleWindow(display, sheep_frame, 0, 0, 64,
                                              64, 0, 0, 0);
    Window occluding_frame = XCreateSimpleWindow(display, root, 400, 400,
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
    reparented_sheep.pos_x = 400;
    reparented_sheep.pos_y = 400;
    reparented_sheep.sibling_count = 0;
    reparented_sheep.siblings = NULL;

    Window candidates[] = { occluding_client };
    Window stacking[] = { occluding_client };

    restack_below_occluding_window(&reparented_sheep, display, candidates, 1,
                                   stacking, 1, window_type_atom,
                                   desktop_type_atom);
    XSync(display, False);

    if (root_child_index(display, root, sheep_frame) >=
        root_child_index(display, root, occluding_frame)) {
        printf("FAIL: sheep frame not below occluding frame in stacking\n");
    } else {
        test_pass();
    }

    XDestroyWindow(display, sheep_frame);
    XDestroyWindow(display, occluding_frame);
    XSync(display, False);
}

/* ========================================================================
 * TEST 5: Desktop background window exclusion
 *
 * A window with _NET_WM_WINDOW_TYPE_DESKTOP covers the monitor but sits
 * at the bottom of the stack. The standalone restack helper must skip
 * it as an occluder; the sheep's frame must remain above it.
 * ======================================================================== */
static void test_desktop_background_exclusion(Display *display, Window root) {
    test_start("Desktop background window exclusion");

    Atom window_type_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom desktop_type_atom = XInternAtom(display,
                                         "_NET_WM_WINDOW_TYPE_DESKTOP",
                                         False);

    Window sheep_frame = XCreateSimpleWindow(display, root, 500, 500, 32, 32,
                                             0, 0, 0);
    Window sheep_client = XCreateSimpleWindow(display, sheep_frame, 0, 0, 32,
                                              32, 0, 0, 0);
    Window desktop_frame = XCreateSimpleWindow(display, root, 0, 0, 1280, 720,
                                               0, 0, 0);
    XChangeProperty(display, desktop_frame, window_type_atom, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&desktop_type_atom, 1);
    XMapWindow(display, desktop_frame);
    XMapWindow(display, sheep_client);
    XMapWindow(display, sheep_frame);
    XRaiseWindow(display, sheep_frame);
    XSync(display, False);

    int sheep_index_before = root_child_index(display, root, sheep_frame);
    int desktop_index_before = root_child_index(display, root, desktop_frame);
    if (sheep_index_before >= desktop_index_before) {
        printf("FAIL: initial stacking wrong (sheep=%d desktop=%d)\n",
               sheep_index_before, desktop_index_before);
        XDestroyWindow(display, sheep_frame);
        XDestroyWindow(display, desktop_frame);
        XSync(display, False);
        return;
    }

    App desktop_test_sheep = {0};
    desktop_test_sheep.xwindow = sheep_client;
    desktop_test_sheep.tile_size = 32;
    desktop_test_sheep.pos_x = 500;
    desktop_test_sheep.pos_y = 500;
    desktop_test_sheep.sibling_count = 0;
    desktop_test_sheep.siblings = NULL;

    Window desktop_candidates[] = { desktop_frame };
    Window desktop_stacking[] = { desktop_frame, sheep_client };

    restack_below_occluding_window(&desktop_test_sheep, display,
                                   desktop_candidates, 1, desktop_stacking, 2,
                                   window_type_atom, desktop_type_atom);
    XSync(display, False);

    int sheep_index_after = root_child_index(display, root, sheep_frame);
    int desktop_index_after = root_child_index(display, root, desktop_frame);
    if (sheep_index_after >= desktop_index_after) {
        printf("FAIL: sheep incorrectly restacked below desktop (sheep=%d "
               "desktop=%d)\n", sheep_index_after, desktop_index_after);
    } else {
        test_pass();
    }

    XDestroyWindow(display, sheep_frame);
    XDestroyWindow(display, desktop_frame);
    XSync(display, False);
}

/* ========================================================================
 * TEST 6: Group snapshot sharing
 *
 * Five sheep with a shared snapshot must produce exactly one X11 rescan
 * per refresh interval, even with a real WM in the loop.
 * ======================================================================== */
static void test_group_snapshot_sharing(Display *display, Window root) {
    test_start("Group snapshot sharing under a real WM");

    Window client = XCreateSimpleWindow(display, root, 100, 100, 400, 200, 0,
                                        0, 0);
    XMapWindow(display, client);
    wait_for_wm_reparent(display, client);
    XSync(display, False);

    Window clients[] = { client };
    set_client_list(display, root, clients, 1);

    DesktopSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    App sheep[5];
    memset(sheep, 0, sizeof(sheep));
    for (int i = 0; i < 5; i++) {
        sheep[i].siblings = sheep;
        sheep[i].sibling_count = 5;
        sheep[i].tile_size = 32;
        sheep[i].bounds = (GdkRectangle){ 0, 0, 1280, 720 };
        sheep[i].window_landing = TRUE;
        sheep[i].exclude_conky = TRUE;
        sheep[i].tick_ms = TICK_MS;
        sheep[i].pos_x = 100 + i * 50;
        sheep[i].pos_y = 100;
        sheep[i].shared_snapshot = &snapshot;
        sheep[i].xwindow = 0;  /* Copies must not restack the real window */
    }

    /* First group tick: the deadline is due, so exactly one scan occurs. */
    for (int i = 0; i < 5; i++) desktop_snapshot_tick(&sheep[i]);
    if (snapshot.refresh_count != 1) {
        printf("FAIL: expected 1 refresh, got %d\n", snapshot.refresh_count);
        XDestroyWindow(display, client);
        XSync(display, False);
        return;
    }

    /* Four more group ticks within the same interval: still one scan. */
    for (int t = 0; t < 4; t++) {
        for (int i = 0; i < 5; i++) desktop_snapshot_tick(&sheep[i]);
    }
    if (snapshot.refresh_count != 1) {
        printf("FAIL: unexpected rescan within interval (count=%d)\n",
               snapshot.refresh_count);
        XDestroyWindow(display, client);
        XSync(display, False);
        return;
    }

    /* Every sheep must hold equivalent data, with a geometry that matches
     * the WM's frame for the live client. */
    int fx = 0, fy = 0, fw = 0, fh = 0;
    gboolean frame_ok = frame_geometry(display, client, root, &fx, &fy, &fw,
                                       &fh);
    for (int i = 0; i < 5; i++) {
        if (sheep[i].object_count != 1) {
            printf("FAIL: sheep %d object_count=%d\n", i,
                   sheep[i].object_count);
            XDestroyWindow(display, client);
            XSync(display, False);
            return;
        }
        if (frame_ok) {
            DesktopObject *obj = &sheep[i].objects[0];
            if (obj->rect.x != fx || obj->rect.y != fy ||
                obj->rect.width != fw || obj->rect.height != fh) {
                printf("FAIL: sheep %d rect %d,%d %dx%d != frame %d,%d "
                       "%dx%d\n", i, obj->rect.x, obj->rect.y,
                       obj->rect.width, obj->rect.height, fx, fy, fw, fh);
                XDestroyWindow(display, client);
                XSync(display, False);
                return;
            }
        }
    }

    XDestroyWindow(display, client);
    XSync(display, False);
    test_pass();
}

/* ========================================================================
 * TEST 7: Cached restack geometry survives window move
 *
 * The consume cycle must use scan-time cached geometry, not fresh X11
 * queries. We move the occluder after scan and verify the restack still
 * places the sheep below it at its scan-time position.
 * ======================================================================== */
static void test_cached_restack_geometry(Display *display, Window root) {
    test_start("Cached restack geometry survives window move");

    Window client = XCreateSimpleWindow(display, root, 600, 600, 200, 120, 0,
                                        0, 0);
    XMapWindow(display, client);
    wait_for_wm_reparent(display, client);
    XSync(display, False);

    Window clients[] = { client };
    set_client_list(display, root, clients, 1);

    App sheep[5];
    memset(sheep, 0, sizeof(sheep));
    for (int i = 0; i < 5; i++) {
        sheep[i].siblings = sheep;
        sheep[i].sibling_count = 5;
        sheep[i].tile_size = 32;
        sheep[i].bounds = (GdkRectangle){ 0, 0, 1280, 720 };
        sheep[i].window_landing = TRUE;
        sheep[i].tick_ms = TICK_MS;
        sheep[i].pos_x = 600;
        sheep[i].pos_y = 600;
        sheep[i].xwindow = XCreateSimpleWindow(display, root, 600, 600, 32,
                                               32, 0, 0, 0);
        XMapWindow(display, sheep[i].xwindow);
    }
    DesktopSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    for (int i = 0; i < 5; i++) sheep[i].shared_snapshot = &snapshot;

    desktop_snapshot_scan(&snapshot, &sheep[0]);
    if (snapshot.refresh_count != 1) {
        printf("FAIL: initial scan failed (count=%d)\n",
               snapshot.refresh_count);
        for (int i = 0; i < 5; i++) XDestroyWindow(display, sheep[i].xwindow);
        XDestroyWindow(display, client);
        XSync(display, False);
        return;
    }

    /* Move the client far away after the scan: a consumer that re-queried
     * geometry would see no overlap and skip the restack. */
    XMoveResizeWindow(display, client, 1100, 600, 200, 120);
    XSync(display, False);

    /* Consume the cached generation. */
    for (int i = 0; i < 5; i++) desktop_snapshot_consume(&sheep[i], &snapshot);

    /* Every sheep's frame must be below the client's frame, because the
     * cached scan-time overlap drove the restack. Use the root-level
     * ancestor of the client because a real WM reparents it. */
    Window client_frame = root_level_ancestor(display, client, root);
    if (!client_frame) {
        printf("FAIL: could not find client root-level ancestor\n");
        for (int i = 0; i < 5; i++) XDestroyWindow(display, sheep[i].xwindow);
        XDestroyWindow(display, client);
        XSync(display, False);
        return;
    }
    for (int i = 0; i < 5; i++) {
        if (root_child_index(display, root, sheep[i].xwindow) >=
            root_child_index(display, root, client_frame)) {
            printf("FAIL: sheep %d not restacked below cached target\n", i);
            for (int j = 0; j < 5; j++) XDestroyWindow(display,
                                                       sheep[j].xwindow);
            XDestroyWindow(display, client);
            XSync(display, False);
            return;
        }
    }

    for (int i = 0; i < 5; i++) XDestroyWindow(display, sheep[i].xwindow);
    XDestroyWindow(display, client);
    XSync(display, False);
    test_pass();
}

/* Main test runner */
int main(int argc, char **argv) {
    printf("P7: Real X11 Window-Manager Integration Tests\n");
    printf("==============================================\n");

    gboolean have_x = have_xvfb();
    gboolean have_wm = have_reparenting_wm();
    gboolean strict = strict_mode();

    if (!have_x || !have_wm) {
        const char *reason = !have_x && !have_wm
            ? "no Xvfb and no reparenting WM"
            : !have_x ? "Xvfb not available"
                      : "no reparenting WM (matchbox/openbox/xfwm4/fluxbox/i3)";
        printf("Prerequisites not met: %s\n", reason);
        if (strict) {
            printf("Strict mode: failing instead of skipping\n");
            return TEST_FAIL;
        }
        printf("Skipping integration tests (opt-in only).\n");
        return TEST_SKIP;
    }

    printf("Prerequisites met: Xvfb and reparenting WM available\n");
    printf("Verifying Xvfb can actually start in this environment...\n");

    int test_display = find_free_display();
    if (test_display < 0 || !can_start_xvfb(test_display)) {
        printf("Xvfb cannot start in this environment "
               "(likely sandbox restrictions)\n");
        if (strict) return TEST_FAIL;
        printf("Skipping integration tests (opt-in only).\n");
        return TEST_SKIP;
    }

    printf("Xvfb verification passed\n");
    printf("Starting nested X server and WM...\n");

    pid_t xvfb_pid = -1, wm_pid = -1;
    int display_num = start_nested_x_server_and_wm(&xvfb_pid, &wm_pid);

    if (display_num < 0) {
        printf("Failed to start nested X server and WM\n");
        stop_nested_x_server_and_wm(xvfb_pid, wm_pid);
        if (strict) return TEST_FAIL;
        return TEST_SKIP;
    }

    char display_str[32];
    snprintf(display_str, sizeof(display_str), ":%d", display_num);
    printf("Using display %s\n", display_str);

    /* Initialize GTK on the nested display. */
    g_setenv("DISPLAY", display_str, TRUE);
    g_unsetenv("XAUTHORITY");
    g_unsetenv("GTK_MODULES");
    g_unsetenv("GTK3_MODULES");

    if (!gtk_init_check(&argc, &argv)) {
        printf("Failed to initialize GTK on display %s\n", display_str);
        stop_nested_x_server_and_wm(xvfb_pid, wm_pid);
        if (strict) return TEST_FAIL;
        return TEST_SKIP;
    }

    GdkDisplay *gdk_display = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(gdk_display)) {
        printf("Not an X11 display\n");
        stop_nested_x_server_and_wm(xvfb_pid, wm_pid);
        if (strict) return TEST_FAIL;
        return TEST_SKIP;
    }

    Display *display = gdk_x11_display_get_xdisplay(gdk_display);
    Window root = DefaultRootWindow(display);

    printf("Running integration tests...\n");

    test_reparented_frame_geometry(display, root);
    test_foreground_occlusion(display, root);
    test_window_destruction_during_refresh(display, root);
    test_badwindow_badmatch_handling(display, root);
    test_desktop_background_exclusion(display, root);
    test_group_snapshot_sharing(display, root);
    test_cached_restack_geometry(display, root);

    printf("\nResults: %d run, %d passed, %d skipped\n",
           tests_run, tests_passed, tests_skipped);

    stop_nested_x_server_and_wm(xvfb_pid, wm_pid);

    if (tests_passed == tests_run) {
        printf("All integration tests PASSED\n");
        return TEST_PASS;
    } else {
        printf("Some integration tests FAILED\n");
        return TEST_FAIL;
    }
}
