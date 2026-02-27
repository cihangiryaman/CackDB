#include "list.h"
#include "util.h"
#include <string.h>

list_t *list_create(void) {
    list_t *list = imdb_calloc(1, sizeof(list_t));
    return list;
}

void list_destroy(list_t *list) {
    if (!list) return;
    list_node_t *n = list->head;
    while (n) {
        list_node_t *next = n->next;
        imdb_free(n->value);
        imdb_free(n);
        n = next;
    }
    imdb_free(list);
}

static list_node_t *make_node(const char *value) {
    list_node_t *n = imdb_malloc(sizeof(list_node_t));
    n->value = imdb_strdup(value);
    n->prev = NULL;
    n->next = NULL;
    return n;
}

void list_lpush(list_t *list, const char *value) {
    list_node_t *n = make_node(value);
    if (list->head) {
        n->next = list->head;
        list->head->prev = n;
        list->head = n;
    } else {
        list->head = list->tail = n;
    }
    list->length++;
}

void list_rpush(list_t *list, const char *value) {
    list_node_t *n = make_node(value);
    if (list->tail) {
        n->prev = list->tail;
        list->tail->next = n;
        list->tail = n;
    } else {
        list->head = list->tail = n;
    }
    list->length++;
}

char *list_lpop(list_t *list) {
    if (!list->head) return NULL;
    list_node_t *n = list->head;
    list->head = n->next;
    if (list->head) list->head->prev = NULL;
    else list->tail = NULL;
    list->length--;

    char *val = n->value;
    imdb_free(n);
    return val;
}

char *list_rpop(list_t *list) {
    if (!list->tail) return NULL;
    list_node_t *n = list->tail;
    list->tail = n->prev;
    if (list->tail) list->tail->next = NULL;
    else list->head = NULL;
    list->length--;

    char *val = n->value;
    imdb_free(n);
    return val;
}

size_t list_length(list_t *list) {
    return list->length;
}

char **list_range(list_t *list, int start, int stop, size_t *count) {
    int len = (int)list->length;

    /* Convert negative indices */
    if (start < 0) start = len + start;
    if (stop < 0) stop = len + stop;
    if (start < 0) start = 0;
    if (stop >= len) stop = len - 1;

    *count = 0;
    if (start > stop || start >= len) return NULL;

    size_t n = (size_t)(stop - start + 1);
    char **result = imdb_malloc(n * sizeof(char *));

    list_node_t *node = list->head;
    for (int i = 0; i < start; i++) node = node->next;

    for (size_t i = 0; i < n; i++) {
        result[i] = node->value; /* borrowed pointer, not duplicated */
        node = node->next;
    }
    *count = n;
    return result;
}
