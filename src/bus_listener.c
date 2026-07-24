#define _POSIX_C_SOURCE 200809L
#include "bus_listener.h"
#include "log.h"
#include "parser.h"
#include "screenshot.h"
#include "storage.h"

#include "storage_pg.h"
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <time.h>
#include <unistd.h>

#define MAX_DISCOVERED_PAIRS 1024
#define MAX_DISCOVERY_ID_LEN (MAX_APP_NAME - 1)
#define ESCAPED_DISCOVERY_ID_LEN (MAX_APP_NAME * 6 + 1)

typedef struct discovered_pair {
  char *app_name;
  char *desktop_entry;
  struct discovered_pair *next;
} discovered_pair_t;

static const config_t *g_cfg;
static PGconn *g_pg;
static bus_listener_mode_t g_mode;
static volatile sig_atomic_t g_stop_requested;
static unsigned long g_screenshot_sequence;
static discovered_pair_t *g_discovered_pairs;
static size_t g_discovered_count;

static void handle_stop_signal(int signal_number) {
  (void)signal_number;
  g_stop_requested = 1;
}

static int install_signal_handlers(void) {
  struct sigaction action;

  memset(&action, 0, sizeof(action));
  action.sa_handler = handle_stop_signal;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGINT, &action, NULL) < 0 ||
      sigaction(SIGTERM, &action, NULL) < 0) {
    log_msg(LOG_ERROR, "failed to install signal handlers: %s",
            strerror(errno));
    return -1;
  }
  return 0;
}

// The cursor is rewound to the message root before every return. This makes
// malformed/missing actions or hints harmless to any later reader and also
// clears any open dict-entry/array state after a partial failure.
static int read_desktop_entry_hint(sd_bus_message *m, char *out_entry,
                                   size_t out_sz) {
  int r;
  int found = 0;

  if (!m || !out_entry || out_sz == 0)
    return 0;
  out_entry[0] = '\0';

  r = sd_bus_message_skip(m, "as");
  if (r <= 0)
    goto done;
  r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
  if (r <= 0)
    goto done;

  for (;;) {
    const char *key = NULL;

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
    if (r == 0)
      break;
    if (r < 0)
      goto done;

    r = sd_bus_message_read(m, "s", &key);
    if (r <= 0)
      goto done;

    if (!found && key && strcmp(key, "desktop-entry") == 0) {
      char type = 0;
      const char *contents = NULL;

      r = sd_bus_message_peek_type(m, &type, &contents);
      if (r <= 0)
        goto done;
      if (type == SD_BUS_TYPE_VARIANT && contents &&
          strcmp(contents, "s") == 0) {
        const char *value = NULL;
        int n;

        r = sd_bus_message_read(m, "v", "s", &value);
        if (r <= 0)
          goto done;
        if (value && value[0] != '\0') {
          n = snprintf(out_entry, out_sz, "%s", value);
          if (n >= 0 && (size_t)n < out_sz)
            found = 1;
          else {
            out_entry[0] = '\0';
            log_msg(LOG_WARN, "desktop-entry hint exceeds %zu-byte limit",
                    out_sz - 1);
          }
        }
      } else {
        r = sd_bus_message_skip(m, "v");
        if (r <= 0)
          goto done;
      }
    } else {
      r = sd_bus_message_skip(m, "v");
      if (r <= 0)
        goto done;
    }

    r = sd_bus_message_exit_container(m);
    if (r < 0)
      goto done;
  }

  r = sd_bus_message_exit_container(m);
  if (r < 0)
    found = 0;

done:
  if (sd_bus_message_rewind(m, true) < 0) {
    out_entry[0] = '\0';
    log_msg(LOG_DEBUG, "failed to rewind malformed Notify message");
    return 0;
  }
  if (!found)
    out_entry[0] = '\0';
  return found;
}

