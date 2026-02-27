#ifndef SERVER_H
#define SERVER_H

#include "db.h"
#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
typedef int socket_t;
#define INVALID_SOCK (-1)
#endif

#define MAX_CLIENTS    1024
#define CLIENT_BUF_SIZE 65536
#define DEFAULT_PORT   6399

typedef struct {
    socket_t fd;
    char read_buf[CLIENT_BUF_SIZE];
    size_t read_len;
    char *write_buf;
    size_t write_len;
    size_t write_cap;
    size_t write_pos;
} client_t;

typedef struct server {
    database_t *db;
    socket_t listen_fd;
    int port;
    int running;
    client_t *clients[MAX_CLIENTS];
    int client_count;
} server_t;

/* Create, run, and stop the server */
server_t *server_create(database_t *db, int port);
void server_run(server_t *srv);
void server_stop(server_t *srv);
void server_destroy(server_t *srv);

#endif /* SERVER_H */
