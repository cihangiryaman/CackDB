#ifndef DB_H
#define DB_H

#include "hashtable.h"
#include "object.h"
#include <stdint.h>

/* Database entry wraps a value object with optional TTL */
typedef struct {
    dbobj_t *obj;
    int64_t expire; /* -1 = no expiry, otherwise ms timestamp */
} db_entry_t;

typedef struct {
    hashtable_t *ht;
    int64_t last_expire_sweep;
} database_t;

/* Create / destroy */
database_t *db_create(void);
void db_destroy(database_t *db);

/* String/Int operations */
int db_set(database_t *db, const char *key, const char *value);
dbobj_t *db_get(database_t *db, const char *key);
int db_del(database_t *db, const char *key);
int db_exists(database_t *db, const char *key);
int64_t db_incr(database_t *db, const char *key, int64_t delta);

/* List operations */
int db_lpush(database_t *db, const char *key, const char *value);
int db_rpush(database_t *db, const char *key, const char *value);
char *db_lpop(database_t *db, const char *key);
char *db_rpop(database_t *db, const char *key);
int64_t db_llen(database_t *db, const char *key);
char **db_lrange(database_t *db, const char *key, int start, int stop, size_t *count);

/* TTL operations */
int db_expire(database_t *db, const char *key, int64_t seconds);
int64_t db_ttl(database_t *db, const char *key);
int db_persist(database_t *db, const char *key);

/* Utility */
size_t db_size(database_t *db);
void db_flush(database_t *db);

/* Periodic expiry sweep — call from event loop */
void db_expire_sweep(database_t *db);

/* Get raw entry (for persistence) */
db_entry_t *db_get_entry(database_t *db, const char *key);

/* Iterator access to underlying hashtable */
hashtable_t *db_get_ht(database_t *db);

#endif /* DB_H */