static void free_discovered_pairs(void) {
  discovered_pair_t *pair = g_discovered_pairs;

  while (pair) {
    discovered_pair_t *next = pair->next;
    free(pair->app_name);
    free(pair->desktop_entry);
    free(pair);
    pair = next;
  }
  g_discovered_pairs = NULL;
  g_discovered_count = 0;
}

static void escape_discovery_identity(const char *input, char *output,
                                      size_t output_size) {
  static const char hex[] = "0123456789ABCDEF";
  size_t j = 0;

  for (size_t i = 0; input[i] != '\0' && j + 1 < output_size; i++) {
    unsigned char c = (unsigned char)input[i];

    if (c == '\\' || c == '\'') {
      if (j + 2 >= output_size)
        break;
      output[j++] = '\\';
      output[j++] = (char)c;
    } else if (c == '\n' || c == '\r' || c == '\t') {
      if (j + 2 >= output_size)
        break;
      output[j++] = '\\';
      output[j++] = c == '\n' ? 'n' : (c == '\r' ? 'r' : 't');
    } else if (c < 0x20 || c == 0x7f) {
      if (j + 6 >= output_size)
        break;
      output[j++] = '\\';
      output[j++] = 'u';
      output[j++] = '0';
      output[j++] = '0';
      output[j++] = hex[c >> 4];
      output[j++] = hex[c & 0x0f];
    } else {
      output[j++] = (char)c;
    }
  }
  output[j] = '\0';
}

static void remember_discovered_pair(const char *app_name,
                                     const char *desktop_entry) {
  discovered_pair_t *pair;

  if (strlen(app_name) > MAX_DISCOVERY_ID_LEN ||
      strlen(desktop_entry) > MAX_DISCOVERY_ID_LEN) {
    log_msg(LOG_WARN,
            "DISCOVER identity exceeds %d bytes and cannot be tracked",
            MAX_DISCOVERY_ID_LEN);
    return;
  }
  for (pair = g_discovered_pairs; pair; pair = pair->next) {
    if (strcmp(pair->app_name, app_name) == 0 &&
        strcmp(pair->desktop_entry, desktop_entry) == 0)
      return;
  }
  if (g_discovered_count >= MAX_DISCOVERED_PAIRS) {
    log_msg(LOG_WARN,
            "DISCOVER pair limit (%d) reached; ignoring new identities",
            MAX_DISCOVERED_PAIRS);
    return;
  }

  pair = calloc(1, sizeof(*pair));
  if (!pair)
    goto allocation_failed;
  pair->app_name = strdup(app_name);
  pair->desktop_entry = strdup(desktop_entry);
  if (!pair->app_name || !pair->desktop_entry) {
    free(pair->app_name);
    free(pair->desktop_entry);
    free(pair);
    goto allocation_failed;
  }
  pair->next = g_discovered_pairs;
  g_discovered_pairs = pair;
  g_discovered_count++;
  {
    char escaped_app[ESCAPED_DISCOVERY_ID_LEN];
    char escaped_desktop[ESCAPED_DISCOVERY_ID_LEN];
    escape_discovery_identity(app_name, escaped_app, sizeof(escaped_app));
    escape_discovery_identity(desktop_entry, escaped_desktop,
                              sizeof(escaped_desktop));
    log_msg(LOG_INFO, "DISCOVER app_name='%s' desktop-entry='%s'", escaped_app,
            escaped_desktop);
  }
  return;

allocation_failed:
  log_msg(LOG_ERROR, "DISCOVER cannot allocate identity tracking entry");
}

static int build_screenshot_path(const char *dir,
                                 const struct timespec *event_time, char *out,
                                 size_t out_sz) {
  int n = snprintf(out, out_sz, "%s/%lld-%09ld-%lu.png", dir,
                   (long long)event_time->tv_sec, event_time->tv_nsec,
                   g_screenshot_sequence++);
  return n >= 0 && (size_t)n < out_sz ? 0 : -1;
}

