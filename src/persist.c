#include "persist.h"
#include "hashtable.h"
#include "object.h"
#include "list.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * RDB File Format:
 *   Header:  "IMDB0001" (8 bytes magic)
 *   Entries: [type(1)] [expire(8)] [key_len(4)] [key] [value_data...]
 *     type 0 = string: [val_len(4)] [val]
 *     type 1 = integer: [int64(8)]
 *     type 2 = list: [count(4)] { [val_len(4)] [val] }*
 *   Footer:  0xFF (1 byte)
 */

#define RDB_MAGIC "IMDB0001"
#define RDB_MAGIC_LEN 8
#define RDB_EOF 0xFF
#define RDB_TYPE_STRING 0
#define RDB_TYPE_INT    1
#define RDB_TYPE_LIST   2

static int write_uint32(FILE *f, uint32_t v) {
    return fwrite(&v, sizeof(v), 1, f) == 1 ? 0 : -1;
}

static int write_int64(FILE *f, int64_t v) {
    return fwrite(&v, sizeof(v), 1, f) == 1 ? 0 : -1;
}

static int write_string(FILE *f, const char *s, uint32_t len) {
    if (write_uint32(f, len) != 0) return -1;
    if (len > 0 && fwrite(s, 1, len, f) != len) return -1;
    return 0;
}

static int read_uint32(FILE *f, uint32_t *v) {
    return fread(v, sizeof(*v), 1, f) == 1 ? 0 : -1;
}

static int read_int64(FILE *f, int64_t *v) {
    return fread(v, sizeof(*v), 1, f) == 1 ? 0 : -1;
}

static char *read_string(FILE *f, uint32_t *out_len) {
    uint32_t len;
    if (read_uint32(f, &len) != 0) return NULL;
    char *s = imdb_malloc(len + 1);
    if (len > 0 && fread(s, 1, len, f) != len) {
        imdb_free(s);
        return NULL;
    }
    s[len] = '\0';
    *out_len = len;
    return s;
}

int persist_save(database_t *db, const char *filename) {
    char tmp_name[256];
    snprintf(tmp_name, sizeof(tmp_name), "%s.tmp", filename);

    FILE *f = fopen(tmp_name, "wb");
    if (!f) return -1;

    /* Write magic header */
    if (fwrite(RDB_MAGIC, 1, RDB_MAGIC_LEN, f) != RDB_MAGIC_LEN) goto fail;

    /* Iterate all entries */
    hashtable_t *ht = db_get_ht(db);
    ht_iter_t iter;
    ht_iter_init(&iter, ht);
    ht_entry_t *he;

    while ((he = ht_iter_next(&iter)) != NULL) {
        db_entry_t *entry = (db_entry_t *)he->value;
        dbobj_t *obj = entry->obj;
        uint8_t type;

        switch (obj->type) {
            case OBJ_STRING: type = RDB_TYPE_STRING; break;
            case OBJ_INT:    type = RDB_TYPE_INT;    break;
            case OBJ_LIST:   type = RDB_TYPE_LIST;   break;
            default: continue;
        }

        /* Write type byte */
        if (fwrite(&type, 1, 1, f) != 1) goto fail;

        /* Write expire */
        if (write_int64(f, entry->expire) != 0) goto fail;

        /* Write key */
        if (write_string(f, he->key, (uint32_t)strlen(he->key)) != 0) goto fail;

        /* Write value */
        switch (obj->type) {
            case OBJ_STRING:
                if (write_string(f, obj->data.str, (uint32_t)strlen(obj->data.str)) != 0) goto fail;
                break;
            case OBJ_INT:
                if (write_int64(f, obj->data.num) != 0) goto fail;
                break;
            case OBJ_LIST: {
                list_t *list = obj->data.list;
                uint32_t count = (uint32_t)list_length(list);
                if (write_uint32(f, count) != 0) goto fail;
                list_node_t *node = list->head;
                while (node) {
                    if (write_string(f, node->value, (uint32_t)strlen(node->value)) != 0) goto fail;
                    node = node->next;
                }
                break;
            }
        }
    }

    /* Write EOF marker */
    uint8_t eof = RDB_EOF;
    if (fwrite(&eof, 1, 1, f) != 1) goto fail;

    fclose(f);

    /* Atomic rename */
    remove(filename);
    if (rename(tmp_name, filename) != 0) {
        remove(tmp_name);
        return -1;
    }

    printf("Database saved to %s\n", filename);
    return 0;

fail:
    fclose(f);
    remove(tmp_name);
    return -1;
}

int persist_load(database_t *db, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;

    /* Verify magic */
    char magic[RDB_MAGIC_LEN];
    if (fread(magic, 1, RDB_MAGIC_LEN, f) != RDB_MAGIC_LEN ||
        memcmp(magic, RDB_MAGIC, RDB_MAGIC_LEN) != 0) {
        fclose(f);
        return -1;
    }

    int loaded = 0;
    while (1) {
        uint8_t type;
        if (fread(&type, 1, 1, f) != 1) break;
        if (type == RDB_EOF) break;

        /* Read expire */
        int64_t expire;
        if (read_int64(f, &expire) != 0) break;

        /* Read key */
        uint32_t key_len;
        char *key = read_string(f, &key_len);
        if (!key) break;

        /* Skip expired keys */
        if (expire >= 0 && imdb_mstime() > expire) {
            imdb_free(key);
            /* Skip value data */
            if (type == RDB_TYPE_STRING) {
                uint32_t vlen; char *v = read_string(f, &vlen);
                imdb_free(v);
            } else if (type == RDB_TYPE_INT) {
                int64_t tmp; read_int64(f, &tmp);
            } else if (type == RDB_TYPE_LIST) {
                uint32_t count; read_uint32(f, &count);
                for (uint32_t i = 0; i < count; i++) {
                    uint32_t vlen; char *v = read_string(f, &vlen);
                    imdb_free(v);
                }
            }
            continue;
        }

        /* Read and create entry */
        db_entry_t *entry = imdb_malloc(sizeof(db_entry_t));
        entry->expire = expire;

        if (type == RDB_TYPE_STRING) {
            uint32_t val_len;
            char *val = read_string(f, &val_len);
            if (!val) { imdb_free(key); imdb_free(entry); break; }
            entry->obj = obj_create_string(val);
            imdb_free(val);
        } else if (type == RDB_TYPE_INT) {
            int64_t num;
            if (read_int64(f, &num) != 0) { imdb_free(key); imdb_free(entry); break; }
            entry->obj = obj_create_int(num);
        } else if (type == RDB_TYPE_LIST) {
            uint32_t count;
            if (read_uint32(f, &count) != 0) { imdb_free(key); imdb_free(entry); break; }
            entry->obj = obj_create_list();
            for (uint32_t i = 0; i < count; i++) {
                uint32_t vlen;
                char *val = read_string(f, &vlen);
                if (!val) break;
                list_rpush(entry->obj->data.list, val);
                imdb_free(val);
            }
        } else {
            imdb_free(key);
            imdb_free(entry);
            break;
        }

        ht_set(db_get_ht(db), key, entry);
        imdb_free(key);
        loaded++;
    }

    fclose(f);
    printf("Loaded %d keys from %s\n", loaded, filename);
    return 0;
}
