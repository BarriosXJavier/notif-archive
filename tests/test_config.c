#define _POSIX_C_SOURCE 200809L
#include "../src/config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_text_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    assert(fputs(text, f) != EOF);
    assert(fclose(f) == 0);
}

static void test_crlf_and_first_duplicate_wins(void) {
    char path[] = "/tmp/notif-config-XXXXXX";
    char group[MAX_APP_NAME];
    config_t cfg;
    int fd = mkstemp(path);

    assert(fd >= 0);
    assert(close(fd) == 0);
    write_text_file(path,
                    "archive_root=/tmp/archive\r\n"
                    "screenshot_delay_ms=0\r\n"
                    "screenshot_timeout_ms=500\r\n"
                    "\r\n[apps]\r\n"
                    "App = First Group\r\n"
                    "App = Second Group\r\n"
                    "Other = First Group\r\n");

    assert(config_load(path, &cfg) == 0);
    assert(cfg.app_count == 2);
    assert(cfg.screenshot_delay_ms == 0);
    assert(cfg.screenshot_timeout_ms == 500);
    assert(config_resolve_group(&cfg, "App", group, sizeof(group)) == 1);
    assert(strcmp(group, "First Group") == 0);
    assert(config_resolve_group(&cfg, "Other", group, sizeof(group)) == 1);
    assert(strcmp(group, "First Group") == 0);
    assert(unlink(path) == 0);
}

static void test_missing_file_fails(void) {
    config_t cfg;
    assert(config_load("/tmp/notif-config-file-that-does-not-exist", &cfg) == -1);
}

static void test_invalid_numeric_value_fails(void) {
    char path[] = "/tmp/notif-config-XXXXXX";
    config_t cfg;
    int fd = mkstemp(path);

    assert(fd >= 0);
    assert(close(fd) == 0);
    write_text_file(path,
                    "screenshot_delay_ms=999999999999\n"
                    "[apps]\nApp\n");
    assert(config_load(path, &cfg) == -1);
    assert(unlink(path) == 0);
}

static void test_catch_all_is_separate_from_exact_mappings(void) {
    char path[] = "/tmp/notif-config-XXXXXX";
    char group[MAX_APP_NAME];
    config_t cfg;
    int fd = mkstemp(path);

    assert(fd >= 0);
    assert(close(fd) == 0);
    write_text_file(path,
                    "[apps]\n"
                    "Known = Sorted\n"
                    "* = Unsorted\n");
    assert(config_load(path, &cfg) == 0);
    assert(cfg.app_count == 1);
    assert(cfg.has_catch_all == 1);
    assert(config_resolve_group(&cfg, "Known", group, sizeof(group)) == 1);
    assert(strcmp(group, "Sorted") == 0);
    assert(config_resolve_group(&cfg, "Unknown", group, sizeof(group)) == 0);
    assert(config_resolve_catch_all(&cfg, group, sizeof(group)) == 1);
    assert(strcmp(group, "Unsorted") == 0);
    assert(unlink(path) == 0);
}

static void test_duplicate_catch_all_is_rejected(void) {
    char path[] = "/tmp/notif-config-XXXXXX";
    config_t cfg;
    int fd = mkstemp(path);

    assert(fd >= 0);
    assert(close(fd) == 0);
    write_text_file(path,
                    "[apps]\n"
                    "* = First\n"
                    "* = Second\n");
    assert(config_load(path, &cfg) == -1);
    assert(cfg.has_catch_all == 1);
    assert(strcmp(cfg.catch_all_group, "First") == 0);
    assert(unlink(path) == 0);
}

static void test_overlong_mapping_is_rejected_not_truncated(void) {
    char path[] = "/tmp/notif-config-XXXXXX";
    char long_name[300];
    char text[360];
    config_t cfg;
    int fd = mkstemp(path);

    assert(fd >= 0);
    assert(close(fd) == 0);
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    snprintf(text, sizeof(text), "[apps]\n%s\n", long_name);
    write_text_file(path, text);
    assert(config_load(path, &cfg) == -1);
    assert(cfg.app_count == 0);
    assert(unlink(path) == 0);
}

int main(void) {
    test_crlf_and_first_duplicate_wins();
    test_missing_file_fails();
    test_invalid_numeric_value_fails();
    test_catch_all_is_separate_from_exact_mappings();
    test_duplicate_catch_all_is_rejected();
    test_overlong_mapping_is_rejected_not_truncated();
    printf("config tests passed\n");
    return 0;
}
