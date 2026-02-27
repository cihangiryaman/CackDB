#include "db.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPIRE_SWEEP_INTERVAL 100   /* ms between sweeps */
#define EXPIRE_SWEEP_SAMPLES  20    /* keys to sample per sweep */

static void entry_free(void *ptr) {
    db_entry_t *entry = (db_entry_t *)ptr;
    if (entry) {
        obj_free(entry->obj);
        imdb_free(entry);
    }
}

database_t *db_create(void) {
    database_t *db = imdb_malloc(sizeof(database_t));
    db->ht = ht_create(64, entry_free);
    db->last_expire_sweep = imdb_mstime();
    return db;
}

void db_destroy(database_t *db) {
    if (!db) return;
    ht_destroy(db->ht);
    imdb_free(db);
}

/* Check and remove key if expired. Returns 1 if key was expired. */
static int check_expired(database_t *db, const char *key) {
    ht_entry_t *he = ht_get_entry(db->ht, key);
    if (!he) return 0;
    db_entry_t *entry = (db_entry_t *)he->value;
    if (entry->expire >= 0 && imdb_mstime() > entry->expire) {
        ht_delete(db->ht, key);
        return 1;
    }
    return 0;
}

db_entry_t *db_get_entry(database_t *db, const char *key) {
    if (check_expired(db, key)) return NULL;
    return (db_entry_t *)ht_get(db->ht, key);
}

/* ---- String/Int operations ---- */

int db_set(database_t *db, const char *key, const char *value) {
    db_entry_t *entry = imdb_malloc(sizeof(db_entry_t));
    int64_t num;
    if (obj_try_parse_int(value, &num)) {
        entry->obj = obj_create_int(num);
    } else {
        entry->obj = obj_create_string(value);
    }
    entry->expire = -1;
    ht_set(db->ht, key, entry);
    return 1;
}

dbobj_t *db_get(database_t *db, const char *key) {
    db_entry_t *entry = db_get_entry(db, key);
    return entry ? entry->obj : NULL;
}

int db_del(database_t *db, const char *key) {
    check_expired(db, key);
    return ht_delete(db->ht, key);
}

int db_exists(database_t *db, const char *key) {
    if (check_expired(db, key)) return 0;
    return ht_exists(db->ht, key);
}

/* Returns new value, or INT64_MIN on type error */
int64_t db_incr(database_t *db, const char *key, int64_t delta) {
    if (check_expired(db, key)) {
        /* Key doesn't exist — treat as 0 */
        db_entry_t *entry = imdb_malloc(sizeof(db_entry_t));
        entry->obj = obj_create_int(delta);
        entry->expire = -1;
        ht_set(db->ht, key, entry);
        return delta;
    }

    db_entry_t *entry = (db_entry_t *)ht_get(db->ht, key);
    if (!entry) {
        entry = imdb_malloc(sizeof(db_entry_t));
        entry->obj = obj_create_int(delta);
        entry->expire = -1;
        ht_set(db->ht, key, entry);
        return delta;
    }

    if (entry->obj->type == OBJ_INT) {
        entry->obj->data.num += delta;
        return entry->obj->data.num;
    } else if (entry->obj->type == OBJ_STRING) {
        int64_t num;
        if (obj_try_parse_int(entry->obj->data.str, &num)) {
            obj_free(entry->obj);
            entry->obj = obj_create_int(num + delta);
            return num + delta;
        }
    }
    return INT64_MIN; /* type error */
}

/* ---- List operations ---- */

static db_entry_t *get_or_create_list(database_t *db, const char *key) {
    check_expired(db, key);
    db_entry_t *entry = (db_entry_t *)ht_get(db->ht, key);
    if (!entry) {
        entry = imdb_malloc(sizeof(db_entry_t));
        entry->obj = obj_create_list();
        entry->expire = -1;
        ht_set(db->ht, key, entry);
    }
    return entry;
}

int db_lpush(database_t *db, const char *key, const char *value) {
    db_entry_t *entry = get_or_create_list(db, key);
    if (entry->obj->type != OBJ_LIST) return -1;
    list_lpush(entry->obj->data.list, value);
    return (int)list_length(entry->obj->data.list);
}

