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

## Configuration

The config parser supports simple `key=value` entries plus an `[apps]` section.

Recognized top-level keys:

- `archive_root` — base directory where archives are stored
- `session_type_override` — override detected session type (`x11` or `wayland`)
- `screenshot_delay_ms` — delay before taking the screenshot

The `[apps]` section defines which notification `app_name` values should be archived.

Supported forms:

- `WhatsApp`
- `whatsapp-for-linux = WhatsApp`

The second form maps multiple source application names into one canonical archive group.

Example:

```ini
archive_root=~/notif_archive
screenshot_delay_ms=300

[apps]
WhatsApp
Telegram Desktop = Telegram
zapzap = WhatsApp
```

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
- `group`
- `sender`
- `content`
- `screenshot`

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

- The program only archives notifications whose `app_name` appears in the `[apps]` section.
- The archive folder name uses the canonical group name, not necessarily the raw source app name.
- If screenshot capture fails, the notification is still logged with `"screenshot": null`.
