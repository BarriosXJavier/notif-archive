#define _POSIX_C_SOURCE 200809L
#include "../src/screenshot.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static long long elapsed_ms(const struct timespec *start,
                            const struct timespec *end) {
    return (long long)(end->tv_sec - start->tv_sec) * 1000LL +
           (end->tv_nsec - start->tv_nsec) / 1000000LL;
}

int main(void) {
    char root[] = "/tmp/notif-screenshot-XXXXXX";
    char tool_path[512];
    const char *path_environment = getenv("PATH");
    char *old_path = path_environment ? strdup(path_environment) : NULL;
    config_t cfg;
    struct timespec start;
    struct timespec end;
    FILE *f;

    assert(mkdtemp(root) != NULL);
    assert(snprintf(tool_path, sizeof(tool_path), "%s/scrot", root) > 0);
    f = fopen(tool_path, "w");
    assert(f != NULL);
    assert(fputs("#!/bin/sh\nprintf x > \"$1\"\n/bin/sleep 5\n", f) != EOF);
    assert(fclose(f) == 0);
    assert(chmod(tool_path, 0700) == 0);
    assert(setenv("PATH", root, 1) == 0);

    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.session_type, sizeof(cfg.session_type), "%s", "x11");
    cfg.screenshot_timeout_ms = 100;
    assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
    assert(screenshot_capture(&cfg, "/tmp/notif-screenshot-unused.png", NULL) == 0);
    assert(clock_gettime(CLOCK_MONOTONIC, &end) == 0);
    assert(elapsed_ms(&start, &end) < 2000);
    assert(access("/tmp/notif-screenshot-unused.png", F_OK) < 0);

    {
        volatile sig_atomic_t cancelled = 1;
        cfg.screenshot_delay_ms = 600000;
        assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
        assert(screenshot_capture(&cfg, "/tmp/notif-screenshot-unused.png",
                                  &cancelled) == 0);
        assert(clock_gettime(CLOCK_MONOTONIC, &end) == 0);
        assert(elapsed_ms(&start, &end) < 1000);
    }

    if (old_path) {
        assert(setenv("PATH", old_path, 1) == 0);
        free(old_path);
    } else {
        assert(unsetenv("PATH") == 0);
    }
    assert(unlink(tool_path) == 0);
    assert(rmdir(root) == 0);
    printf("screenshot tests passed\n");
    return 0;
}
