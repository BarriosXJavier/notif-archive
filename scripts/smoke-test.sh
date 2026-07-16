#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
BINARY="$PROJECT_DIR/notif-archiver"
APP_NAME="notif-archiver-smoke-test"
ARCHIVE_GROUP="SmokeTest"
ARCHIVER_PID=""
KEEP_ARTIFACTS=1

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'error: required command not found: %s\n' "$1" >&2
        exit 1
    fi
}

cleanup() {
    if [ -n "$ARCHIVER_PID" ] && kill -0 "$ARCHIVER_PID" 2>/dev/null; then
        kill "$ARCHIVER_PID" 2>/dev/null || true
        wait "$ARCHIVER_PID" 2>/dev/null || true
    fi

    if [ "$KEEP_ARTIFACTS" -eq 0 ]; then
        rm -rf "$TEST_DIR"
    else
        printf 'smoke-test artifacts kept at: %s\n' "$TEST_DIR" >&2
    fi
}

require_command make
require_command notify-send
require_command grep
require_command mktemp

TEST_DIR=$(mktemp -d "${TMPDIR:-/tmp}/notif-archiver-smoke.XXXXXX")
trap cleanup EXIT INT TERM

CONFIG_PATH="$TEST_DIR/notif-archiver.conf"
ARCHIVE_ROOT="$TEST_DIR/archive"
ARCHIVER_LOG="$TEST_DIR/notif-archiver.log"

cat >"$CONFIG_PATH" <<EOF
archive_root=$ARCHIVE_ROOT
screenshot_delay_ms=0

[apps]
$APP_NAME = $ARCHIVE_GROUP
EOF

if [ ! -x "$BINARY" ]; then
    make -C "$PROJECT_DIR"
fi

"$BINARY" "$CONFIG_PATH" >"$ARCHIVER_LOG" 2>&1 &
ARCHIVER_PID=$!

ready=0
attempt=0
while [ "$attempt" -lt 5 ]; do
    if grep -q "listening for notifications" "$ARCHIVER_LOG"; then
        ready=1
        break
    fi

    if ! kill -0 "$ARCHIVER_PID" 2>/dev/null; then
        printf 'error: notif-archiver exited before becoming ready\n' >&2
        cat "$ARCHIVER_LOG" >&2
        exit 1
    fi

    attempt=$((attempt + 1))
    sleep 1
done

if [ "$ready" -ne 1 ]; then
    printf 'error: timed out waiting for notif-archiver to listen on D-Bus\n' >&2
    cat "$ARCHIVER_LOG" >&2
    exit 1
fi

notify-send --app-name="$APP_NAME" \
    "Smoke Test Direct" \
    "Hello from the notif-archiver smoke test"

# Screenshot filenames currently use second-resolution timestamps.
sleep 1

notify-send --app-name="$APP_NAME" \
    "Smoke Test Group" \
    "Smoke Sender: Group notification from the smoke test"

attempt=0
LOG_PATH=""
while [ "$attempt" -lt 5 ]; do
    LOG_PATH="$ARCHIVE_ROOT/$ARCHIVE_GROUP/$(date +%F)/log.jsonl"
    if [ -f "$LOG_PATH" ] && \
       grep -q '"sender":"Smoke Test Direct"' "$LOG_PATH" && \
       grep -q '"group":"Smoke Test Group"' "$LOG_PATH" && \
       grep -q '"sender":"Smoke Sender"' "$LOG_PATH"; then
        printf 'smoke test passed\n'
        printf 'verified archive log: %s\n' "$LOG_PATH"
        cat "$LOG_PATH"
        KEEP_ARTIFACTS=0
        exit 0
    fi

    attempt=$((attempt + 1))
    sleep 1
done

printf 'error: expected notification records were not written\n' >&2
printf 'archiver log:\n' >&2
cat "$ARCHIVER_LOG" >&2
if [ -n "$LOG_PATH" ] && [ -f "$LOG_PATH" ]; then
    printf 'archive log:\n' >&2
    cat "$LOG_PATH" >&2
fi
exit 1
