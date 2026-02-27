#include "hashtable.h"
#include "util.h"
#include <string.h>

#define HT_INITIAL_CAP  64
#define HT_LOAD_HIGH    0.70
#define HT_LOAD_LOW     0.20
#define HT_MIN_CAP      64

/* FNV-1a hash */
uint32_t ht_hash(const char *key) {
    uint32_t h = 2166136261u;
    for (const char *p = key; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    return h;
}

static size_t probe_index(size_t capacity, uint32_t hash, size_t i) {
    return (hash + i) & (capacity - 1);
}

/* Distance from ideal position (Robin Hood metric) */
static size_t probe_distance(size_t capacity, uint32_t hash, size_t slot) {
    return (slot + capacity - (hash & (capacity - 1))) & (capacity - 1);
}

static void ht_resize(hashtable_t *ht, size_t new_cap);

hashtable_t *ht_create(size_t initial_capacity, ht_free_fn free_fn) {
    if (initial_capacity < HT_INITIAL_CAP) initial_capacity = HT_INITIAL_CAP;

    /* Round up to power of 2 */
    size_t cap = 1;
    while (cap < initial_capacity) cap <<= 1;

    hashtable_t *ht = imdb_malloc(sizeof(hashtable_t));
    ht->entries = imdb_calloc(cap, sizeof(ht_entry_t));
    ht->capacity = cap;
    ht->size = 0;
    ht->tombstones = 0;
    ht->free_fn = free_fn;
    return ht;
}

void ht_destroy(hashtable_t *ht) {
    if (!ht) return;
    for (size_t i = 0; i < ht->capacity; i++) {
        if (ht->entries[i].occupied == 1) {
            imdb_free(ht->entries[i].key);
            if (ht->free_fn) ht->free_fn(ht->entries[i].value);
        }
    }
    imdb_free(ht->entries);
    imdb_free(ht);
}

int ht_set(hashtable_t *ht, const char *key, void *value) {
    /* Resize if load too high */
    if ((ht->size + ht->tombstones + 1) * 100 / ht->capacity > (size_t)(HT_LOAD_HIGH * 100)) {
        ht_resize(ht, ht->capacity * 2);
    }

    uint32_t h = ht_hash(key);
    char *new_key = imdb_strdup(key);
    void *new_value = value;
    int is_new = 1;

    for (size_t i = 0; ; i++) {
        size_t idx = probe_index(ht->capacity, h, i);
        ht_entry_t *e = &ht->entries[idx];

        if (e->occupied == 0 || e->occupied == 2) {
            /* Empty or tombstone slot — insert here */
            if (e->occupied == 2) ht->tombstones--;
            e->key = new_key;
            e->value = new_value;
            e->hash = h;
            e->occupied = 1;
            ht->size++;
            return is_new;
        }

        if (e->occupied == 1 && e->hash == h && strcmp(e->key, key) == 0) {
            /* Key exists — update value */
            if (ht->free_fn) ht->free_fn(e->value);
            e->value = new_value;
            imdb_free(new_key);
            return 0; /* not new */
        }

        /* Robin Hood: if current entry has shorter probe distance, swap */
        size_t cur_dist = probe_distance(ht->capacity, e->hash, idx);
        size_t new_dist = probe_distance(ht->capacity, h, idx);
        if (new_dist > cur_dist) {
            /* Swap entries */
            char *tmp_key = e->key;
            void *tmp_val = e->value;
            uint32_t tmp_hash = e->hash;

            e->key = new_key;
            e->value = new_value;
            e->hash = h;

            new_key = tmp_key;
            new_value = tmp_val;
            h = tmp_hash;
            is_new = 1;
        }
    }
}

static ht_entry_t *ht_find(hashtable_t *ht, const char *key) {
    uint32_t h = ht_hash(key);

    for (size_t i = 0; ; i++) {
        size_t idx = probe_index(ht->capacity, h, i);
        ht_entry_t *e = &ht->entries[idx];

        if (e->occupied == 0) return NULL;

        if (e->occupied == 1) {
            size_t dist = probe_distance(ht->capacity, e->hash, idx);
            if (i > dist) return NULL; /* Robin Hood guarantee */

            if (e->hash == h && strcmp(e->key, key) == 0) {
                return e;
            }
        }
        /* occupied == 2 (tombstone): keep probing */
    }
}

void *ht_get(hashtable_t *ht, const char *key) {
    ht_entry_t *e = ht_find(ht, key);
    return e ? e->value : NULL;
}

ht_entry_t *ht_get_entry(hashtable_t *ht, const char *key) {
    return ht_find(ht, key);
}

int ht_delete(hashtable_t *ht, const char *key) {
    ht_entry_t *e = ht_find(ht, key);
    if (!e) return 0;

    imdb_free(e->key);
    if (ht->free_fn) ht->free_fn(e->value);
    e->key = NULL;
    e->value = NULL;
    e->occupied = 2; /* tombstone */
    ht->size--;
    ht->tombstones++;

    /* Shrink if load too low */
    if (ht->capacity > HT_MIN_CAP &&
        ht->size * 100 / ht->capacity < (size_t)(HT_LOAD_LOW * 100)) {
        ht_resize(ht, ht->capacity / 2);
    }

    return 1;
}

int ht_exists(hashtable_t *ht, const char *key) {
    return ht_find(ht, key) != NULL;
}

size_t ht_size(hashtable_t *ht) {
    return ht->size;
}

static void ht_resize(hashtable_t *ht, size_t new_cap) {
    if (new_cap < HT_MIN_CAP) new_cap = HT_MIN_CAP;

    ht_entry_t *old_entries = ht->entries;
    size_t old_cap = ht->capacity;

    ht->entries = imdb_calloc(new_cap, sizeof(ht_entry_t));
    ht->capacity = new_cap;
    ht->size = 0;
    ht->tombstones = 0;

    for (size_t i = 0; i < old_cap; i++) {
        if (old_entries[i].occupied == 1) {
            /* Re-insert without duplicating key strings */
            uint32_t h = old_entries[i].hash;
            char *k = old_entries[i].key;
            void *v = old_entries[i].value;

            for (size_t j = 0; ; j++) {
                size_t idx = probe_index(new_cap, h, j);
                ht_entry_t *e = &ht->entries[idx];
                if (e->occupied == 0) {
                    e->key = k;
                    e->value = v;
                    e->hash = h;
                    e->occupied = 1;
                    ht->size++;
                    break;
                }
                /* Robin Hood during resize */
                size_t cur_dist = probe_distance(new_cap, e->hash, idx);
                size_t new_dist = probe_distance(new_cap, h, idx);
                if (new_dist > cur_dist) {
                    char *tk = e->key; void *tv = e->value; uint32_t th = e->hash;
                    e->key = k; e->value = v; e->hash = h;
                    k = tk; v = tv; h = th;
                }
            }
        }
    }

    imdb_free(old_entries);
}

void ht_iter_init(ht_iter_t *iter, hashtable_t *ht) {
    iter->ht = ht;
    iter->index = 0;
}

ht_entry_t *ht_iter_next(ht_iter_t *iter) {
    while (iter->index < iter->ht->capacity) {
        ht_entry_t *e = &iter->ht->entries[iter->index];
        iter->index++;
        if (e->occupied == 1) return e;
    }
    return NULL;
}
