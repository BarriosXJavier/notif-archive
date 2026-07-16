#ifndef STORAGE_H
#define STORAGE_H

#include "config.h"
#include "parser.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Returns 0 on success and -1 on truncation or directory creation failure.
int storage_build_dir(const config_t *cfg, const char *group_name, time_t event_time,
                      char *out, size_t out_sz);

// Appends one complete JSONL record. source_app is informational only; path
// selection is based exclusively on the canonical group_name.
int storage_write_entry(const char *dir, const char *group_name,
                        const char *source_app, uint32_t replaces_id,
                        time_t event_time, const parsed_message_t *msg,
                        const char *screenshot_path);

#endif