static int on_message(sd_bus_message *m, void *userdata,
                      sd_bus_error *ret_error) {
  const char *app_name = NULL;
  const char *app_icon = NULL;
  const char *summary = NULL;
  const char *body = NULL;
  const char *resolved_source;
  uint32_t replaces_id = 0;
  char desktop_entry[MAX_DISCOVERY_ID_LEN + 1];
  char group_name[MAX_APP_NAME];
  int matched;
  int has_desktop_entry;
  int used_catch_all = 0;
  int r;

  (void)userdata;
  (void)ret_error;

  if (!sd_bus_message_is_method_call(m, "org.freedesktop.Notifications",
                                     "Notify"))
    return 1;
  r = sd_bus_message_read(m, "susss", &app_name, &replaces_id, &app_icon,
                          &summary, &body);
  if (r <= 0 || !app_name || !summary || !body) {
    log_msg(LOG_DEBUG, "skipped unparseable Notify call (r=%d)", r);
    return 1;
  }

  desktop_entry[0] = '\0';
  has_desktop_entry =
      read_desktop_entry_hint(m, desktop_entry, sizeof(desktop_entry));
  if (!has_desktop_entry)
    desktop_entry[0] = '\0';
  if (g_mode == BUS_LISTENER_DISCOVER) {
    remember_discovered_pair(app_name, desktop_entry);
    return 1;
  }

  matched =
      config_resolve_group(g_cfg, app_name, group_name, sizeof(group_name));
  if (matched < 0)
    goto group_too_long;
  resolved_source = app_name;

  if (!matched && has_desktop_entry) {
    log_msg(LOG_DEBUG, "unmatched app_name='%s', desktop-entry hint='%s'",
            app_name, desktop_entry);
    matched = config_resolve_group(g_cfg, desktop_entry, group_name,
                                   sizeof(group_name));
    if (matched < 0)
      goto group_too_long;
    if (matched)
      resolved_source = desktop_entry;
  }

  if (!matched) {
    matched = config_resolve_catch_all(g_cfg, group_name, sizeof(group_name));
    if (matched < 0)
      goto group_too_long;
    if (matched) {
      used_catch_all = 1;
      if (app_name[0] == '\0' && has_desktop_entry)
        resolved_source = desktop_entry;
      else if (app_name[0] == '\0')
        resolved_source = "<unknown>";
    }
  }
  if (!matched)
    return 1;

  if (used_catch_all) {
    log_msg(LOG_WARN,
            "catch-all matched app_name='%s' desktop-entry='%s' group='%s'; "
            "add an explicit mapping",
            app_name, desktop_entry, group_name);
  }

  {
    struct timespec event_time;
    char dir[MAX_PATH_LEN];
    char screenshot_path[MAX_PATH_LEN];
    parsed_message_t parsed;
    int got_screenshot = 0;

    if (clock_gettime(CLOCK_REALTIME, &event_time) < 0) {
      log_msg(LOG_ERROR, "cannot timestamp notification: %s", strerror(errno));
      return 1;
    }
    log_msg(LOG_INFO,
            "captured notification from %s (group=%s, replaces_id=%u)",
            resolved_source, group_name, replaces_id);

    if (storage_build_dir(g_cfg, group_name, event_time.tv_sec, dir,
                          sizeof(dir)) < 0)
      return 1;
    if (build_screenshot_path(dir, &event_time, screenshot_path,
                              sizeof(screenshot_path)) < 0) {
      log_msg(LOG_ERROR, "screenshot path is too long");
      return 1;
    }

    got_screenshot =
        screenshot_capture(g_cfg, screenshot_path, &g_stop_requested);
    parser_split(summary, body, &parsed);
    storage_write_entry(dir, group_name, resolved_source, replaces_id,
                        event_time.tv_sec, &parsed,
                        got_screenshot ? screenshot_path : NULL);

    storage_pg_insert(g_pg, group_name, resolved_source, &parsed,
                      got_screenshot ? screenshot_path : NULL);
  }

  return 1;

group_too_long:
  log_msg(LOG_ERROR, "configured group name does not fit output buffer");
  return 1;
}

