# notif-archiver

`notif-archiver` is a small C utility that listens for desktop notification traffic on the session D-Bus, filters for configured apps, captures a screenshot when a matching notification arrives, and appends the notification details to a JSON Lines archive.

## What it does

For each configured app:

1. listens for `org.freedesktop.Notifications.Notify` calls on the session bus,
2. checks whether the notification's `app_name` matches one of the configured mappings,
3. creates a dated archive directory under `archive_root`,
4. waits briefly so the notification toast is visible,
5. captures a full-screen screenshot, and
6. appends a structured record to `log.jsonl`.

The resulting archive is organized by app/group name and date.

## Where this runs

This project is intended for a **Linux desktop user session**.

It expects all of the following to be true:

- the machine is running Linux,
- the desktop environment exposes a **session D-Bus**,
- apps send notifications through `org.freedesktop.Notifications.Notify`,
- the session is graphical (`X11` or `Wayland`), and
- a screenshot tool is available in `PATH`.

Typical good fits:

- a personal Linux workstation or laptop,
- a desktop session managed with `systemd --user`,
- chat or messaging apps that already show normal desktop notifications.

Typical bad fits:

- macOS or Windows,
- headless servers,
- containers without a graphical session bus,
- environments where notifications are not sent via the standard freedesktop notification interface.

## What must already be installed

### Build-time requirements

To compile the project, the system should already have:

- `gcc`
- `make`
- `pkg-config`
- `libsystemd` development headers and libraries

### Runtime requirements

To run the archiver successfully, the system should already have:

- a Linux desktop session with access to the session bus,
- `systemd --user` if you want to run it via the included service unit,
- at least one supported screenshot tool installed,
- the target apps configured in `notif-archiver.conf` and sending standard desktop notifications.

- **Note that the target apps must have the message preview enabled**

Supported screenshot tools:

- Wayland: `grim` or `gnome-screenshot`
- X11: `scrot` or ImageMagick `import`

### Package examples

Package names vary by distribution, but these are the kinds of packages users should expect to install.

**Debian / Ubuntu**

```sh
sudo apt install build-essential pkg-config libsystemd-dev grim scrot gnome-screenshot imagemagick
```

**Fedora**

```sh
sudo dnf install gcc make pkgconf-pkg-config systemd-devel grim scrot gnome-screenshot ImageMagick
```

**Arch Linux**

```sh
sudo pacman -S base-devel pkgconf systemd grim scrot gnome-screenshot imagemagick
```

You do **not** need every screenshot tool above; you only need at least one tool that matches the session type.

## Build

This project uses `make` and depends on `libsystemd` for `sd-bus`.

```sh
make
```

The main binary is written to `./notif-archiver`.

## Running

By default, the program reads configuration from:

```text
~/.config/notif-archiver/notif-archiver.conf
```

You can also pass an explicit config path:

```sh
./notif-archiver /path/to/notif-archiver.conf
```

To discover notification identities without loading config, taking screenshots,
or writing archives:

```sh
./notif-archiver --list-apps
```

Trigger notifications from the apps you want to identify. Each distinct pair is
logged once:

```text
DISCOVER app_name='' desktop-entry='com.rtosta.zapzap'
DISCOVER app_name='Google Chrome' desktop-entry='google-chrome'
```

Press `Ctrl-C` when finished, then add the observed `app_name` or
`desktop-entry` values to `[apps]`.

## Installation

The repository now includes:

- `config/notif-archiver.conf` — sample configuration file
- `systemd/notif-archiver.service` — user service unit

To install them into your home directory using the provided `Makefile`:

```sh
make install
```

This installs:

- `notif-archiver` to `~/bin/notif-archiver`
- the sample config to `~/.config/notif-archiver/notif-archiver.conf`
- the service unit to `~/.config/systemd/user/notif-archiver.service`

Then enable the service:

```sh
systemctl --user enable --now notif-archiver.service
```

To inspect logs:

```sh
journalctl --user -u notif-archiver -f
```

## Smoke testing with fake notifications

The repository includes `scripts/smoke-test.sh` for testing the full notification pipeline without waiting for real messages.

Run it from an active graphical desktop session:

```sh
./scripts/smoke-test.sh
```

The script:

1. creates a temporary config and archive directory,
2. builds `notif-archiver` if the binary is missing,
3. starts a temporary archiver process,
4. sends one direct-message notification and one group-message notification with `notify-send`,
5. verifies that both records were written to `log.jsonl`, and
6. removes the temporary files after a successful run.

If the test fails, it keeps its temporary directory and prints the path so you can inspect the archiver log and any partial archive output.

The smoke test requires `notify-send`, normally provided by:

- Debian/Ubuntu: `libnotify-bin`
- Fedora/Arch Linux: `libnotify`

Because this is an end-to-end test, it must run inside the same Linux graphical user session whose D-Bus carries desktop notifications. It will briefly display two test notifications. Screenshot failure does not fail the smoke test; the test verifies notification capture, parsing, and JSONL storage.

## Configuration

The config parser supports simple `key=value` entries plus an `[apps]` section.

Recognized top-level keys:

