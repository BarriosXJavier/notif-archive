#ifndef BUS_LISTENER_H
#define BUS_LISTENER_H

#include "config.h"

// Blocks while processing the session bus. Returns zero after SIGINT/SIGTERM
// and non-zero after unrecoverable setup or repeated connection failures.
int bus_listener_run(const config_t *cfg);

#endif
