#ifndef COMMAND_H
#define COMMAND_H

#include "db.h"
#include "resp.h"

/* Forward declaration */
typedef struct server server_t;

/* Execute a parsed RESP command and write the response */
void command_execute(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply);

#endif /* COMMAND_H */
