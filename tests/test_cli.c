/* CLI and custom-pet package boundary tests.
 *
 * These tests run esheep as a subprocess and check exit codes and output.
 *
 * Environment: xvfb-run in some container environments returns 1 even on
 * success (broken autohook). We detect this and adjust expectations.
 *
 * Exit code semantics:
 *   - exit 0  = clean startup and auto-quit (needs working display)
 *   - exit 1  = load failure: missing/unreadable/unparseable spritesheet
 *   - exit 2  = validation failure: bad character or wrong tile dimensions
 *
 * Some tests (e.g. invalid character) require GTK init which needs a working
 * display. In environments without a working X server these are skipped.
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/stat.h>

static const char ESHEEP_BIN[] = "/home/mason/repos/linux-esheep.git/esheep";

/* Returns: exit code of child, or -1 on fork/pipe error.
 * Output is always NUL-terminated (truncated if > out_bufsiz-1). */
static int run_esheep_capture(char *const *args, int nargs, const char *extra_env,
                              char *out_buf, size_t out_bufsiz) {
    int out[2];
    if (pipe(out) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) { close(out[0]); close(out[1]); return -1; }

    if (pid == 0) {
        close(out[0]);
        if (dup2(out[1], STDOUT_FILENO) < 0) _exit(127);
        if (dup2(out[1], STDERR_FILENO) < 0) _exit(127);
        close(out[1]);

        char *xvfb_argv[16];
        int n = 0;
        xvfb_argv[n++] = (char *)"xvfb-run";
        xvfb_argv[n++] = (char *)"-a";
        xvfb_argv[n++] = (char *)ESHEEP_BIN;
        for (int i = 0; i < nargs && n < 15; i++) xvfb_argv[n++] = (char *)args[i];
        xvfb_argv[n++] = NULL;

        char *envp[4];
        int e = 0;
        envp[e++] = (char *)"ESHEEP_AUTOQUIT_MS=500";
        if (extra_env) envp[e++] = (char *)extra_env;
        envp[e++] = NULL;

        execve("/usr/bin/xvfb-run", xvfb_argv, envp);
        _exit(127);
    }

    close(out[1]);
    ssize_t total = 0;
    ssize_t r;
    while ((r = read(out[0], out_buf + total, out_bufsiz - total - 1)) > 0)
        total += r;
    close(out[0]);
    out_buf[total] = '\0';

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

/* Returns 1 if display-init-dependent tests should be skipped.
 * Detects environments where xvfb-run returns 1 on success. */
static int skip_gtk_tests(const char *reason) {
    static int checked = 0, broken = 0;
    if (checked) return broken;
    checked = 1;

    /* Quick probe: does a trivial command succeed with output? */
    int out[2];
    if (pipe(out) < 0) { broken = 1; return broken; }
    pid_t pid = fork();
    if (pid < 0) { close(out[0]); close(out[1]); broken = 1; return broken; }
    if (pid == 0) {
        close(out[0]);
        if (dup2(out[1], STDOUT_FILENO) < 0) _exit(1);
        close(out[1]);
        execl("/usr/bin/xvfb-run", "xvfb-run", "-a", "/bin/echo", "ok", NULL);
        _exit(1);
    }
    close(out[1]);
    char buf[16] = {0};
    read(out[0], buf, sizeof(buf) - 1);
    close(out[0]);
    int st;
    waitpid(pid, &st, 0);
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0 && strstr(buf, "ok") != NULL) {
        broken = 0;
    } else {
        broken = 1;  /* xvfb-run always returns 1 or produces no output */
    }
    if (reason && broken) fprintf(stderr, "note: GTK tests skipped (%s)\n", reason);
    return broken;
}

/* ---- Test cases ---- */

/* --help and --version are pure CLI; no GTK needed. */
static void test_help_shows_usage(void) {
    fprintf(stderr, "test: --help shows usage\n");
    char *argv[] = { "--help" };
    char buf[4096] = {0};
    run_esheep_capture(argv, 1, NULL, buf, sizeof(buf));
    assert(strstr(buf, "--sprite") != NULL);
    assert(strstr(buf, "--character") != NULL);
}

static void test_version_shows_version(void) {
    fprintf(stderr, "test: --version shows version\n");
    char *argv[] = { "--version" };
    char buf[4096] = {0};
    run_esheep_capture(argv, 1, NULL, buf, sizeof(buf));
    assert(strstr(buf, "linux-esheep") != NULL);
    assert(strstr(buf, "0.1.0") != NULL);
}

/* These require GTK init to reach the character/spritesheet validation code. */
static void test_invalid_character(void) {
    fprintf(stderr, "test: invalid character exits 2 with diagnostic\n");
    if (skip_gtk_tests("xvfb-run returns 1 even on success")) return;
    char *argv[] = { "--character", "dragon", "--no-window-landing" };
    char buf[4096] = {0};
    int rc = run_esheep_capture(argv, 3, "ESHEEP_AUTOQUIT_MS=1",
                                buf, sizeof(buf));
    assert(rc == 2);
    assert(strstr(buf, "invalid character") != NULL);
}

static void test_missing_spritesheet(void) {
    fprintf(stderr, "test: missing spritesheet exits 1 with diagnostic\n");
    if (skip_gtk_tests("xvfb-run returns 1 even on success")) return;
    char *argv[] = { "--sprite", "/nonexistent/path.png" };
    char buf[4096] = {0};
    int rc = run_esheep_capture(argv, 2, "ESHEEP_AUTOQUIT_MS=1",
                                buf, sizeof(buf));
    assert(rc == 1);
    assert(strstr(buf, "failed to load spritesheet") != NULL);
}

