#include "server.h"
#include "command.h"
#include "resp.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#define close_socket closesocket
#define sock_errno WSAGetLastError()
#else
#define close_socket close
#define sock_errno errno
#endif

static void set_nonblocking(socket_t fd) {
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

static client_t *client_create(socket_t fd) {
    client_t *c = imdb_calloc(1, sizeof(client_t));
    c->fd = fd;
    c->write_cap = 1024;
    c->write_buf = imdb_malloc(c->write_cap);
    c->write_len = 0;
    c->write_pos = 0;
    return c;
}

static void client_destroy(client_t *c) {
    if (!c) return;
    close_socket(c->fd);
    imdb_free(c->write_buf);
    imdb_free(c);
}

static void client_queue_write(client_t *c, const char *data, size_t len) {
    while (c->write_len + len > c->write_cap) {
        c->write_cap *= 2;
        c->write_buf = imdb_realloc(c->write_buf, c->write_cap);
    }
    memcpy(c->write_buf + c->write_len, data, len);
    c->write_len += len;
}

server_t *server_create(database_t *db, int port) {
    server_t *srv = imdb_calloc(1, sizeof(server_t));
    srv->db = db;
    srv->port = port;
    srv->running = 0;
    srv->listen_fd = INVALID_SOCK;
    return srv;
}

static int server_listen(server_t *srv) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)srv->port);

    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd == INVALID_SOCK) {
        fprintf(stderr, "Error: cannot create socket\n");
        return -1;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#else
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Error: cannot bind to port %d\n", srv->port);
        close_socket(srv->listen_fd);
        return -1;
    }

    if (listen(srv->listen_fd, 128) < 0) {
        fprintf(stderr, "Error: listen failed\n");
        close_socket(srv->listen_fd);
        return -1;
    }

    set_nonblocking(srv->listen_fd);
    return 0;
}

static void server_accept(server_t *srv) {
    struct sockaddr_in client_addr;
    int addrlen = sizeof(client_addr);
    socket_t client_fd;

#ifdef _WIN32
    client_fd = accept(srv->listen_fd, (struct sockaddr *)&client_addr, &addrlen);
#else
    client_fd = accept(srv->listen_fd, (struct sockaddr *)&client_addr, (socklen_t *)&addrlen);
#endif

    if (client_fd == INVALID_SOCK) return;

    if (srv->client_count >= MAX_CLIENTS) {
        close_socket(client_fd);
        return;
    }

    set_nonblocking(client_fd);
    client_t *c = client_create(client_fd);

    /* Find empty slot */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!srv->clients[i]) {
            srv->clients[i] = c;
            srv->client_count++;
            return;
        }
    }

    client_destroy(c);
}

static void server_remove_client(server_t *srv, int index) {
    client_destroy(srv->clients[index]);
    srv->clients[index] = NULL;
    srv->client_count--;
}

static void process_client_input(server_t *srv, client_t *c) {
    while (c->read_len > 0) {
        resp_value_t *cmd = NULL;
        int consumed = resp_parse(c->read_buf, c->read_len, &cmd);

        if (consumed <= 0) break; /* incomplete or error */

        /* Execute command */
        resp_buf_t reply;
        resp_buf_init(&reply);
        command_execute(srv->db, srv, cmd, &reply);
        resp_free(cmd);

        /* Queue reply */
        client_queue_write(c, reply.buf, reply.len);
        resp_buf_free(&reply);

        /* Consume from read buffer */
        memmove(c->read_buf, c->read_buf + consumed, c->read_len - consumed);
        c->read_len -= consumed;
    }
}

void server_run(server_t *srv) {
    if (server_listen(srv) < 0) return;

    srv->running = 1;
    printf("inMemDb server listening on port %d\n", srv->port);

    while (srv->running) {
        fd_set read_fds, write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);

        FD_SET(srv->listen_fd, &read_fds);
        socket_t max_fd = srv->listen_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            client_t *c = srv->clients[i];
            if (!c) continue;
            FD_SET(c->fd, &read_fds);
            if (c->write_len > c->write_pos) {
                FD_SET(c->fd, &write_fds);
            }
            if (c->fd > max_fd) max_fd = c->fd;
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000; /* 50ms timeout for expiry sweeps */

        int ready = select((int)(max_fd + 1), &read_fds, &write_fds, NULL, &tv);
        if (ready < 0) {
            if (sock_errno == EINTR) continue;
            break;
        }

        /* Accept new connections */
        if (FD_ISSET(srv->listen_fd, &read_fds)) {
            server_accept(srv);
        }

        /* Process client I/O */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            client_t *c = srv->clients[i];
            if (!c) continue;

            /* Read */
            if (FD_ISSET(c->fd, &read_fds)) {
                int n = recv(c->fd, c->read_buf + c->read_len,
                           (int)(CLIENT_BUF_SIZE - c->read_len), 0);
                if (n <= 0) {
                    server_remove_client(srv, i);
                    continue;
                }
                c->read_len += n;
                process_client_input(srv, c);
            }

            /* Write */
            if (c && FD_ISSET(c->fd, &write_fds)) {
                size_t to_write = c->write_len - c->write_pos;
                int n = send(c->fd, c->write_buf + c->write_pos, (int)to_write, 0);
                if (n <= 0) {
                    server_remove_client(srv, i);
                    continue;
                }
                c->write_pos += n;
                if (c->write_pos >= c->write_len) {
                    c->write_pos = 0;
                    c->write_len = 0;
                }
            }
        }

        /* Periodic expiry sweep */
        db_expire_sweep(srv->db);
    }

    /* Cleanup */
    close_socket(srv->listen_fd);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (srv->clients[i]) server_remove_client(srv, i);
    }
}

void server_stop(server_t *srv) {
    srv->running = 0;
}

void server_destroy(server_t *srv) {
    if (!srv) return;
    imdb_free(srv);
}
