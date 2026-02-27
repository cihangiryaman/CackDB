#ifndef LIST_H
#define LIST_H

#include <stddef.h>

typedef struct list_node {
    struct list_node *prev;
    struct list_node *next;
    char *value;
} list_node_t;

typedef struct {
    list_node_t *head;
    list_node_t *tail;
    size_t length;
} list_t;

/* Create / destroy */
list_t *list_create(void);
void list_destroy(list_t *list);

/* Push operations */
void list_lpush(list_t *list, const char *value);
void list_rpush(list_t *list, const char *value);

/* Pop operations (caller must free returned string) */
char *list_lpop(list_t *list);
char *list_rpop(list_t *list);

/* Length */
size_t list_length(list_t *list);

/* Range — returns array of strings, sets *count. Caller must free array (not strings). */
char **list_range(list_t *list, int start, int stop, size_t *count);

#endif /* LIST_H */
