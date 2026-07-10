#define _GNU_SOURCE
#include "storage.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

// mkdir -p equivalent: walks the path, temporarily null-terminating
// at each '/' to create one directory level at a time, since mkdir()
// itself only creates one level and fails if the parent is missing.
static void mkdir_p(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);

    for (char *p = tmp + 1; *p; p++) { // +1 skips the leading '/'
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                log_msg(LOG_WARN, "mkdir failed for %s", tmp);
            *p = '/'; // restore and keep walking
        }
    }
    // final path component (after the last '/') still needs creating
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        log_msg(LOG_WARN, "mkdir failed for %s", tmp);
}

void storage_build_dir(const config_t *cfg, const char *app_name, char *out, size_t out_sz) {
    char app_safe[128];
    snprintf(app_safe, sizeof(app_safe), "%s", app_name);
    for (char *p = app_safe; *p; p++) if (*p == ' ') *p = '_'; // "Telegram Desktop" -> "Telegram_Desktop"

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char date_dir[32];
    strftime(date_dir, sizeof(date_dir), "%Y-%m-%d", &tm_now);

    snprintf(out, out_sz, "%s/%s/%s", cfg->archive_root, app_safe, date_dir);
    mkdir_p(out);
}

// Minimal JSON string escaper: handles quotes, backslashes, newlines,
// and drops other control characters. Message content is untrusted
// (comes straight off the bus), so this matters for correctness, not
// just cleanliness -- an unescaped quote would corrupt the JSON line.
static void json_escape(const char *in, char *out, size_t out_sz) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j < out_sz - 2; i++) {
        unsigned char c = in[i];
        if (c == '"' || c == '\\') { out[j++] = '\\'; out[j++] = c; }
        else if (c == '\n') { out[j++] = '\\'; out[j++] = 'n'; }
        else if (c < 0x20) continue; // drop other control chars
        else out[j++] = c;
    }
    out[j] = '\0';
}

void storage_write_entry(const char *dir, const char *group_name, const char *source_app,
                          const parsed_message_t *msg, const char *screenshot_path) {
    char log_path[900];
    snprintf(log_path, sizeof(log_path), "%s/log.jsonl", dir);

    FILE *f = fopen(log_path, "a");
    if (!f) {
        log_msg(LOG_ERROR, "failed to open %s for append", log_path);
        return;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char iso_ts[64];
    strftime(iso_ts, sizeof(iso_ts), "%Y-%m-%dT%H:%M:%S", &tm_now);

    char esc_sender[256], esc_content[4096], esc_group[256];
    json_escape(msg->sender, esc_sender, sizeof(esc_sender));
    json_escape(msg->content, esc_content, sizeof(esc_content));

    char chat_group_field[300]; // JSON field for group-CHAT (Family Group etc), unrelated to app grouping
    if (msg->is_group) {
        json_escape(msg->group, esc_group, sizeof(esc_group));
        snprintf(chat_group_field, sizeof(chat_group_field), "\"%s\"", esc_group);
    } else {
        snprintf(chat_group_field, sizeof(chat_group_field), "null");
    }

    if (screenshot_path) {
        fprintf(f,
            "{\"timestamp\":\"%s\",\"app\":\"%s\",\"source_app\":\"%s\",\"group\":%s,"
            "\"sender\":\"%s\",\"content\":\"%s\",\"screenshot\":\"%s\"}\n",
            iso_ts, group_name, source_app, chat_group_field, esc_sender, esc_content, screenshot_path);
    } else {
        fprintf(f,
            "{\"timestamp\":\"%s\",\"app\":\"%s\",\"source_app\":\"%s\",\"group\":%s,"
            "\"sender\":\"%s\",\"content\":\"%s\",\"screenshot\":null}\n",
            iso_ts, group_name, source_app, chat_group_field, esc_sender, esc_content);
    }

    fclose(f);
}
