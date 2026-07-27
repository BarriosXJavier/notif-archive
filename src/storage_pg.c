#include "storage_pg.h"
#include "log.h"
#include <stdlib.h>

PGconn *storage_pg_connect(const config_t *cfg) {
  (void)cfg;
  // TODO: real PQconnectdb() call goes here
  log_msg(LOG_WARN, "storage_pg_connect: not yet implemented");
  return NULL;
}

void storage_pg_insert(PGconn *conn, const char *group_name,
                       const char *source_app, const parsed_message_t *msg,
                       const char *screenshot_path) {
  (void)group_name;
  (void)source_app;
  (void)msg;
  (void)screenshot_path;
  if (!conn)
    return; // pg disabled or not connected -- silent no-op
            // TODO: real PQexecParams() insert goes here
}

void storage_pg_close(PGconn *conn) {
  (void)conn;
  // TODO: real PQfinish() goes here
}
