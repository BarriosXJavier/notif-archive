#define _GNU_SOURCE
#include "screenshot.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define WAIT_POLL_MS 50

static int is_cancelled(const volatile sig_atomic_t *cancelled) {
    return cancelled && *cancelled;
}

static int cancellable_delay(int milliseconds,
                             const volatile sig_atomic_t *cancelled) {
    int remaining = milliseconds;

    while (remaining > 0) {
        int slice = remaining > WAIT_POLL_MS ? WAIT_POLL_MS : remaining;
        struct timespec delay = {
            .tv_sec = slice / 1000,
            .tv_nsec = (long)(slice % 1000) * 1000000L,
        };

        if (is_cancelled(cancelled))
            return -1;
        if (nanosleep(&delay, NULL) < 0 && errno == EINTR &&
            is_cancelled(cancelled))
            return -1;
        remaining -= slice;
    }
    return is_cancelled(cancelled) ? -1 : 0;
}

static void remove_partial_file(const char *path) {
    if (unlink(path) < 0 && errno != ENOENT)
        log_msg(LOG_WARN, "cannot remove partial screenshot %s: %s", path,
                strerror(errno));
}

static void kill_group_and_reap(pid_t pid) {
    int status;

    if (kill(-pid, SIGKILL) < 0) {
        if (errno != ESRCH)
            log_msg(LOG_WARN, "failed to terminate screenshot process group: %s",
                    strerror(errno));
        if (kill(pid, SIGKILL) < 0 && errno != ESRCH)
            log_msg(LOG_WARN, "failed to terminate screenshot process: %s",
                    strerror(errno));
    }
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
}

static long long elapsed_ms(const struct timespec *start,
                            const struct timespec *now) {
    return (long long)(now->tv_sec - start->tv_sec) * 1000LL +
           (now->tv_nsec - start->tv_nsec) / 1000000LL;
}

static int valid_screenshot(const char *path) {
    struct stat st;

    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

int screenshot_capture(const config_t *cfg, const char *path,
                       const volatile sig_atomic_t *cancelled) {
    pid_t pid;
    int status = 0;
    struct timespec started;

    remove_partial_file(path);
    if (cancellable_delay(cfg->screenshot_delay_ms, cancelled) < 0) {
        log_msg(LOG_INFO, "screenshot cancelled during delay");
        return 0;
    }
    if (is_cancelled(cancelled))
        return 0;

    pid = fork();
    if (pid < 0) {
        log_msg(LOG_ERROR, "fork failed for screenshot: %s", strerror(errno));
        return 0;
    }

    if (pid == 0) {
        int devnull;

        if (setpgid(0, 0) < 0)
            _exit(126);
        umask(0077);
        devnull = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (devnull >= 0) {
            if (dup2(devnull, STDOUT_FILENO) < 0 ||
                dup2(devnull, STDERR_FILENO) < 0) {
                close(devnull);
                _exit(126);
            }
            close(devnull);
        }

        if (strcmp(cfg->session_type, "wayland") == 0) {
            execlp("grim", "grim", path, (char *)NULL);
            execlp("gnome-screenshot", "gnome-screenshot", "-f", path,
                   (char *)NULL);
        } else {
            execlp("scrot", "scrot", path, (char *)NULL);
            execlp("import", "import", "-window", "root", path, (char *)NULL);
        }
        _exit(127);
    }

    if (setpgid(pid, pid) < 0 && errno != EACCES && errno != ESRCH)
        log_msg(LOG_WARN, "cannot isolate screenshot process group: %s",
                strerror(errno));
    if (clock_gettime(CLOCK_MONOTONIC, &started) < 0) {
        log_msg(LOG_ERROR, "clock_gettime failed: %s", strerror(errno));
        kill_group_and_reap(pid);
        remove_partial_file(path);
        return 0;
    }

    for (;;) {
        pid_t waited = waitpid(pid, &status, WNOHANG);
        struct timespec now;
        struct timespec poll_delay = {
            .tv_sec = 0,
            .tv_nsec = WAIT_POLL_MS * 1000000L,
        };

        if (waited == pid)
            break;
        if (waited < 0) {
            if (errno == EINTR && is_cancelled(cancelled))
                log_msg(LOG_INFO, "screenshot cancelled during capture");
            else
                log_msg(LOG_ERROR, "waitpid failed for screenshot: %s",
                        strerror(errno));
            kill_group_and_reap(pid);
            remove_partial_file(path);
            return 0;
        }
        if (is_cancelled(cancelled)) {
            log_msg(LOG_INFO, "screenshot cancelled during capture");
            kill_group_and_reap(pid);
            remove_partial_file(path);
            return 0;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
            log_msg(LOG_ERROR, "clock_gettime failed: %s", strerror(errno));
            kill_group_and_reap(pid);
            remove_partial_file(path);
            return 0;
        }
        if (elapsed_ms(&started, &now) >= cfg->screenshot_timeout_ms) {
            log_msg(LOG_WARN, "screenshot timed out after %d ms",
                    cfg->screenshot_timeout_ms);
            kill_group_and_reap(pid);
            remove_partial_file(path);
            return 0;
        }
        if (nanosleep(&poll_delay, NULL) < 0 && errno == EINTR &&
            is_cancelled(cancelled)) {
            log_msg(LOG_INFO, "screenshot cancelled during capture");
            kill_group_and_reap(pid);
            remove_partial_file(path);
            return 0;
        }
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && valid_screenshot(path))
        return 1;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        log_msg(LOG_WARN, "screenshot tool produced no nonempty regular file");
    else if (WIFEXITED(status))
        log_msg(LOG_WARN, "screenshot capture failed (exit=%d)",
                WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        log_msg(LOG_WARN, "screenshot capture killed by signal %d", WTERMSIG(status));
    else
        log_msg(LOG_WARN, "screenshot capture ended unexpectedly");
    remove_partial_file(path);
    return 0;
}