static int resolve_session_address(char *out, size_t out_sz) {
  const char *environment = getenv("DBUS_SESSION_BUS_ADDRESS");
  const char *runtime_dir;
  int n;

  if (environment && environment[0] != '\0') {
    n = snprintf(out, out_sz, "%s", environment);
    return n >= 0 && (size_t)n < out_sz ? 0 : -1;
  }

  runtime_dir = getenv("XDG_RUNTIME_DIR");
  if (!runtime_dir || runtime_dir[0] == '\0')
    return -1;
  n = snprintf(out, out_sz, "unix:path=%s/bus", runtime_dir);
  return n >= 0 && (size_t)n < out_sz ? 0 : -1;
}

static int connect_via_eavesdrop(const char *address, sd_bus **out_bus) {
  sd_bus *bus = NULL;
  int r;

  r = sd_bus_new(&bus);
  if (r < 0)
    return r;
  r = sd_bus_set_bus_client(bus, true);
  if (r < 0)
    goto fail;
  r = sd_bus_set_address(bus, address);
  if (r < 0)
    goto fail;
  r = sd_bus_start(bus);
  if (r < 0)
    goto fail;
  r = sd_bus_add_match(bus, NULL,
                       "eavesdrop='true',interface='org.freedesktop."
                       "Notifications',member='Notify'",
                       on_message, NULL);
  if (r < 0)
    goto fail;

  *out_bus = bus;
  return 0;

fail:
  sd_bus_unref(bus);
  return r;
}
static int connect_via_monitor(const char *address, sd_bus **out_bus) {
  sd_bus *bus = NULL;
  sd_bus_error error = SD_BUS_ERROR_NULL;
  sd_bus_message *call = NULL;
  sd_bus_message *reply = NULL;
  const char *rule =
      "interface='org.freedesktop.Notifications',member='Notify'";
  int r;
  r = sd_bus_new(&bus);
  if (r < 0)
    return r;
  r = sd_bus_set_bus_client(bus, true);
  if (r < 0)
    goto fail;
  r = sd_bus_negotiate_creds(bus, true, SD_BUS_CREDS_WELL_KNOWN_NAMES);
  if (r < 0)
    goto fail;
  r = sd_bus_negotiate_timestamp(bus, true);
  if (r < 0)
    goto fail;
  r = sd_bus_negotiate_fds(bus, true);
  if (r < 0)
    goto fail;
  r = sd_bus_set_monitor(bus, true);
  if (r < 0)
    goto fail;
  r = sd_bus_set_address(bus, address);
  if (r < 0)
    goto fail;
  r = sd_bus_start(bus);
  if (r < 0)
    goto fail;
  r = sd_bus_add_filter(bus, NULL, on_message, NULL);
  if (r < 0)
    goto fail;
  r = sd_bus_message_new_method_call(
      bus, &call, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus.Monitoring", "BecomeMonitor");
  if (r < 0)
    goto fail;
  r = sd_bus_message_open_container(call, 'a', "s");
  if (r < 0)
    goto fail_msg;
  r = sd_bus_message_append(call, "s", rule);
  if (r < 0)
    goto fail_msg;
  r = sd_bus_message_close_container(call);
  if (r < 0)
    goto fail_msg;
  r = sd_bus_message_append(call, "u", (uint32_t)0);
  if (r < 0)
    goto fail_msg;
  r = sd_bus_call(bus, call, 0, &error, &reply);
  log_msg(LOG_DEBUG,
          "sd_bus_call BecomeMonitor: %d (error.name=%s error.message=%s)", r,
          error.name ? error.name : "none",
          error.message ? error.message : "none");
  if (r < 0)
    goto fail_msg;
  sd_bus_message_unref(call);
  sd_bus_message_unref(reply);
  sd_bus_error_free(&error);
  *out_bus = bus;
  return 0;
fail_msg:
  sd_bus_message_unref(call);
  if (reply)
    sd_bus_message_unref(reply);
fail:
  sd_bus_error_free(&error);
  sd_bus_unref(bus);
  return r;
}
static int connect_bus(const char *address, sd_bus **out_bus) {
  int r = connect_via_eavesdrop(address, out_bus);

  if (r >= 0) {
    log_msg(LOG_INFO, "connected via eavesdrop match rule");
    return 0;
  }
  log_msg(LOG_WARN,
          "eavesdrop match rejected (%s), falling back to BecomeMonitor",
          strerror(-r));

  r = connect_via_monitor(address, out_bus);
  if (r >= 0) {
    log_msg(LOG_INFO, "connected via BecomeMonitor");
    return 0;
  }
  log_msg(LOG_ERROR, "BecomeMonitor also failed: %s", strerror(-r));
  return r;
}

static void reconnect_delay(unsigned int seconds) {
  struct timespec delay = {.tv_sec = (time_t)seconds, .tv_nsec = 0};

  while (!g_stop_requested && nanosleep(&delay, &delay) < 0 && errno == EINTR)
    ;
}

int bus_listener_run(const config_t *cfg, bus_listener_mode_t mode) {
  char address[MAX_PATH_LEN];
  int consecutive_failures = 0;
  int result = 0;

  if (mode == BUS_LISTENER_ARCHIVE && !cfg)
    return 1;
  g_cfg = cfg;
  g_mode = mode;
  g_stop_requested = 0;
  free_discovered_pairs();
  if (install_signal_handlers() < 0)
    return 1;
  if (resolve_session_address(address, sizeof(address)) < 0) {
    log_msg(LOG_ERROR,
            "could not determine session bus address or address is too long");
    return 1;
  }

  while (!g_stop_requested) {
    sd_bus *bus = NULL;
    int r = connect_bus(address, &bus);

    if (r < 0) {
      consecutive_failures++;
      if (consecutive_failures >= 5) {
        log_msg(LOG_ERROR,
                "giving up after %d consecutive failed connection attempts",
                consecutive_failures);
        result = 1;
        break;
      }
      log_msg(LOG_WARN, "retrying bus connection in 3s (attempt %d/5)",
              consecutive_failures);
      reconnect_delay(3);
      continue;
    }
    consecutive_failures = 0;
    if (mode == BUS_LISTENER_DISCOVER) {
      log_msg(LOG_INFO, "discovery mode active; trigger notifications and "
                        "press Ctrl-C to stop");
    } else {
      log_msg(LOG_INFO, "listening for %d configured app(s)%s", cfg->app_count,
              cfg->has_catch_all ? " plus catch-all" : "");
    }

    while (!g_stop_requested) {
      r = sd_bus_process(bus, NULL);
      if (r < 0) {
        if (r == -EINTR && g_stop_requested)
          break;
        log_msg(LOG_WARN, "bus connection lost (%s), reconnecting",
                strerror(-r));
        break;
      }
      if (r > 0)
        continue;
      // A finite wait closes the signal lost-wakeup window: even if
      // SIGTERM lands just before this call, shutdown is noticed quickly.
      r = sd_bus_wait(bus, 250000);
      if (r < 0) {
        if (r == -EINTR && g_stop_requested)
          break;
        log_msg(LOG_WARN, "bus wait failed (%s), reconnecting", strerror(-r));
        break;
      }
    }

    sd_bus_unref(bus);
    if (!g_stop_requested)
      reconnect_delay(1);
  }

  if (g_stop_requested)
    log_msg(LOG_INFO, "shutdown requested; listener stopped cleanly");
  free_discovered_pairs();
  return result;
}
