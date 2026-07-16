#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include "config.h"

#include <signal.h>

// Returns 1 only when a nonempty regular screenshot is produced. The child
// process group is always terminated/reaped on timeout or cancellation.
int screenshot_capture(const config_t *cfg, const char *path,
                       const volatile sig_atomic_t *cancelled);

#endif
