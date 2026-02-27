#include "db.h"
#include "server.h"
#include "persist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
#include <winsock2.h>
#endif

static server_t *g_server = NULL;

#ifdef _WIN32
static BOOL WINAPI console_handler(DWORD sig) {
    (void)sig;
    if (g_server) {
        printf("\nShutting down...\n");
        persist_save(g_server->db, "dump.rdb");
        server_stop(g_server);
    }
    return TRUE;
}
#else
static void signal_handler(int sig) {
    (void)sig;
    if (g_server) {
        printf("\nShutting down...\n");
        persist_save(g_server->db, "dump.rdb");
        server_stop(g_server);
    }
}
#endif

static void print_banner(int port) {
    printf("\n");
    printf("  _       __  __                 ____  _     \n");
    printf(" (_)_ __ |  \\/  | ___ _ __ ___  |  _ \\| |__  \n");
    printf(" | | '_ \\| |\\/| |/ _ \\ '_ ` _ \\ | | | | '_ \\ \n");
    printf(" | | | | | |  | |  __/ | | | | || |_| | |_) |\n");
    printf(" |_|_| |_|_|  |_|\\___|_| |_| |_||____/|_.__/ \n");
    printf("\n");
    printf("  Version 1.0.0 | Port %d\n", port);
    printf("  Type 'SHUTDOWN' from a client to stop.\n\n");
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--port") == 0 || strcmp(argv[i], "-p") == 0) && i + 1 < argc) {
            port = atoi(argv[++i]);
        }
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "Error: WSAStartup failed\n");
        return 1;
    }
    SetConsoleCtrlHandler(console_handler, TRUE);
#else
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif

    database_t *db = db_create();

    /* Load existing data if dump file exists */
    persist_load(db, "dump.rdb");

    server_t *srv = server_create(db, port);
    g_server = srv;

    print_banner(port);
    server_run(srv);

    server_destroy(srv);
    db_destroy(db);

#ifdef _WIN32
    WSACleanup();
#endif

    printf("Goodbye.\n");
    return 0;
}
