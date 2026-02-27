#ifndef RESP_H
#define RESP_H

#include <stddef.h>
#include <stdint.h>

/* RESP data types */
typedef enum {
    RESP_SIMPLE_STRING, /* +OK\r\n */
    RESP_ERROR,         /* -ERR ...\r\n */
    RESP_INTEGER,       /* :123\r\n */
    RESP_BULK_STRING,   /* $5\r\nhello\r\n */
    RESP_ARRAY,         /* *2\r\n... */
    RESP_NIL            /* $-1\r\n */
} resp_type_t;

typedef struct resp_value {
    resp_type_t type;
    union {
        char *str;          /* simple string, error, bulk string */
        int64_t num;        /* integer */
        struct {
            struct resp_value **items;
            size_t count;
        } array;
    } data;
} resp_value_t;

/* Parse a RESP message from buffer. Returns bytes consumed, 0 if incomplete, -1 on error. */
int resp_parse(const char *buf, size_t len, resp_value_t **out);

/* Free a parsed RESP value */
void resp_free(resp_value_t *val);

/* Serialization — writes to dynamically allocated buffer */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} resp_buf_t;

void resp_buf_init(resp_buf_t *rb);
void resp_buf_free(resp_buf_t *rb);

void resp_write_simple_string(resp_buf_t *rb, const char *str);
void resp_write_error(resp_buf_t *rb, const char *str);
void resp_write_integer(resp_buf_t *rb, int64_t num);
void resp_write_bulk_string(resp_buf_t *rb, const char *str, size_t len);
void resp_write_nil(resp_buf_t *rb);
void resp_write_array_header(resp_buf_t *rb, size_t count);

#endif /* RESP_H */
