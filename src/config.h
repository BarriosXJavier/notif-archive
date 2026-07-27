#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

#define MAX_APPS 32
#define MAX_APP_NAME 256
#define MAX_PATH_LEN 4096

typedef struct {
  char app_name[MAX_APP_NAME];
  char group_name[MAX_APP_NAME];
} app_mapping_t;

typedef struct {
  char archive_root[MAX_PATH_LEN];
  char session_type[16];
  int screenshot_delay_ms;
  int screenshot_timeout_ms;
  app_mapping_t apps[MAX_APPS];
  int app_count;
  char catch_all_group[MAX_APP_NAME];
  int has_catch_all;

  // Postgres connection info
  int pg_enabled;
  char pg_host[128];
  char pg_port[8];
  char pg_dbname[128];
  char pg_user[128];
} config_t;

// Returns 0 on success and -1 if the file cannot be read or contains an
// invalid/truncated value. Duplicate app names are ignored after the first.
int config_load(const char *path, config_t *out);

// Resolves the first configured mapping for app_name. Returns 1 on success,
// 0 when no mapping exists, and -1 when the output buffer is too small.
int config_resolve_group(const config_t *cfg, const char *app_name,
                         char *out_group, size_t out_group_sz);

// Returns the optional wildcard group configured as "* = Group".
int config_resolve_catch_all(const config_t *cfg, char *out_group,
                             size_t out_group_sz);

#endif
