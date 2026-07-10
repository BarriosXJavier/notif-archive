#include "config.h"
#include "bus_listener.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    char config_path[MAX_PATH_LEN];
    const char *home = getenv("HOME");

    if (argc > 1) {
        snprintf(config_path, sizeof(config_path), "%s", argv[1]);
    } else {
        snprintf(config_path, sizeof(config_path),
                 "%s/.config/notif-archiver/notif-archiver.conf", home ? home : ".");
    }

    config_t cfg;
    if (config_load(config_path, &cfg) != 0) {
        log_msg(LOG_ERROR, "fatal: could not load config");
        return 1;
    }

    if (cfg.app_count == 0) {
        log_msg(LOG_ERROR,
            "fatal: no target apps configured (check [apps] section in %s)",
            config_path);
        return 1;
    }

    log_msg(LOG_INFO, "archive_root=%s session_type=%s apps=%d",
            cfg.archive_root, cfg.session_type, cfg.app_count);

    return bus_listener_run(&cfg);
}
