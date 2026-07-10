#ifndef LOG_H
#define LOG_H

// Severity levels, roughly matching syslog conventions. DEBUG/INFO go
// to stdout, WARN/ERROR go to stderr -- see log.c.
typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } log_level_t;

// printf-style logger used by every other module in this program.
// Timestamps and formats consistently so `journalctl --user -u
// notif-archiver` output is easy to read and grep.
void log_msg(log_level_t level, const char *fmt, ...);

#endif
