#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

void *imdb_malloc(size_t size) {
    void *p = malloc(size);
    if (!p && size) {
        fprintf(stderr, "Fatal: out of memory allocating %zu bytes\n", size);
        abort();
    }
    return p;
}

void *imdb_calloc(size_t count, size_t size) {
    void *p = calloc(count, size);
    if (!p && count && size) {
        fprintf(stderr, "Fatal: out of memory allocating %zu bytes\n", count * size);
        abort();
    }
    return p;
}

void *imdb_realloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p && size) {
        fprintf(stderr, "Fatal: out of memory reallocating %zu bytes\n", size);
        abort();
    }
    return p;
}

void imdb_free(void *ptr) {
    free(ptr);
}

char *imdb_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *d = imdb_malloc(len + 1);
    memcpy(d, s, len + 1);
    return d;
}

char *imdb_strndup(const char *s, size_t n) {
    if (!s) return NULL;
    size_t len = strlen(s);
    if (n < len) len = n;
    char *d = imdb_malloc(len + 1);
    memcpy(d, s, len);
    d[len] = '\0';
    return d;
}

int64_t imdb_mstime(void) {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    /* Convert from 100-ns intervals since 1601 to ms since epoch */
    return (int64_t)((t / 10000) - 11644473600000ULL);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

int imdb_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}
