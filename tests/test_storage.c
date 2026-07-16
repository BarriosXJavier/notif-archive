#define _GNU_SOURCE
#include "../src/storage.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_paths_json_and_permissions(void) {
    char root[] = "/tmp/notif-storage-XXXXXX";
    char dir[MAX_PATH_LEN];
    char log_path[MAX_PATH_LEN];
    char line[8192];
    config_t cfg;
    parsed_message_t msg;
    struct stat st;
    FILE *f;
    const time_t event_time = 1700000000;

    assert(mkdtemp(root) != NULL);
    memset(&cfg, 0, sizeof(cfg));
    assert(snprintf(cfg.archive_root, sizeof(cfg.archive_root), "%s", root) > 0);

    assert(storage_build_dir(&cfg, "A B/..", event_time, dir, sizeof(dir)) == 0);
    assert(strstr(dir, "/A%20B%2F%2E%2E/") != NULL);
    assert(stat(dir, &st) == 0);
    assert(S_ISDIR(st.st_mode));
    assert((st.st_mode & 0777) == 0700);

    memset(&msg, 0, sizeof(msg));
    snprintf(msg.sender, sizeof(msg.sender), "%s", "Zoë \"Admin\"");
    snprintf(msg.content, sizeof(msg.content), "%s", "line 1\nline 2 \\ 🌍");
    assert(storage_write_entry(dir, "A B/..", "source\"app", 42, event_time,
                               &msg, "/tmp/shot\"name.png") == 0);

    assert(snprintf(log_path, sizeof(log_path), "%s/log.jsonl", dir) > 0);
    assert(stat(log_path, &st) == 0);
    assert((st.st_mode & 0777) == 0600);
    f = fopen(log_path, "r");
    assert(f != NULL);
    assert(fgets(line, sizeof(line), f) != NULL);
    assert(fclose(f) == 0);
    assert(strstr(line, "\"source_app\":\"source\\\"app\"") != NULL);
    assert(strstr(line, "\"replaces_id\":42") != NULL);
    assert(strstr(line, "line 1\\nline 2 \\\\ 🌍") != NULL);
    assert(strstr(line, "shot\\\"name.png") != NULL);

    assert(unlink(log_path) == 0);
    assert(rmdir(dir) == 0);
    {
        char group_dir[MAX_PATH_LEN];
        char *slash;
        snprintf(group_dir, sizeof(group_dir), "%s", dir);
        slash = strrchr(group_dir, '/');
        assert(slash != NULL);
        *slash = '\0';
        assert(rmdir(group_dir) == 0);
    }
    assert(rmdir(root) == 0);
}

static void test_small_output_buffer_fails_without_truncation(void) {
    char root[] = "/tmp/notif-storage-XXXXXX";
    char out[8] = "sentinel";
    config_t cfg;

    assert(mkdtemp(root) != NULL);
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.archive_root, sizeof(cfg.archive_root), "%s", root);
    assert(storage_build_dir(&cfg, "Group", 1700000000, out, sizeof(out)) == -1);
    assert(out[0] == '\0');
    assert(rmdir(root) == 0);
}

int main(void) {
    test_paths_json_and_permissions();
    test_small_output_buffer_fails_without_truncation();
    printf("storage tests passed\n");
    return 0;
}
