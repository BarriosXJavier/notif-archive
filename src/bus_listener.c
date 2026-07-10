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

static int on_message(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)userdata; (void)ret_error;

    const char *iface = sd_bus_message_get_interface(m);
    const char *member = sd_bus_message_get_member(m);
    if (!iface || !member) return 0;
    if (strcmp(iface, "org.freedesktop.Notifications") != 0) return 0;
    if (strcmp(member, "Notify") != 0) return 0;

    const char *app_name, *app_icon, *summary, *body;
    uint32_t replaces_id;

    int r = sd_bus_message_read(m, "susss", &app_name, &replaces_id,
                                 &app_icon, &summary, &body);
    if (r < 0) {
        log_msg(LOG_DEBUG, "skipped unparseable Notify call (r=%d)", r);
        return 0;
    }

    char group_name[MAX_APP_NAME];
    if (!config_resolve_group(g_cfg, app_name, group_name, sizeof(group_name))) return 0;

    log_msg(LOG_INFO, "captured notification from %s (group=%s)", app_name, group_name);

    char dir[MAX_PATH_LEN];
    storage_build_dir(g_cfg, group_name, dir, sizeof(dir));

    char shot_path[900];
    snprintf(shot_path, sizeof(shot_path), "%s/%ld.png", dir, (long)time(NULL));
    int got_shot = screenshot_capture(g_cfg, shot_path);

    parsed_message_t msg;
    parser_split(summary, body, &msg);

    storage_write_entry(dir, group_name, app_name, &msg, got_shot ? shot_path : NULL);

    return 0;
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
