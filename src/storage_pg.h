#ifndef STORAGE_PG_H
#define STORAGE_PG_H
#include "config.h"
#include "parser.h"
#include <libpq-fe.h>

// Opens a connection to Postgres using cfg's pg_* fields plus the
// $NOTIF_ARCHIVER_PG_PASSWORD environment variable for the password
// (never read from the config file). Returns a live connection handle
// on success, or NULL on failure (with a reason already logged).
// Caller owns the returned handle and must eventually pass it to
// storage_pg_close().
PGconn *storage_pg_connect(const config_t *cfg);

// Inserts one notification row. Safe to call with conn == NULL (e.g.
// if pg_enabled is false) -- becomes a no-op. screenshot_path may be
// NULL, mirroring storage_write_entry's contract.
void storage_pg_insert(PGconn *conn, const char *group_name,
                        const char *source_app, const parsed_message_t *msg,
                        const char *screenshot_path);

// Closes conn if non-NULL. Safe to call with conn == NULL.
void storage_pg_close(PGconn *conn);

#endif
