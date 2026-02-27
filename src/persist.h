#ifndef PERSIST_H
#define PERSIST_H

#include "db.h"

/* Save database to an RDB-style binary file. Returns 0 on success. */
int persist_save(database_t *db, const char *filename);

/* Load database from an RDB-style binary file. Returns 0 on success. */
int persist_load(database_t *db, const char *filename);

#endif /* PERSIST_H */
