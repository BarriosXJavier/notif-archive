#ifndef STORAGE_H
#define STORAGE_H
#include <stddef.h>
#include "config.h"
#include "parser.h"

void storage_build_dir(const config_t *cfg, const char *group_name, char *out, size_t out_sz);

// group_name is the canonical folder name (e.g. "WhatsApp"); source_app
// is the raw D-Bus app_name that actually sent it (e.g. "zapzap") --
// kept in the JSON for traceability even though multiple source_apps
// can share one group_name/folder.
void storage_write_entry(const char *dir, const char *group_name, const char *source_app,
                          const parsed_message_t *msg, const char *screenshot_path);

#endif
