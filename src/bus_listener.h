#ifndef BUS_LISTENER_H
#define BUS_LISTENER_H

#include "config.h"

typedef enum {
    BUS_LISTENER_ARCHIVE,
    BUS_LISTENER_DISCOVER,
} bus_listener_mode_t;

// Blocks while processing the session bus. cfg is required in archive mode and
// ignored in discovery mode. Returns zero after SIGINT/SIGTERM and non-zero
// after unrecoverable setup or repeated connection failures.
int bus_listener_run(const config_t *cfg, bus_listener_mode_t mode);

#endif
