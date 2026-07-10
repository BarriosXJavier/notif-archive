#ifndef BUS_LISTENER_H
#define BUS_LISTENER_H
#include "config.h"

// Blocks forever, processing the session bus and dispatching matching
// Notify() calls to the archive pipeline. Only returns (non-zero) on
// unrecoverable setup failure, e.g. can't determine the bus address at
// all, or 5 consecutive connection attempts all fail.
int bus_listener_run(const config_t *cfg);

#endif
