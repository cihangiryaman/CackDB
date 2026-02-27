#include "object.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

dbobj_t *obj_create_string(const char *str) {
    dbobj_t *obj = imdb_malloc(sizeof(dbobj_t));
    obj->type = OBJ_STRING;
    obj->data.str = imdb_strdup(str);
    return obj;
}

dbobj_t *obj_create_int(int64_t num) {
    dbobj_t *obj = imdb_malloc(sizeof(dbobj_t));
    obj->type = OBJ_INT;
    obj->data.num = num;
    return obj;
}

dbobj_t *obj_create_list(void) {
    dbobj_t *obj = imdb_malloc(sizeof(dbobj_t));
    obj->type = OBJ_LIST;
    obj->data.list = list_create();
    return obj;
}

void obj_free(dbobj_t *obj) {
    if (!obj) return;
    switch (obj->type) {
        case OBJ_STRING:
            imdb_free(obj->data.str);
            break;
        case OBJ_INT:
            break;
        case OBJ_LIST:
            list_destroy(obj->data.list);
            break;
    }
    imdb_free(obj);
}

int obj_try_parse_int(const char *str, int64_t *out) {
    if (!str || !*str) return 0;
    char *end;
    errno = 0;
    long long val = strtoll(str, &end, 10);
    if (errno != 0 || *end != '\0') return 0;
    *out = (int64_t)val;
    return 1;
}

const char *obj_get_string(dbobj_t *obj) {
    if (!obj) return NULL;
    switch (obj->type) {
        case OBJ_STRING: return obj->data.str;
        case OBJ_INT:    return NULL; /* caller should use snprintf */
        case OBJ_LIST:   return NULL;
    }
    return NULL;
}
