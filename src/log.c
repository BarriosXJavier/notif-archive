// _GNU_SOURCE exposes localtime_r() under -std=c11 (it's a POSIX
// extension, not pulled in by the strict ISO C standard alone).
#define _GNU_SOURCE
#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static const char *level_str(log_level_t l) {
    switch (l) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO";
        case LOG_WARN:  return "WARN";
        case LOG_ERROR: return "ERROR";
    }
    return "?"; // unreachable given the enum, but keeps the compiler quiet
}

void log_msg(log_level_t level, const char *fmt, ...) {
    char ts[32] = "unknown-time";
    time_t now = time(NULL);
    struct tm tm_now;

    if (!fmt)
        return;
    if (localtime_r(&now, &tm_now))
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);

    // WARN/ERROR go to stderr so `2>` redirection and journald severity
    // tagging both work correctly; DEBUG/INFO go to stdout.
    FILE *out = (level == LOG_ERROR || level == LOG_WARN) ? stderr : stdout;
    fprintf(out, "[%s] %-5s ", ts, level_str(level));

    // Standard C variadic-argument forwarding idiom: va_start/vfprintf/va_end.
    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);

    fprintf(out, "\n");
    fflush(out); // don't let a crash lose the last buffered log line
}
