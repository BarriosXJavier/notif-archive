#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

#define MAX_APPS 32
#define MAX_APP_NAME 64
#define MAX_PATH_LEN 512

// One watched D-Bus app_name, mapped to a canonical "group" name used
// for the archive folder. Multiple app_name values (e.g. "WhatsApp",
// "whatsapp-for-linux", "zapzap") can all map to the same group
// ("WhatsApp"), so notifications/screenshots from any of them land in
// one shared folder instead of being split across several.
typedef struct {
    char app_name[MAX_APP_NAME];
    char group_name[MAX_APP_NAME];
} app_mapping_t;

typedef struct {
    char archive_root[MAX_PATH_LEN];
    char session_type[16];
    int screenshot_delay_ms;
    app_mapping_t apps[MAX_APPS];
    int app_count;
} config_t;

int config_load(const char *path, config_t *out);

// Looks up app_name in cfg's configured mappings. Returns 1 and fills
// out_group if found, 0 if app_name isn't one we're watching.
int config_resolve_group(const config_t *cfg, const char *app_name,
                          char *out_group, size_t out_group_sz);

#endif
