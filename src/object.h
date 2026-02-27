#ifndef OBJECT_H
#define OBJECT_H

#include "list.h"
#include <stdint.h>

typedef enum {
    OBJ_STRING,
    OBJ_INT,
    OBJ_LIST
} obj_type_t;

typedef struct {
    obj_type_t type;
    union {
        char *str;
        int64_t num;
        list_t *list;
    } data;
} dbobj_t;

/* Constructors */
dbobj_t *obj_create_string(const char *str);
dbobj_t *obj_create_int(int64_t num);
dbobj_t *obj_create_list(void);

/* Destructor */
void obj_free(dbobj_t *obj);

/* Try to parse a string as an integer; returns 1 on success */
int obj_try_parse_int(const char *str, int64_t *out);

/* Get string representation (caller must free for OBJ_INT) */
const char *obj_get_string(dbobj_t *obj);

#endif /* OBJECT_H */
