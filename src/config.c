#define _GNU_SOURCE
#include "config.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

static void expand_home(const char *in, char *out, size_t out_sz) {
    const char *home = getenv("HOME");
    if (in[0] == '~' && home) {
        snprintf(out, out_sz, "%s%s", home, in + 1);
    } else {
        snprintf(out, out_sz, "%s", in);
    }
}

static void set_defaults(config_t *cfg) {
    const char *home = getenv("HOME");
    snprintf(cfg->archive_root, sizeof(cfg->archive_root),
             "%s/notif_archive", home ? home : ".");
    const char *st = getenv("XDG_SESSION_TYPE");
    snprintf(cfg->session_type, sizeof(cfg->session_type), "%s", st ? st : "x11");
    cfg->screenshot_delay_ms = 300;
    cfg->app_count = 0;
}

// Trims leading+trailing whitespace off a string in place, returning
// a pointer to the (possibly shifted) start.
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

int config_load(const char *path, config_t *out) {
    set_defaults(out);

    FILE *f = fopen(path, "r");
    if (!f) {
        return 0;
    }

    char line[1024];
    int in_apps_section = 0;

    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') continue;

        if (strcmp(p, "[apps]") == 0) {
            in_apps_section = 1;
            continue;
        }

        if (in_apps_section) {
            if (out->app_count >= MAX_APPS) {
                log_msg(LOG_WARN, "config: dropping app '%s', MAX_APPS (%d) reached",
                        p, MAX_APPS);
                continue;
            }

            // Supports two forms:
            //   WhatsApp                    (app_name == group_name)
            //   whatsapp-for-linux = WhatsApp  (app_name mapped to group_name)
            char *eq = strchr(p, '=');
            app_mapping_t *entry = &out->apps[out->app_count];

            if (eq) {
                *eq = '\0';
                char *app = trim(p);
                char *group = trim(eq + 1);
                snprintf(entry->app_name, sizeof(entry->app_name), "%.*s",
                         MAX_APP_NAME - 1, app);
                snprintf(entry->group_name, sizeof(entry->group_name), "%.*s",
                         MAX_APP_NAME - 1, group);
            } else {
                char *name = trim(p);
                snprintf(entry->app_name, sizeof(entry->app_name), "%.*s",
                         MAX_APP_NAME - 1, name);
                snprintf(entry->group_name, sizeof(entry->group_name), "%.*s",
                         MAX_APP_NAME - 1, name);
            }

            out->app_count++;
            continue;
        }

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(p);
        char *val = trim(eq + 1);

        if (strcmp(key, "archive_root") == 0) {
            expand_home(val, out->archive_root, sizeof(out->archive_root));
        } else if (strcmp(key, "session_type_override") == 0 && strlen(val) > 0) {
            snprintf(out->session_type, sizeof(out->session_type), "%s", val);
        } else if (strcmp(key, "screenshot_delay_ms") == 0) {
            out->screenshot_delay_ms = atoi(val);
        }
    }

    fclose(f);
    return 0;
}

int config_resolve_group(const config_t *cfg, const char *app_name,
                          char *out_group, size_t out_group_sz) {
    for (int i = 0; i < cfg->app_count; i++) {
        if (strcmp(cfg->apps[i].app_name, app_name) == 0) {
            snprintf(out_group, out_group_sz, "%s", cfg->apps[i].group_name);
            return 1;
        }
    }
    return 0;
}
