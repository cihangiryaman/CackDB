/*
 * inMemDb CLI Client
 * Connects to the inMemDb server via TCP and provides a REPL interface.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define close_socket closesocket
#define INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#define close_socket close
#define INVALID_SOCK (-1)
#endif

#define BUF_SIZE 65536

#ifdef _WIN32
static char *my_strndup(const char *s, size_t n) {
    size_t len = strlen(s);
    if (n < len) len = n;
    char *d = malloc(len + 1);
    if (d) { memcpy(d, s, len); d[len] = '\0'; }
    return d;
}
#define strndup my_strndup
#endif

/* ---- Minimal RESP parser for display ---- */

typedef enum {
    R_STRING, R_ERROR, R_INTEGER, R_BULK, R_ARRAY, R_NIL
} rtype_t;

typedef struct rval {
    rtype_t type;
    char *str;
    int64_t num;
    struct rval **items;
    size_t count;
} rval_t;

static const char *find_crlf(const char *buf, size_t len) {
    for (size_t i = 0; i + 1 < len; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n') return buf + i;
    return NULL;
}

static int parse_resp(const char *buf, size_t len, rval_t **out) {
    if (len == 0) return 0;
    const char *crlf = find_crlf(buf, len);
    if (!crlf) return 0;
    size_t line_len = (size_t)(crlf - buf);
    size_t consumed = line_len + 2;

    switch (buf[0]) {
    case '+': {
        rval_t *v = calloc(1, sizeof(rval_t));
        v->type = R_STRING;
        v->str = strndup(buf + 1, line_len - 1);
        *out = v;
        return (int)consumed;
    }
    case '-': {
        rval_t *v = calloc(1, sizeof(rval_t));
        v->type = R_ERROR;
        v->str = strndup(buf + 1, line_len - 1);
        *out = v;
        return (int)consumed;
    }
    case ':': {
        rval_t *v = calloc(1, sizeof(rval_t));
        v->type = R_INTEGER;
        v->num = strtoll(buf + 1, NULL, 10);
        *out = v;
        return (int)consumed;
    }
    case '$': {
        int blen = atoi(buf + 1);
        if (blen == -1) {
            rval_t *v = calloc(1, sizeof(rval_t));
            v->type = R_NIL;
            *out = v;
            return (int)consumed;
        }
        size_t need = consumed + (size_t)blen + 2;
        if (need > len) return 0;
        rval_t *v = calloc(1, sizeof(rval_t));
        v->type = R_BULK;
        v->str = malloc((size_t)blen + 1);
        memcpy(v->str, buf + consumed, (size_t)blen);
        v->str[blen] = '\0';
        *out = v;
        return (int)(consumed + (size_t)blen + 2);
    }
    case '*': {
        int cnt = atoi(buf + 1);
        if (cnt == -1) {
            rval_t *v = calloc(1, sizeof(rval_t));
            v->type = R_NIL;
            *out = v;
            return (int)consumed;
        }
        rval_t *v = calloc(1, sizeof(rval_t));
        v->type = R_ARRAY;
        v->count = (size_t)cnt;
        v->items = calloc((size_t)cnt, sizeof(rval_t *));
        size_t total = consumed;
        for (int i = 0; i < cnt; i++) {
            int n = parse_resp(buf + total, len - total, &v->items[i]);
            if (n <= 0) {
                for (int j = 0; j < i; j++) { free(v->items[j]->str); free(v->items[j]); }
                free(v->items); free(v);
                return n;
            }
            total += (size_t)n;
        }
        *out = v;
        return (int)total;
    }
    default: return -1;
    }
}

static void print_resp(rval_t *v, int indent) {
    switch (v->type) {
    case R_STRING: printf("%s\n", v->str); break;
    case R_ERROR:  printf("(error) %s\n", v->str); break;
    case R_INTEGER: printf("(integer) %lld\n", (long long)v->num); break;
    case R_BULK:   printf("\"%s\"\n", v->str); break;
    case R_NIL:    printf("(nil)\n"); break;
    case R_ARRAY:
        if (v->count == 0) { printf("(empty array)\n"); break; }
        for (size_t i = 0; i < v->count; i++) {
            printf("%*s%zu) ", indent, "", i + 1);
            print_resp(v->items[i], indent + 3);
        }
        break;
    }
}

static void free_resp(rval_t *v) {
    if (!v) return;
    if (v->str) free(v->str);
    if (v->items) {
        for (size_t i = 0; i < v->count; i++) free_resp(v->items[i]);
        free(v->items);
    }
    free(v);
}

/* ---- Build RESP command from input line ---- */

static int build_command(const char *line, char *out, size_t out_cap) {
    /* Tokenize by spaces, respecting double quotes */
    const char *args[128];
    size_t lens[128];
    int argc = 0;

    const char *p = line;
    while (*p && argc < 128) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (*p == '"') {
            p++;
            const char *start = p;
            while (*p && *p != '"') p++;
            args[argc] = start;
            lens[argc] = (size_t)(p - start);
            if (*p == '"') p++;
        } else {
            const char *start = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            args[argc] = start;
            lens[argc] = (size_t)(p - start);
        }
        argc++;
    }

    if (argc == 0) return 0;

    /* Build RESP array */
    int offset = snprintf(out, out_cap, "*%d\r\n", argc);
    for (int i = 0; i < argc; i++) {
        offset += snprintf(out + offset, out_cap - offset, "$%zu\r\n", lens[i]);
        memcpy(out + offset, args[i], lens[i]);
        offset += (int)lens[i];
        out[offset++] = '\r';
        out[offset++] = '\n';
    }
    return offset;
}

