#define _GNU_SOURCE
#include "screenshot.h"
#include "log.h"
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>

int screenshot_capture(const config_t *cfg, const char *path) {
    // Give the compositor time to actually paint the notification
    // toast before we capture the screen.
    if (cfg->screenshot_delay_ms > 0) {
        usleep(cfg->screenshot_delay_ms * 1000); // ms -> microseconds
    }

    pid_t pid = fork();
    if (pid < 0) {
        log_msg(LOG_ERROR, "fork failed for screenshot");
        return 0;
    }

    if (pid == 0) {
        // Child process: silence the tool's own stdout/stderr, then
        // become the screenshot tool via exec. fork+exec (not
        // system()) deliberately avoids any shell interpretation of
        // `path`, which matters once/if path is ever built from
        // untrusted input.
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, 1); dup2(devnull, 2); }

        if (strcmp(cfg->session_type, "wayland") == 0) {
            execlp("grim", "grim", path, (char *)NULL);
            execlp("gnome-screenshot", "gnome-screenshot", "-f", path, (char *)NULL);
        } else {
            execlp("scrot", "scrot", path, (char *)NULL);
            execlp("import", "import", "-window", "root", path, (char *)NULL);
        }
        _exit(127); // none of the tools above exist -- give up cleanly
    }

    // Parent: wait for the child (whichever tool it became) to finish,
    // then check its exit status.
    int status;
    waitpid(pid, &status, 0);
    int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok) log_msg(LOG_WARN, "screenshot capture failed (exit=%d)",
                      WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    return ok;
}