int db_rpush(database_t *db, const char *key, const char *value) {
    db_entry_t *entry = get_or_create_list(db, key);
    if (entry->obj->type != OBJ_LIST) return -1;
    list_rpush(entry->obj->data.list, value);
    return (int)list_length(entry->obj->data.list);
}

char *db_lpop(database_t *db, const char *key) {
    if (check_expired(db, key)) return NULL;
    db_entry_t *entry = (db_entry_t *)ht_get(db->ht, key);
    if (!entry || entry->obj->type != OBJ_LIST) return NULL;
    char *val = list_lpop(entry->obj->data.list);
    if (list_length(entry->obj->data.list) == 0) ht_delete(db->ht, key);
    return val;
}

char *db_rpop(database_t *db, const char *key) {
    if (check_expired(db, key)) return NULL;
    db_entry_t *entry = (db_entry_t *)ht_get(db->ht, key);
    if (!entry || entry->obj->type != OBJ_LIST) return NULL;
    char *val = list_rpop(entry->obj->data.list);
    if (list_length(entry->obj->data.list) == 0) ht_delete(db->ht, key);
    return val;
}

int64_t db_llen(database_t *db, const char *key) {
    if (check_expired(db, key)) return 0;
    db_entry_t *entry = (db_entry_t *)ht_get(db->ht, key);
    if (!entry) return 0;
    if (entry->obj->type != OBJ_LIST) return -1;
    return (int64_t)list_length(entry->obj->data.list);
}

char **db_lrange(database_t *db, const char *key, int start, int stop, size_t *count) {
    *count = 0;
    if (check_expired(db, key)) return NULL;
    db_entry_t *entry = (db_entry_t *)ht_get(db->ht, key);
    if (!entry || entry->obj->type != OBJ_LIST) return NULL;
    return list_range(entry->obj->data.list, start, stop, count);
}

/* ---- TTL operations ---- */

int db_expire(database_t *db, const char *key, int64_t seconds) {
    db_entry_t *entry = db_get_entry(db, key);
    if (!entry) return 0;
    entry->expire = imdb_mstime() + seconds * 1000;
    return 1;
}

/* Returns: -2 if key missing, -1 if no TTL, else seconds remaining */
int64_t db_ttl(database_t *db, const char *key) {
    db_entry_t *entry = db_get_entry(db, key);
    if (!entry) return -2;
    if (entry->expire < 0) return -1;
    int64_t remaining = (entry->expire - imdb_mstime()) / 1000;
    return remaining > 0 ? remaining : 0;
}

int db_persist(database_t *db, const char *key) {
    db_entry_t *entry = db_get_entry(db, key);
    if (!entry) return 0;
    if (entry->expire < 0) return 0;
    entry->expire = -1;
    return 1;
}

/* ---- Utility ---- */

size_t db_size(database_t *db) {
    return ht_size(db->ht);
}

void db_flush(database_t *db) {
    ht_destroy(db->ht);
    db->ht = ht_create(64, entry_free);
}

/* Periodic expiry: sample random keys and delete expired ones */
void db_expire_sweep(database_t *db) {
    int64_t now = imdb_mstime();
    if (now - db->last_expire_sweep < EXPIRE_SWEEP_INTERVAL) return;
    db->last_expire_sweep = now;

    if (ht_size(db->ht) == 0) return;

    size_t checked = 0;
    ht_iter_t iter;
    ht_iter_init(&iter, db->ht);
    ht_entry_t *he;

    while (checked < EXPIRE_SWEEP_SAMPLES && (he = ht_iter_next(&iter)) != NULL) {
        db_entry_t *entry = (db_entry_t *)he->value;
        if (entry->expire >= 0 && now > entry->expire) {
            /* Need to delete by key, which rehashes, so save key first */
            char *key = imdb_strdup(he->key);
            ht_delete(db->ht, key);
            imdb_free(key);
            /* Iterator may be invalidated after delete; restart */
            ht_iter_init(&iter, db->ht);
        }
        checked++;
    }
}

hashtable_t *db_get_ht(database_t *db) {
    return db->ht;
}