/* ---- Receive full RESP response ---- */

static int recv_response(socket_t sock, char *buf, size_t cap) {
    size_t total = 0;
    while (total < cap) {
        int n = recv(sock, buf + total, (int)(cap - total), 0);
        if (n <= 0) return -1;
        total += (size_t)n;

        /* Try to parse — if complete, we're done */
        rval_t *tmp = NULL;
        int parsed = parse_resp(buf, total, &tmp);
        if (parsed > 0) {
            free_resp(tmp);
            return (int)total;
        }
    }
    return (int)total;
}

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    int port = 6399;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--host") == 0) && i + 1 < argc)
            host = argv[++i];
        else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc)
            port = atoi(argv[++i]);
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCK) {
        fprintf(stderr, "Cannot create socket\n");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Cannot connect to %s:%d\n", host, port);
        close_socket(sock);
        return 1;
    }

    printf("Connected to %s:%d\n", host, port);
    printf("Type commands (e.g., SET key value, GET key). Ctrl+C to quit.\n\n");

    char line[4096];
    char cmd_buf[BUF_SIZE];
    char recv_buf[BUF_SIZE];

    while (1) {
        printf("inmemdb> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        /* Trim newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;

        /* Build RESP command */
        int cmd_len = build_command(line, cmd_buf, sizeof(cmd_buf));
        if (cmd_len <= 0) continue;

        /* Send */
        if (send(sock, cmd_buf, cmd_len, 0) <= 0) {
            fprintf(stderr, "Connection lost\n");
            break;
        }

        /* Receive */
        int n = recv_response(sock, recv_buf, sizeof(recv_buf));
        if (n <= 0) {
            fprintf(stderr, "Connection lost\n");
            break;
        }

        /* Parse and display */
        rval_t *resp = NULL;
        if (parse_resp(recv_buf, (size_t)n, &resp) > 0 && resp) {
            print_resp(resp, 0);
            free_resp(resp);
        }

        /* Check for quit/shutdown */
        if (strncmp(line, "SHUTDOWN", 8) == 0 || strncmp(line, "shutdown", 8) == 0)
            break;
    }

    close_socket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