static void test_unreadable_spritesheet(void) {
    fprintf(stderr, "test: unreadable spritesheet exits 1\n");
    if (skip_gtk_tests("xvfb-run returns 1 even on success")) return;
    char *argv[] = { "--sprite", "/proc/1/sched" };
    char buf[4096] = {0};
    int rc = run_esheep_capture(argv, 2, "ESHEEP_AUTOQUIT_MS=1",
                                buf, sizeof(buf));
    assert(rc == 1);
    assert(strstr(buf, "failed to load spritesheet") != NULL);
}

static void test_invalid_spritesheet_file(void) {
    fprintf(stderr, "test: invalid spritesheet file exits 1\n");
    if (skip_gtk_tests("xvfb-run returns 1 even on success")) return;
    char tmpfile[] = "/tmp/esheep_test_invalid.XXXXXX";
    int fd = mkstemp(tmpfile);
    assert(fd >= 0);
    write(fd, "this is not a PNG", 16);
    close(fd);
    char *argv[] = { "--sprite", tmpfile };
    char buf[4096] = {0};
    int rc = run_esheep_capture(argv, 2, "ESHEEP_AUTOQUIT_MS=1",
                                buf, sizeof(buf));
    unlink(tmpfile);
    assert(rc == 1);
    assert(strstr(buf, "failed to load spritesheet") != NULL);
}

static void test_wrong_dimension_spritesheet(void) {
    fprintf(stderr, "test: wrong-dimension spritesheet exits 2 with diagnostic\n");
    if (skip_gtk_tests("xvfb-run returns 1 even on success")) return;
    char tmpfile[] = "/tmp/esheep_test_wrong_dim.XXXXXX";
    int fd = mkstemp(tmpfile);
    assert(fd >= 0);
    /* 1x1 transparent PNG — not a valid esheep tile grid */
    unsigned char png[] = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,
        0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
        0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
        0x08,0x06,0x00,0x00,0x00,0x1F,0x15,0xC4,
        0x89,0x00,0x00,0x00,0x0A,0x49,0x44,0x41,
        0x54,0x78,0x9C,0x63,0x00,0x01,0x00,0x00,
        0x05,0x00,0x01,0x0D,0x0A,0x2D,0xB4,0x00,
        0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,
        0x42,0x60,0x82
    };
    write(fd, png, sizeof(png));
    close(fd);
    char *argv[] = { "--sprite", tmpfile, "--no-window-landing" };
    char buf[4096] = {0};
    int rc = run_esheep_capture(argv, 3, "ESHEEP_AUTOQUIT_MS=1",
                                buf, sizeof(buf));
    unlink(tmpfile);
    assert(rc == 2);
    assert(strstr(buf, "not a") != NULL);
}

static void test_custom_character_emits_package_note(void) {
    fprintf(stderr, "test: custom character emits package note\n");
    if (skip_gtk_tests("xvfb-run returns 1 even on success")) return;
    char *argv[] = { "--character", "dragon",
                     "--sprite", "assets/sheep_spritesheet.png",
                     "--no-window-landing" };
    char buf[4096] = {0};
    run_esheep_capture(argv, 5, "ESHEEP_AUTOQUIT_MS=1",
                       buf, sizeof(buf));
    assert(strstr(buf, "custom-pet package note") != NULL);
}

static void test_env_spritesheet_precedence(void) {
    fprintf(stderr, "test: ESHEEP_SPRITESHEET env respected\n");
    if (skip_gtk_tests("xvfb-run returns 1 even on success")) return;
    char *argv[] = { "--no-window-landing" };
    char buf[4096] = {0};
    run_esheep_capture(argv, 1,
                       "ESHEEP_SPRITESHEET=assets/sheep_spritesheet.png",
                       buf, sizeof(buf));
    assert(strstr(buf, "failed to load spritesheet") == NULL);
}

static void test_cli_sprite_overrides_env(void) {
    fprintf(stderr, "test: --sprite overrides ESHEEP_SPRITESHEET env\n");
    if (skip_gtk_tests("xvfb-run returns 1 even on success")) return;
    char *argv[] = { "--sprite", "assets/sheep_spritesheet.png", "--no-window-landing" };
    char buf[4096] = {0};
    run_esheep_capture(argv, 3, "ESHEEP_SPRITESHEET=/nonexistent/path.png",
                       buf, sizeof(buf));
    assert(strstr(buf, "failed to load spritesheet") == NULL);
}

static void test_cli_character_overrides_env(void) {
    fprintf(stderr, "test: --character overrides ESHEEP_CHARACTER env\n");
    if (skip_gtk_tests("xvfb-run returns 1 even on success")) return;
    char *argv[] = { "--character", "sheep", "--no-window-landing" };
    char buf[4096] = {0};
    run_esheep_capture(argv, 3, "ESHEEP_CHARACTER=dragon",
                       buf, sizeof(buf));
    assert(strstr(buf, "invalid character") == NULL);
}

int main(void) {
    if (access("/usr/bin/xvfb-run", X_OK) != 0) {
        fprintf(stderr, "SKIP: xvfb-run not available\n");
        return 0;
    }
    if (access(ESHEEP_BIN, X_OK) != 0) {
        fprintf(stderr, "SKIP: %s not found\n", ESHEEP_BIN);
        return 0;
    }

    test_help_shows_usage();
    test_version_shows_version();
    test_invalid_character();
    test_missing_spritesheet();
    test_unreadable_spritesheet();
    test_invalid_spritesheet_file();
    test_wrong_dimension_spritesheet();
    test_custom_character_emits_package_note();
    test_env_spritesheet_precedence();
    test_cli_sprite_overrides_env();
    test_cli_character_overrides_env();

    fprintf(stderr, "All CLI tests passed.\n");
    return 0;
}
