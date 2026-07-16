#define _GNU_SOURCE
#include "storage.h"
#include "log.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static int mkdir_one(const char *path) {
    struct stat st;

    if (mkdir(path, 0700) == 0)
        return 0;
    if (errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;
    log_msg(LOG_ERROR, "cannot create archive directory %s: %s", path,
            strerror(errno));
    return -1;
}

static int mkdir_generated_dir(const char *path) {
    struct stat st;

    if (mkdir(path, 0700) == 0)
        return 0;
    if (errno == EEXIST && lstat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;
    log_msg(LOG_ERROR,
            "generated archive path is not a real directory %s: %s", path,
            strerror(errno));
    return -1;
}

static int mkdir_p(const char *path) {
    char tmp[MAX_PATH_LEN];
    int n;

    n = snprintf(tmp, sizeof(tmp), "%s", path);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        log_msg(LOG_ERROR, "archive directory path is too long");
        return -1;
    }

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (tmp[0] != '\0' && mkdir_one(tmp) < 0)
                return -1;
            *p = '/';
        }
    }
    return mkdir_one(tmp);
}

// Percent encoding is injective, prevents traversal, and avoids collisions such
// as "A B" and "A_B" that occurred with the old space-to-underscore mapping.
static int encode_group_name(const char *group_name, char *out, size_t out_sz) {
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;

    if (!group_name || group_name[0] == '\0')
        return -1;
    for (size_t i = 0; group_name[i] != '\0'; i++) {
        unsigned char c = (unsigned char)group_name[i];
        int safe = isalnum(c) || c == '-' || c == '_';

        if (safe) {
            if (j + 1 >= out_sz)
                return -1;
            out[j++] = (char)c;
        } else {
            if (j + 3 >= out_sz)
                return -1;
            out[j++] = '%';
            out[j++] = hex[c >> 4];
            out[j++] = hex[c & 0x0f];
        }
    }
    out[j] = '\0';
    return 0;
}

int storage_build_dir(const config_t *cfg, const char *group_name, time_t event_time,
                      char *out, size_t out_sz) {
    char group_safe[MAX_APP_NAME * 3];
    char group_path[MAX_PATH_LEN];
    char date_dir[16];
    struct tm tm_event;
    int n;

    if (!cfg || !out || out_sz == 0 ||
        encode_group_name(group_name, group_safe, sizeof(group_safe)) < 0) {
        log_msg(LOG_ERROR, "invalid or overlong archive group name");
        return -1;
    }
    if (!localtime_r(&event_time, &tm_event) ||
        strftime(date_dir, sizeof(date_dir), "%Y-%m-%d", &tm_event) == 0) {
        log_msg(LOG_ERROR, "failed to format archive date");
        return -1;
    }

    n = snprintf(group_path, sizeof(group_path), "%s/%s", cfg->archive_root,
                 group_safe);
    if (n < 0 || (size_t)n >= sizeof(group_path))
        goto path_too_long;
    n = snprintf(out, out_sz, "%s/%s", group_path, date_dir);
    if (n < 0 || (size_t)n >= out_sz)
        goto path_too_long;

    // archive_root is trusted configuration and may itself traverse a deliberate
    // symlink. Generated group/date components must be real directories.
    if (mkdir_p(cfg->archive_root) < 0 || mkdir_generated_dir(group_path) < 0 ||
        mkdir_generated_dir(out) < 0)
        return -1;
    return 0;

path_too_long:
    log_msg(LOG_ERROR, "archive path is too long for group '%s'", group_name);
    if (out_sz > 0)
        out[0] = '\0';
    return -1;
}

static int write_json_string(FILE *f, const char *value) {
    const unsigned char *p = (const unsigned char *)(value ? value : "");

    if (fputc('"', f) == EOF)
        return -1;
    for (; *p; p++) {
        switch (*p) {
        case '"':
            if (fputs("\\\"", f) == EOF)
                return -1;
            break;
        case '\\':
            if (fputs("\\\\", f) == EOF)
                return -1;
            break;
        case '\b':
            if (fputs("\\b", f) == EOF)
                return -1;
            break;
        case '\f':
            if (fputs("\\f", f) == EOF)
                return -1;
            break;
        case '\n':
            if (fputs("\\n", f) == EOF)
                return -1;
            break;
        case '\r':
            if (fputs("\\r", f) == EOF)
                return -1;
            break;
        case '\t':
            if (fputs("\\t", f) == EOF)
                return -1;
            break;
        default:
            if (*p < 0x20) {
                if (fprintf(f, "\\u%04x", *p) < 0)
                    return -1;
            } else if (fputc(*p, f) == EOF) {
                return -1;
            }
        }
    }
    return fputc('"', f) == EOF ? -1 : 0;
}

