#include "resp.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- Buffer helpers ---- */

void resp_buf_init(resp_buf_t *rb) {
    rb->cap = 256;
    rb->buf = imdb_malloc(rb->cap);
    rb->len = 0;
}

void resp_buf_free(resp_buf_t *rb) {
    imdb_free(rb->buf);
    rb->buf = NULL;
    rb->len = rb->cap = 0;
}

static void resp_buf_ensure(resp_buf_t *rb, size_t extra) {
    while (rb->len + extra > rb->cap) {
        rb->cap *= 2;
        rb->buf = imdb_realloc(rb->buf, rb->cap);
    }
}

static void resp_buf_append(resp_buf_t *rb, const char *data, size_t len) {
    resp_buf_ensure(rb, len);
    memcpy(rb->buf + rb->len, data, len);
    rb->len += len;
}

static void resp_buf_appendf(resp_buf_t *rb, const char *fmt, ...) {
    char tmp[128];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (n > 0) resp_buf_append(rb, tmp, (size_t)n);
}

/* ---- Serialization ---- */

void resp_write_simple_string(resp_buf_t *rb, const char *str) {
    resp_buf_appendf(rb, "+%s\r\n", str);
}

void resp_write_error(resp_buf_t *rb, const char *str) {
    resp_buf_appendf(rb, "-%s\r\n", str);
}

void resp_write_integer(resp_buf_t *rb, int64_t num) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), ":%lld\r\n", (long long)num);
    resp_buf_append(rb, tmp, strlen(tmp));
}

void resp_write_bulk_string(resp_buf_t *rb, const char *str, size_t len) {
    char header[32];
    snprintf(header, sizeof(header), "$%zu\r\n", len);
    resp_buf_append(rb, header, strlen(header));
    resp_buf_append(rb, str, len);
    resp_buf_append(rb, "\r\n", 2);
}

void resp_write_nil(resp_buf_t *rb) {
    resp_buf_append(rb, "$-1\r\n", 5);
}

void resp_write_array_header(resp_buf_t *rb, size_t count) {
    char header[32];
    snprintf(header, sizeof(header), "*%zu\r\n", count);
    resp_buf_append(rb, header, strlen(header));
}

/* ---- Parsing ---- */

static const char *find_crlf(const char *buf, size_t len) {
    for (size_t i = 0; i + 1 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') return buf + i;
    }
    return NULL;
}

/* Forward declaration for recursive parsing */
static int resp_parse_value(const char *buf, size_t len, resp_value_t **out);

static int resp_parse_value(const char *buf, size_t len, resp_value_t **out) {
    if (len == 0) return 0;

    const char *crlf = find_crlf(buf, len);
    if (!crlf) return 0; /* incomplete */

    size_t line_len = (size_t)(crlf - buf);
    size_t consumed = line_len + 2; /* +2 for \r\n */

    switch (buf[0]) {
    case '+': { /* Simple String */
        resp_value_t *v = imdb_malloc(sizeof(resp_value_t));
        v->type = RESP_SIMPLE_STRING;
        v->data.str = imdb_strndup(buf + 1, line_len - 1);
        *out = v;
        return (int)consumed;
    }
    case '-': { /* Error */
        resp_value_t *v = imdb_malloc(sizeof(resp_value_t));
        v->type = RESP_ERROR;
        v->data.str = imdb_strndup(buf + 1, line_len - 1);
        *out = v;
        return (int)consumed;
    }
    case ':': { /* Integer */
        resp_value_t *v = imdb_malloc(sizeof(resp_value_t));
        v->type = RESP_INTEGER;
        v->data.num = strtoll(buf + 1, NULL, 10);
        *out = v;
        return (int)consumed;
    }
    case '$': { /* Bulk String */
        int bulk_len = atoi(buf + 1);
        if (bulk_len == -1) {
            resp_value_t *v = imdb_malloc(sizeof(resp_value_t));
            v->type = RESP_NIL;
            v->data.str = NULL;
            *out = v;
            return (int)consumed;
        }
        if (bulk_len < 0) return -1;

        size_t need = consumed + (size_t)bulk_len + 2;
        if (need > len) return 0; /* incomplete */

        resp_value_t *v = imdb_malloc(sizeof(resp_value_t));
        v->type = RESP_BULK_STRING;
        v->data.str = imdb_strndup(buf + consumed, (size_t)bulk_len);
        *out = v;
        return (int)(consumed + (size_t)bulk_len + 2);
    }
    case '*': { /* Array */
        int arr_count = atoi(buf + 1);
        if (arr_count == -1) {
            resp_value_t *v = imdb_malloc(sizeof(resp_value_t));
            v->type = RESP_NIL;
            v->data.str = NULL;
            *out = v;
            return (int)consumed;
        }
        if (arr_count < 0) return -1;

        resp_value_t *v = imdb_malloc(sizeof(resp_value_t));
        v->type = RESP_ARRAY;
        v->data.array.count = (size_t)arr_count;
        v->data.array.items = imdb_calloc((size_t)arr_count, sizeof(resp_value_t *));

        size_t total = consumed;
        for (int i = 0; i < arr_count; i++) {
            int n = resp_parse_value(buf + total, len - total, &v->data.array.items[i]);
            if (n <= 0) {
                /* Incomplete or error — free what we have */
                for (int j = 0; j < i; j++) resp_free(v->data.array.items[j]);
                imdb_free(v->data.array.items);
                imdb_free(v);
                return n;
            }
            total += (size_t)n;
        }
        *out = v;
        return (int)total;
    }
    default:
        return -1; /* unknown type */
    }
}

int resp_parse(const char *buf, size_t len, resp_value_t **out) {
    return resp_parse_value(buf, len, out);
}

void resp_free(resp_value_t *val) {
    if (!val) return;
    switch (val->type) {
    case RESP_SIMPLE_STRING:
    case RESP_ERROR:
    case RESP_BULK_STRING:
        imdb_free(val->data.str);
        break;
    case RESP_ARRAY:
        for (size_t i = 0; i < val->data.array.count; i++) {
            resp_free(val->data.array.items[i]);
        }
        imdb_free(val->data.array.items);
        break;
    case RESP_NIL:
    case RESP_INTEGER:
        break;
    }
    imdb_free(val);
}
