#include "bus_listener.h"
#include "log.h"
#include "parser.h"
#include "screenshot.h"
#include "storage.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <time.h>
#include <unistd.h>

static const config_t *g_cfg;

// Attempts to read the "desktop-entry" hint from a Notify() message's
// hints dictionary (the 6th field, a{sv}). Must be called with the
// message cursor positioned right after body -- i.e. right after the
// "susss" read below, before anything else has consumed the actions
// array. Returns 1 and fills out_entry if found, 0 otherwise. Never
// fatal: most apps simply don't set this hint, and that's normal.
static int read_desktop_entry_hint(sd_bus_message *m, char *out_entry, size_t out_sz) {
    int r;

    // Notify()'s signature is susssasa{sv}i -- actions (array of
    // strings) comes before hints, so skip over it first.
    r = sd_bus_message_skip(m, "as");
    if (r < 0) return 0;

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    if (r < 0) return 0;

    int found = 0;
    while ((r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0) {
        const char *key = NULL;
        r = sd_bus_message_read(m, "s", &key);
        if (r < 0) {
            sd_bus_message_exit_container(m);
            break;
        }

        if (!found && key && strcmp(key, "desktop-entry") == 0) {
            const char *val = NULL;
            r = sd_bus_message_read(m, "v", "s", &val);
            if (r >= 0 && val) {
                snprintf(out_entry, out_sz, "%s", val);
                found = 1;
            }
        } else {
            sd_bus_message_skip(m, "v"); // not the hint we want, skip it
        }

        sd_bus_message_exit_container(m); // dict entry
    }
    sd_bus_message_exit_container(m); // array

    return found;
}

static int on_message(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)userdata; (void)ret_error;

    const char *iface = sd_bus_message_get_interface(m);
    const char *member = sd_bus_message_get_member(m);
    if (!iface || !member) return 1;
    if (strcmp(iface, "org.freedesktop.Notifications") != 0) return 1;
    if (strcmp(member, "Notify") != 0) return 1;

    const char *app_name, *app_icon, *summary, *body;
    uint32_t replaces_id;

    int r = sd_bus_message_read(m, "susss", &app_name, &replaces_id,
                                 &app_icon, &summary, &body);
    if (r < 0) {
        log_msg(LOG_DEBUG, "skipped unparseable Notify call (r=%d)", r);
        return 1;
    }

    char group_name[MAX_APP_NAME];
    int matched = config_resolve_group(g_cfg, app_name, group_name, sizeof(group_name));

    char resolved_source[MAX_APP_NAME];
    snprintf(resolved_source, sizeof(resolved_source), "%s", app_name);

    if (!matched) {
        // app_name alone didn't match a configured entry -- sandboxed
        // apps relayed through xdg-desktop-portal (common with
        // Flatpak) often send an empty or generic app_name and
        // identify themselves via a "desktop-entry" hint instead.
        char desktop_entry[MAX_APP_NAME];
        if (read_desktop_entry_hint(m, desktop_entry, sizeof(desktop_entry))) {
            log_msg(LOG_DEBUG, "app_name empty, desktop-entry hint='%s'", desktop_entry);
            if (config_resolve_group(g_cfg, desktop_entry, group_name, sizeof(group_name))) {
                matched = 1;
                snprintf(resolved_source, sizeof(resolved_source), "%s", desktop_entry);
            }
        }
    }

    if (!matched) return 1;

    log_msg(LOG_INFO, "captured notification from %s (group=%s)", resolved_source, group_name);

    char dir[MAX_PATH_LEN];
    storage_build_dir(g_cfg, group_name, dir, sizeof(dir));

    char shot_path[900];
    snprintf(shot_path, sizeof(shot_path), "%s/%ld.png", dir, (long)time(NULL));
    int got_shot = screenshot_capture(g_cfg, shot_path);

    parsed_message_t msg;
    parser_split(summary, body, &msg);

    storage_write_entry(dir, group_name, resolved_source, &msg, got_shot ? shot_path : NULL);

    return 1;
}

