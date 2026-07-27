#define _GNU_SOURCE
#include "config.h"
#include "log.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_LINE_LEN 1024
#define MAX_SCREENSHOT_DELAY_MS 600000
#define MIN_SCREENSHOT_TIMEOUT_MS 100
#define MAX_SCREENSHOT_TIMEOUT_MS 600000

static int copy_string(char *out, size_t out_sz, const char *in) {
  int n;

  if (!out || out_sz == 0 || !in)
    return -1;
  n = snprintf(out, out_sz, "%s", in);
  return n >= 0 && (size_t)n < out_sz ? 0 : -1;
}

static int expand_home(const char *in, char *out, size_t out_sz) {
  const char *home = getenv("HOME");
  int n;

  if (in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
    if (!home)
      home = ".";
    n = snprintf(out, out_sz, "%s%s", home, in + 1);
  } else {
    n = snprintf(out, out_sz, "%s", in);
  }
  return n >= 0 && (size_t)n < out_sz ? 0 : -1;
}

static int valid_utf8(const char *text) {
  const unsigned char *p = (const unsigned char *)text;

  while (*p) {
    if (*p < 0x80) {
      p++;
    } else if (*p >= 0xc2 && *p <= 0xdf && p[1] >= 0x80 && p[1] <= 0xbf) {
      p += 2;
    } else if (*p == 0xe0 && p[1] >= 0xa0 && p[1] <= 0xbf && p[2] >= 0x80 &&
               p[2] <= 0xbf) {
      p += 3;
    } else if (((*p >= 0xe1 && *p <= 0xec) || (*p >= 0xee && *p <= 0xef)) &&
               p[1] >= 0x80 && p[1] <= 0xbf && p[2] >= 0x80 && p[2] <= 0xbf) {
      p += 3;
    } else if (*p == 0xed && p[1] >= 0x80 && p[1] <= 0x9f && p[2] >= 0x80 &&
               p[2] <= 0xbf) {
      p += 3;
    } else if (*p == 0xf0 && p[1] >= 0x90 && p[1] <= 0xbf && p[2] >= 0x80 &&
               p[2] <= 0xbf && p[3] >= 0x80 && p[3] <= 0xbf) {
      p += 4;
    } else if (*p >= 0xf1 && *p <= 0xf3 && p[1] >= 0x80 && p[1] <= 0xbf &&
               p[2] >= 0x80 && p[2] <= 0xbf && p[3] >= 0x80 && p[3] <= 0xbf) {
      p += 4;
    } else if (*p == 0xf4 && p[1] >= 0x80 && p[1] <= 0x8f && p[2] >= 0x80 &&
               p[2] <= 0xbf && p[3] >= 0x80 && p[3] <= 0xbf) {
      p += 4;
    } else {
      return 0;
    }
  }
  return 1;
}

static int set_defaults(config_t *cfg) {
  const char *home = getenv("HOME");
  const char *session_type = getenv("XDG_SESSION_TYPE");
  int n;

  memset(cfg, 0, sizeof(*cfg));
  n = snprintf(cfg->archive_root, sizeof(cfg->archive_root), "%s/notif_archive",
               home ? home : ".");
  if (n < 0 || (size_t)n >= sizeof(cfg->archive_root) ||
      !valid_utf8(cfg->archive_root))
    return -1;
  if (!session_type || (strcmp(session_type, "x11") != 0 &&
                        strcmp(session_type, "wayland") != 0))
    session_type = "x11";
  if (copy_string(cfg->session_type, sizeof(cfg->session_type), session_type) <
      0)
    return -1;
  cfg->screenshot_delay_ms = 300;
  cfg->screenshot_timeout_ms = 10000;

  cfg->pg_enabled = 0;
  if (copy_string(cfg->pg_port, sizeof(cfg->pg_port), "5432") < 0)
    return -1;
  return 0;
}