int storage_write_entry(const char *dir, const char *group_name,
                        const char *source_app, uint32_t replaces_id,
                        time_t event_time, const parsed_message_t *msg,
                        const char *screenshot_path) {
    char log_path[MAX_PATH_LEN];
    char iso_ts[32];
    struct tm tm_event;
    FILE *f = NULL;
    int fd = -1;
    int n;
    int failed = 0;
    int saved_errno = 0;
    off_t original_size = -1;

    n = snprintf(log_path, sizeof(log_path), "%s/log.jsonl", dir);
    if (n < 0 || (size_t)n >= sizeof(log_path)) {
        log_msg(LOG_ERROR, "log path is too long");
        return -1;
    }
    if (!localtime_r(&event_time, &tm_event) ||
        strftime(iso_ts, sizeof(iso_ts), "%Y-%m-%dT%H:%M:%S%z", &tm_event) == 0) {
        log_msg(LOG_ERROR, "failed to format archive timestamp");
        return -1;
    }

    fd = open(log_path,
              O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        log_msg(LOG_ERROR, "cannot open %s for append: %s", log_path,
                strerror(errno));
        return -1;
    }
    if (fchmod(fd, 0600) < 0 || flock(fd, LOCK_EX) < 0 ||
        (original_size = lseek(fd, 0, SEEK_END)) < 0) {
        saved_errno = errno;
        close(fd);
        log_msg(LOG_ERROR, "cannot secure/lock %s: %s", log_path,
                strerror(saved_errno));
        return -1;
    }
    f = fdopen(fd, "a");
    if (!f) {
        saved_errno = errno;
        flock(fd, LOCK_UN);
        close(fd);
        log_msg(LOG_ERROR, "cannot create stream for %s: %s", log_path,
                strerror(saved_errno));
        return -1;
    }
    if (setvbuf(f, NULL, _IONBF, 0) != 0) {
        flock(fd, LOCK_UN);
        fclose(f);
        log_msg(LOG_ERROR, "cannot configure unbuffered stream for %s", log_path);
        return -1;
    }

    if (fputs("{\"timestamp\":", f) == EOF || write_json_string(f, iso_ts) < 0 ||
        fputs(",\"app\":", f) == EOF || write_json_string(f, group_name) < 0 ||
        fputs(",\"source_app\":", f) == EOF ||
        write_json_string(f, source_app) < 0 ||
        fprintf(f, ",\"replaces_id\":%u,\"group\":", replaces_id) < 0) {
        failed = 1;
    }

    if (!failed) {
        if (msg->is_group) {
            failed = write_json_string(f, msg->group) < 0;
        } else {
            failed = fputs("null", f) == EOF;
        }
    }
    if (!failed &&
        (fputs(",\"sender\":", f) == EOF ||
         write_json_string(f, msg->sender) < 0 ||
         fputs(",\"content\":", f) == EOF ||
         write_json_string(f, msg->content) < 0 ||
         fputs(",\"screenshot\":", f) == EOF)) {
        failed = 1;
    }
    if (!failed) {
        if (screenshot_path)
            failed = write_json_string(f, screenshot_path) < 0;
        else
            failed = fputs("null", f) == EOF;
    }
    if (!failed && fputs("}\n", f) == EOF)
        failed = 1;
    if (!failed && fflush(f) != 0)
        failed = 1;
    if (!failed && fsync(fd) != 0)
        failed = 1;
    if (failed) {
        saved_errno = errno ? errno : EIO;
        clearerr(f);
        if (ftruncate(fd, original_size) < 0)
            log_msg(LOG_ERROR, "cannot roll back partial record in %s: %s",
                    log_path, strerror(errno));
    }
    if (flock(fd, LOCK_UN) < 0 && !failed) {
        saved_errno = errno;
        failed = 1;
    }
    if (fclose(f) != 0 && !failed) {
        saved_errno = errno;
        failed = 1;
    }

    if (failed) {
        log_msg(LOG_ERROR, "failed while writing %s: %s", log_path,
                strerror(saved_errno ? saved_errno : EIO));
        return -1;
    }
    return 0;
}