static int resolve_session_address(char *out, size_t out_sz) {
  const char *env = getenv("DBUS_SESSION_BUS_ADDRESS");
  if (env && env[0] != '\0') {
    snprintf(out, out_sz, "%s", env);
    return 0;
  }

  const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
  if (!runtime_dir || runtime_dir[0] == '\0') {
    return -1;
  }
  snprintf(out, out_sz, "unix:path=%s/bus", runtime_dir);
  return 0;
}

static int connect_via_eavesdrop(const char *address, sd_bus **out_bus) {
  sd_bus *bus = NULL;
  int r = sd_bus_new(&bus);
  if (r < 0)
    return r;

  r = sd_bus_set_bus_client(bus, true);
  if (r < 0) {
    sd_bus_unref(bus);
    return r;
  }

  r = sd_bus_set_address(bus, address);
  if (r < 0) {
    sd_bus_unref(bus);
    return r;
  }

  r = sd_bus_start(bus);
  if (r < 0) {
    sd_bus_unref(bus);
    return r;
  }

  r = sd_bus_add_match(bus, NULL,
                       "eavesdrop='true',interface='org.freedesktop."
                       "Notifications',member='Notify'",
                       on_message, NULL);
  if (r < 0) {
    sd_bus_unref(bus);
    return r;
  }

  *out_bus = bus;
  return 0;
}

static int connect_via_monitor(const char *address, sd_bus **out_bus) {
  sd_bus *bus = NULL;
  int r = sd_bus_new(&bus);
  if (r < 0)
    return r;

  sd_bus_set_bus_client(bus, true);
  sd_bus_negotiate_creds(bus, true,
                         SD_BUS_CREDS_AUGMENT | SD_BUS_CREDS_WELL_KNOWN_NAMES);
  sd_bus_negotiate_timestamp(bus, true);
  sd_bus_negotiate_fds(bus, true);

  r = sd_bus_set_monitor(bus, true);
  if (r < 0) {
    sd_bus_unref(bus);
    return r;
  }

  r = sd_bus_set_address(bus, address);
  if (r < 0) {
    sd_bus_unref(bus);
    return r;
  }

  r = sd_bus_start(bus);
  if (r < 0) {
    sd_bus_unref(bus);
    return r;
  }

  r = sd_bus_add_filter(bus, NULL, on_message, NULL);
  if (r < 0) {
    sd_bus_unref(bus);
    return r;
  }

  const char *rules[] = {
      "interface='org.freedesktop.Notifications',member='Notify'"};
  sd_bus_error error = SD_BUS_ERROR_NULL;
  r = sd_bus_call_method(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                         "org.freedesktop.DBus.Monitoring", "BecomeMonitor",
                         &error, NULL, "asu", 1, rules[0], 0);
  sd_bus_error_free(&error);
  if (r < 0) {
    sd_bus_unref(bus);
    return r;
  }

  *out_bus = bus;
  return 0;
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

int bus_listener_run(const config_t *cfg) {
  g_cfg = cfg;

  char address[MAX_PATH_LEN];
  if (resolve_session_address(address, sizeof(address)) < 0) {
    log_msg(LOG_ERROR,
            "could not determine session bus address "
            "(neither $DBUS_SESSION_BUS_ADDRESS nor $XDG_RUNTIME_DIR is set)");
    return 1;
  }

  int consecutive_failures = 0;

  for (;;) {
    sd_bus *bus = NULL;
    int r = connect_bus(address, &bus);
    if (r < 0) {
      consecutive_failures++;
      if (consecutive_failures >= 5) {
        log_msg(LOG_ERROR,
                "giving up after %d consecutive failed connection attempts",
                consecutive_failures);
        return 1;
      }
      log_msg(LOG_WARN, "retrying bus connection in 3s (attempt %d/5)",
              consecutive_failures);
      sleep(3);
      continue;
    }
    consecutive_failures = 0;

    log_msg(LOG_INFO, "listening for notifications from %d configured app(s)",
            cfg->app_count);

    for (;;) {
      r = sd_bus_process(bus, NULL);
      if (r < 0) {
        log_msg(LOG_WARN, "bus connection lost (%s), reconnecting",
                strerror(-r));
        break;
      }
      if (r > 0)
        continue;
      sd_bus_wait(bus, (uint64_t)-1);
    }

    sd_bus_unref(bus);
    sleep(1);
  }
}