static char *trim(char *s) {
  char *end;

  while (isspace((unsigned char)*s))
    s++;
  if (*s == '\0')
    return s;
  end = s + strlen(s) - 1;
  while (end >= s && isspace((unsigned char)*end))
    *end-- = '\0';
  return s;
}

static int parse_bounded_int(const char *text, int min, int max, int *out) {
  char *end = NULL;
  long value;

  errno = 0;
  value = strtol(text, &end, 10);
  if (errno != 0 || end == text || *trim(end) != '\0' || value < min ||
      value > max || value > INT_MAX)
    return -1;
  *out = (int)value;
  return 0;
}

static int mapping_exists(const config_t *cfg, const char *app_name) {
  for (int i = 0; i < cfg->app_count; i++) {
    if (strcmp(cfg->apps[i].app_name, app_name) == 0)
      return 1;
  }
  return 0;
}

int config_load(const char *path, config_t *out) {
  FILE *f;
  char line[CONFIG_LINE_LEN];
  int in_apps_section = 0;
  int line_number = 0;
  int had_error = 0;

  if (!path || !out)
    return -1;
  if (set_defaults(out) < 0) {
    log_msg(LOG_ERROR,
            "config: HOME default path is too long or invalid UTF-8");
    return -1;
  }

  f = fopen(path, "r");
  if (!f) {
    log_msg(LOG_ERROR, "config: cannot open %s: %s", path, strerror(errno));
    return -1;
  }

  while (fgets(line, sizeof(line), f)) {
    char *p;
    char *newline;
    char *eq;

    line_number++;
    newline = strchr(line, '\n');
    if (newline) {
      *newline = '\0';
    } else if (!feof(f)) {
      int ch;
      while ((ch = fgetc(f)) != '\n' && ch != EOF)
        ;
      log_msg(LOG_ERROR, "config:%d: line exceeds %d bytes", line_number,
              CONFIG_LINE_LEN - 1);
      had_error = 1;
      continue;
    }

    p = trim(line); // Also removes CR from CRLF files.
    if (*p == '\0' || *p == '#')
      continue;

    if (*p == '[') {
      if (strcmp(p, "[apps]") == 0) {
        in_apps_section = 1;
      } else {
        log_msg(LOG_ERROR, "config:%d: unknown section '%s'", line_number, p);
        in_apps_section = 0;
        had_error = 1;
      }
      continue;
    }

    if (in_apps_section) {
      char *app;
      char *group;
      app_mapping_t *entry;

      eq = strchr(p, '=');
      if (eq) {
        *eq = '\0';
        app = trim(p);
        group = trim(eq + 1);
      } else {
        app = trim(p);
        group = app;
      }

      if (*app == '\0' || *group == '\0') {
        log_msg(LOG_ERROR, "config:%d: app and group names must not be empty",
                line_number);
        had_error = 1;
        continue;
      }
      if (!valid_utf8(app) || !valid_utf8(group)) {
        log_msg(LOG_ERROR, "config:%d: app/group name is not valid UTF-8",
                line_number);
        had_error = 1;
        continue;
      }
      if (strlen(app) >= MAX_APP_NAME || strlen(group) >= MAX_APP_NAME) {
        log_msg(LOG_ERROR,
                "config:%d: app/group name is too long (maximum %d bytes)",
                line_number, MAX_APP_NAME - 1);
        had_error = 1;
        continue;
      }
      if (strcmp(app, "*") == 0) {
        if (out->has_catch_all) {
          log_msg(LOG_ERROR,
                  "config:%d: only one '*' catch-all mapping is allowed",
                  line_number);
          had_error = 1;
          continue;
        }
        copy_string(out->catch_all_group, sizeof(out->catch_all_group), group);
        out->has_catch_all = 1;
        continue;
      }
      if (mapping_exists(out, app)) {
        log_msg(LOG_WARN,
                "config:%d: duplicate app '%s' ignored; first mapping wins",
                line_number, app);
        continue;
      }
      if (out->app_count >= MAX_APPS) {
        log_msg(LOG_ERROR, "config:%d: MAX_APPS (%d) exceeded", line_number,
                MAX_APPS);
        had_error = 1;
        continue;
      }

      entry = &out->apps[out->app_count];
      copy_string(entry->app_name, sizeof(entry->app_name), app);
      copy_string(entry->group_name, sizeof(entry->group_name), group);
      out->app_count++;
      continue;
    }

    eq = strchr(p, '=');
    if (!eq) {
      log_msg(LOG_ERROR, "config:%d: expected key=value", line_number);
      had_error = 1;
      continue;
    }
    *eq = '\0';
    {
      char *key = trim(p);
      char *value = trim(eq + 1);

      if (strcmp(key, "archive_root") == 0) {
        if (*value == '\0' || !valid_utf8(value) ||
            expand_home(value, out->archive_root, sizeof(out->archive_root)) <
                0) {
          log_msg(
              LOG_ERROR,
              "config:%d: archive_root is empty, too long, or invalid UTF-8",
              line_number);
          had_error = 1;
        }
      } else if (strcmp(key, "session_type_override") == 0) {
        if (strcmp(value, "x11") != 0 && strcmp(value, "wayland") != 0) {
          log_msg(LOG_ERROR,
                  "config:%d: session_type_override must be x11 or wayland",
                  line_number);
          had_error = 1;
        } else if (copy_string(out->session_type, sizeof(out->session_type),
                               value) < 0) {
          had_error = 1;
        }
      } else if (strcmp(key, "screenshot_delay_ms") == 0) {
        if (parse_bounded_int(value, 0, MAX_SCREENSHOT_DELAY_MS,
                              &out->screenshot_delay_ms) < 0) {
          log_msg(LOG_ERROR, "config:%d: screenshot_delay_ms must be 0..%d",
                  line_number, MAX_SCREENSHOT_DELAY_MS);
          had_error = 1;
        }
      } else if (strcmp(key, "screenshot_timeout_ms") == 0) {
        if (parse_bounded_int(value, MIN_SCREENSHOT_TIMEOUT_MS,
                              MAX_SCREENSHOT_TIMEOUT_MS,
                              &out->screenshot_timeout_ms) < 0) {
          log_msg(LOG_ERROR, "config:%d: screenshot_timeout_ms must be %d..%d",
                  line_number, MIN_SCREENSHOT_TIMEOUT_MS,
                  MAX_SCREENSHOT_TIMEOUT_MS);
          had_error = 1;
        }
      } else {
        log_msg(LOG_ERROR, "config:%d: unknown key '%s'", line_number, key);
        had_error = 1;
      }
    }
  }

  if (ferror(f)) {
    log_msg(LOG_ERROR, "config: read failed for %s: %s", path, strerror(errno));
    had_error = 1;
  }
  if (fclose(f) != 0) {
    log_msg(LOG_ERROR, "config: close failed for %s: %s", path,
            strerror(errno));
    had_error = 1;
  }
  return had_error ? -1 : 0;
}

int config_resolve_group(const config_t *cfg, const char *app_name,
                         char *out_group, size_t out_group_sz) {
  if (!cfg || !app_name || !out_group || out_group_sz == 0)
    return -1;

  for (int i = 0; i < cfg->app_count; i++) {
    if (strcmp(cfg->apps[i].app_name, app_name) == 0) {
      if (copy_string(out_group, out_group_sz, cfg->apps[i].group_name) < 0)
        return -1;
      return 1;
    }
  }
  return 0;
}

int config_resolve_catch_all(const config_t *cfg, char *out_group,
                             size_t out_group_sz) {
  if (!cfg || !out_group || out_group_sz == 0)
    return -1;
  if (!cfg->has_catch_all)
    return 0;
  if (copy_string(out_group, out_group_sz, cfg->catch_all_group) < 0)
    return -1;
  return 1;
}
