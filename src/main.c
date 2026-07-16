#include "bus_listener.h"
#include "config.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void log_usage(const char *program) {
    log_msg(LOG_ERROR, "usage: %s [config-path] | --list-apps", program);
}

int main(int argc, char **argv) {
    char config_path[MAX_PATH_LEN];
    const char *home = getenv("HOME");
    int n;

    if (argc == 2 && strcmp(argv[1], "--list-apps") == 0) {
        log_msg(LOG_INFO,
                "starting notification source discovery (no screenshots or archives)");
        return bus_listener_run(NULL, BUS_LISTENER_DISCOVER);
    }
    if (argc > 2) {
        log_usage(argv[0]);
        return 1;
    }
    if (argc == 2) {
        n = snprintf(config_path, sizeof(config_path), "%s", argv[1]);
    } else {
        n = snprintf(config_path, sizeof(config_path),
                     "%s/.config/notif-archiver/notif-archiver.conf",
                     home ? home : ".");
    }
    if (n < 0 || (size_t)n >= sizeof(config_path)) {
        log_msg(LOG_ERROR, "fatal: config path is too long");
        return 1;
    }

    config_t cfg;
    if (config_load(config_path, &cfg) != 0) {
        log_msg(LOG_ERROR, "fatal: could not load config %s", config_path);
        return 1;
    }
    if (cfg.app_count == 0 && !cfg.has_catch_all) {
        log_msg(LOG_ERROR,
                "fatal: no target apps or catch-all configured (check [apps] in %s)",
                config_path);
        return 1;
    }

    log_msg(LOG_INFO,
            "archive_root=%s session_type=%s apps=%d catch_all=%s "
            "screenshot_timeout_ms=%d",
            cfg.archive_root, cfg.session_type, cfg.app_count,
            cfg.has_catch_all ? cfg.catch_all_group : "disabled",
            cfg.screenshot_timeout_ms);
    return bus_listener_run(&cfg, BUS_LISTENER_ARCHIVE);
}
