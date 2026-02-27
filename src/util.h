#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Safe memory allocation — aborts on failure */
void *imdb_malloc(size_t size);
void *imdb_calloc(size_t count, size_t size);
void *imdb_realloc(void *ptr, size_t size);
void imdb_free(void *ptr);

/* String duplication */
char *imdb_strdup(const char *s);
char *imdb_strndup(const char *s, size_t n);

/* Time utilities (milliseconds since epoch) */
int64_t imdb_mstime(void);

/* Case-insensitive string compare */
int imdb_strcasecmp(const char *a, const char *b);

#endif /* UTIL_H */