- `archive_root` — base directory where archives are stored
- `session_type_override` — override detected session type (`x11` or `wayland`)
- `screenshot_delay_ms` — delay before taking the screenshot (0–600000)
- `screenshot_timeout_ms` — maximum screenshot-tool runtime before it is killed and reaped (100–600000; default 10000)

The `[apps]` section defines which notification `app_name` values should be archived.

Supported forms:

- `WhatsApp`
- `whatsapp-for-linux = WhatsApp`
- `* = Unsorted`

The second form maps multiple source application names into one canonical archive group.
The optional `*` mapping captures only notifications that match neither an exact
`app_name` nor an exact `desktop-entry`. Only one catch-all is allowed. Every
catch-all match emits a warning containing both observed identities so it can be
replaced with an explicit mapping.

Example:

```ini
archive_root=~/notif_archive
screenshot_delay_ms=300
screenshot_timeout_ms=10000

[apps]
WhatsApp
Telegram Desktop = Telegram
zapzap = WhatsApp
# * = Unsorted
```

## Notification source discovery

For Flatpak applications, list installed application IDs first:

```sh
flatpak list --app --columns=application,name
```

Run `notif-archiver --list-apps`, trigger one notification, and compare the
reported `desktop-entry` with the Flatpak ID. The observed D-Bus value is
authoritative; add it to `[apps]` exactly as logged.

Use the same procedure for browser-installed PWAs. A PWA may receive a unique
generated desktop entry, depending on the browser and installation.

Notifications from ordinary browser tabs generally expose only the browser's
identity (`Google Chrome`/`google-chrome`, `Firefox`/`firefox`). The originating
site is not a stable D-Bus application identity, so separate archival by tab or
site is intentionally unsupported. Install important sites as PWAs when they
need separate groups.

## Archive layout

For each captured notification, the program creates or reuses a directory shaped like:

```text
<archive_root>/<App_or_Group>/<YYYY-MM-DD>/
```

Example contents:

```text
~/notif_archive/WhatsApp/2026-07-10/
├── 1720612345.png
└── log.jsonl
```

Each line in `log.jsonl` is a standalone JSON object with fields such as:

- `timestamp`
- `app`
- `source_app`
- `replaces_id` (nonzero means the sender updated an existing desktop notification; the archive remains append-only)
- `group`
- `sender`
- `content`
- `screenshot`

Archive directories are created with mode `0700` and JSONL files with mode `0600`. Group names are percent-encoded for path safety; the original group remains unchanged in JSON.

## Core components

### `src/main.c`

Program entry point. Resolves the config path, loads `config_t`, validates that at least one app is configured, logs startup details, and hands off to the bus listener.

### `src/config.c` / `src/config.h`

Loads configuration, applies defaults, expands `~` in paths, parses the `[apps]` section, and resolves raw D-Bus `app_name` values to canonical archive group names.

### `src/bus_listener.c` / `src/bus_listener.h`

Owns the session D-Bus connection and long-running event loop.

It attempts to connect in two ways:

1. an eavesdrop match rule, and if that fails,
2. a fallback monitor connection using `BecomeMonitor`.

When a `Notify` message arrives, it filters by configured app name, builds the destination directory, triggers screenshot capture, parses the message, and writes the archive entry.

### `src/parser.c` / `src/parser.h`

Converts raw notification `summary` and `body` strings into a normalized message structure.

It handles two common cases:

- one-to-one chats where `summary` is the sender and `body` is the message,
- group chats where `summary` is the group name and `body` looks like `Sender: message`.

### `src/storage.c` / `src/storage.h`

Creates archive directories and appends JSON Lines records. It also escapes untrusted notification text before writing JSON.

### `src/screenshot.c` / `src/screenshot.h`

Captures a screenshot by forking and `exec`-ing an external tool appropriate for the current session type. The capture runs after a configurable delay so the notification is visible on screen.

### `src/log.c` / `src/log.h`

Shared timestamped logger used across the program. `INFO` and `DEBUG` messages go to stdout, while `WARN` and `ERROR` go to stderr.

## End-to-end flow

```text
main -> config_load -> bus_listener_run
     -> on matching Notify()
     -> storage_build_dir
     -> screenshot_capture
     -> parser_split
     -> storage_write_entry
```

`PROGRAM_FLOW.md` contains an ASCII diagram of the same pipeline.

## Notes

- Exact `app_name` mappings take precedence over exact `desktop-entry` mappings; the optional `*` catch-all runs last.
- The archive folder name uses the canonical group name, not necessarily the raw source app name.
- If screenshot capture fails or times out, the notification is still logged with `"screenshot": null`.
- Screenshot capture is synchronous. This preserves record order but bursts can delay later captures; a bounded asynchronous worker queue is a planned improvement.
- Screenshot filenames include seconds, nanoseconds, and a process-local sequence, so rapid notifications cannot overwrite each other.
- `SIGINT` and `SIGTERM` stop the listener cleanly. An in-flight screenshot child is killed and reaped if the wait is interrupted.
- The archive is append-only and has no built-in rotation. Use a separate retention/rotation policy if archive growth is a concern.
