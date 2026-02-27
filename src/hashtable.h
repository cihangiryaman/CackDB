#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *key;
    void *value;
    uint32_t hash;
    int occupied; /* 0 = empty, 1 = occupied, 2 = tombstone */
} ht_entry_t;

typedef void (*ht_free_fn)(void *value);

typedef struct {
    ht_entry_t *entries;
    size_t capacity;
    size_t size;       /* number of occupied entries */
    size_t tombstones; /* number of tombstones */
    ht_free_fn free_fn;
} hashtable_t;

/* Iterator for walking all entries */
typedef struct {
    hashtable_t *ht;
    size_t index;
} ht_iter_t;

/* Create / destroy */
hashtable_t *ht_create(size_t initial_capacity, ht_free_fn free_fn);
void ht_destroy(hashtable_t *ht);

/* Core operations */
int ht_set(hashtable_t *ht, const char *key, void *value);
void *ht_get(hashtable_t *ht, const char *key);
int ht_delete(hashtable_t *ht, const char *key);
int ht_exists(hashtable_t *ht, const char *key);

/* Get entry pointer (for TTL checks etc.) */
ht_entry_t *ht_get_entry(hashtable_t *ht, const char *key);

/* Size */
size_t ht_size(hashtable_t *ht);

/* Iterator */
void ht_iter_init(ht_iter_t *iter, hashtable_t *ht);
ht_entry_t *ht_iter_next(ht_iter_t *iter);

/* Hash function (FNV-1a) */
uint32_t ht_hash(const char *key);

#endif /* HASHTABLE_H */
