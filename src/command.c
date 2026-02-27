#include "command.h"
#include "server.h"
#include "persist.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

/* Helper to get string from RESP value */
static const char *get_arg(resp_value_t *cmd, int index) {
    if (!cmd || cmd->type != RESP_ARRAY) return NULL;
    if ((size_t)index >= cmd->data.array.count) return NULL;
    resp_value_t *item = cmd->data.array.items[index];
    if (item->type == RESP_BULK_STRING || item->type == RESP_SIMPLE_STRING)
        return item->data.str;
    return NULL;
}

static size_t arg_count(resp_value_t *cmd) {
    if (!cmd || cmd->type != RESP_ARRAY) return 0;
    return cmd->data.array.count;
}

/* ---- Command handlers ---- */

static void cmd_ping(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)db; (void)srv;
    if (arg_count(cmd) > 1) {
        const char *msg = get_arg(cmd, 1);
        resp_write_bulk_string(reply, msg, strlen(msg));
    } else {
        resp_write_simple_string(reply, "PONG");
    }
}

static void cmd_set(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    if (arg_count(cmd) < 3) {
        resp_write_error(reply, "ERR wrong number of arguments for 'SET' command");
        return;
    }
    const char *key = get_arg(cmd, 1);
    const char *val = get_arg(cmd, 2);
    db_set(db, key, val);

    /* Handle optional EX argument */
    size_t argc = arg_count(cmd);
    for (size_t i = 3; i + 1 < argc; i += 2) {
        const char *opt = get_arg(cmd, (int)i);
        const char *optval = get_arg(cmd, (int)(i + 1));
        if (opt && optval && imdb_strcasecmp(opt, "EX") == 0) {
            int64_t secs = strtoll(optval, NULL, 10);
            if (secs > 0) db_expire(db, key, secs);
        }
    }

    resp_write_simple_string(reply, "OK");
}

static void cmd_get(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    if (arg_count(cmd) < 2) {
        resp_write_error(reply, "ERR wrong number of arguments for 'GET' command");
        return;
    }
    dbobj_t *obj = db_get(db, get_arg(cmd, 1));
    if (!obj) {
        resp_write_nil(reply);
    } else if (obj->type == OBJ_STRING) {
        resp_write_bulk_string(reply, obj->data.str, strlen(obj->data.str));
    } else if (obj->type == OBJ_INT) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%" PRId64, obj->data.num);
        resp_write_bulk_string(reply, buf, strlen(buf));
    } else {
        resp_write_error(reply, "WRONGTYPE Operation against a key holding the wrong kind of value");
    }
}

static void cmd_del(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    size_t argc = arg_count(cmd);
    if (argc < 2) {
        resp_write_error(reply, "ERR wrong number of arguments for 'DEL' command");
        return;
    }
    int64_t deleted = 0;
    for (size_t i = 1; i < argc; i++) {
        deleted += db_del(db, get_arg(cmd, (int)i));
    }
    resp_write_integer(reply, deleted);
}

static void cmd_exists(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    if (arg_count(cmd) < 2) {
        resp_write_error(reply, "ERR wrong number of arguments for 'EXISTS' command");
        return;
    }
    resp_write_integer(reply, db_exists(db, get_arg(cmd, 1)));
}

static void cmd_incr(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    if (arg_count(cmd) < 2) {
        resp_write_error(reply, "ERR wrong number of arguments for 'INCR' command");
        return;
    }
    int64_t val = db_incr(db, get_arg(cmd, 1), 1);
    if (val == INT64_MIN) {
        resp_write_error(reply, "ERR value is not an integer or out of range");
    } else {
        resp_write_integer(reply, val);
    }
}

static void cmd_decr(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    if (arg_count(cmd) < 2) {
        resp_write_error(reply, "ERR wrong number of arguments for 'DECR' command");
        return;
    }
    int64_t val = db_incr(db, get_arg(cmd, 1), -1);
    if (val == INT64_MIN) {
        resp_write_error(reply, "ERR value is not an integer or out of range");
    } else {
        resp_write_integer(reply, val);
    }
}

static void cmd_mset(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    size_t argc = arg_count(cmd);
    if (argc < 3 || (argc - 1) % 2 != 0) {
        resp_write_error(reply, "ERR wrong number of arguments for 'MSET' command");
        return;
    }
    for (size_t i = 1; i + 1 < argc; i += 2) {
        db_set(db, get_arg(cmd, (int)i), get_arg(cmd, (int)(i + 1)));
    }
    resp_write_simple_string(reply, "OK");
}

static void cmd_mget(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    size_t argc = arg_count(cmd);
    if (argc < 2) {
        resp_write_error(reply, "ERR wrong number of arguments for 'MGET' command");
        return;
    }
    resp_write_array_header(reply, argc - 1);
    for (size_t i = 1; i < argc; i++) {
        dbobj_t *obj = db_get(db, get_arg(cmd, (int)i));
        if (!obj) {
            resp_write_nil(reply);
        } else if (obj->type == OBJ_STRING) {
            resp_write_bulk_string(reply, obj->data.str, strlen(obj->data.str));
        } else if (obj->type == OBJ_INT) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%" PRId64, obj->data.num);
            resp_write_bulk_string(reply, buf, strlen(buf));
        } else {
            resp_write_nil(reply);
        }
    }
}

/* ---- List commands ---- */

static void cmd_lpush(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    size_t argc = arg_count(cmd);
    if (argc < 3) {
        resp_write_error(reply, "ERR wrong number of arguments for 'LPUSH' command");
        return;
    }
    int result = 0;
    for (size_t i = 2; i < argc; i++) {
        result = db_lpush(db, get_arg(cmd, 1), get_arg(cmd, (int)i));
        if (result < 0) {
            resp_write_error(reply, "WRONGTYPE Operation against a key holding the wrong kind of value");
            return;
        }
    }
    resp_write_integer(reply, result);
}

static void cmd_rpush(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    size_t argc = arg_count(cmd);
    if (argc < 3) {
        resp_write_error(reply, "ERR wrong number of arguments for 'RPUSH' command");
        return;
    }
    int result = 0;
    for (size_t i = 2; i < argc; i++) {
        result = db_rpush(db, get_arg(cmd, 1), get_arg(cmd, (int)i));
        if (result < 0) {
            resp_write_error(reply, "WRONGTYPE Operation against a key holding the wrong kind of value");
            return;
        }
    }
    resp_write_integer(reply, result);
}

static void cmd_lpop(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    if (arg_count(cmd) < 2) {
        resp_write_error(reply, "ERR wrong number of arguments for 'LPOP' command");
        return;
    }
    char *val = db_lpop(db, get_arg(cmd, 1));
    if (!val) {
        resp_write_nil(reply);
    } else {
        resp_write_bulk_string(reply, val, strlen(val));
        imdb_free(val);
    }
}

static void cmd_rpop(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    if (arg_count(cmd) < 2) {
        resp_write_error(reply, "ERR wrong number of arguments for 'RPOP' command");
        return;
    }
    char *val = db_rpop(db, get_arg(cmd, 1));
    if (!val) {
        resp_write_nil(reply);
    } else {
        resp_write_bulk_string(reply, val, strlen(val));
        imdb_free(val);
    }
}

static void cmd_llen(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    if (arg_count(cmd) < 2) {
        resp_write_error(reply, "ERR wrong number of arguments for 'LLEN' command");
        return;
    }
    int64_t len = db_llen(db, get_arg(cmd, 1));
    if (len < 0) {
        resp_write_error(reply, "WRONGTYPE Operation against a key holding the wrong kind of value");
    } else {
        resp_write_integer(reply, len);
    }
}

static void cmd_lrange(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    if (arg_count(cmd) < 4) {
        resp_write_error(reply, "ERR wrong number of arguments for 'LRANGE' command");
        return;
    }
    int start = atoi(get_arg(cmd, 2));
    int stop = atoi(get_arg(cmd, 3));
    size_t count = 0;
    char **items = db_lrange(db, get_arg(cmd, 1), start, stop, &count);

    resp_write_array_header(reply, count);
    for (size_t i = 0; i < count; i++) {
        resp_write_bulk_string(reply, items[i], strlen(items[i]));
    }
    if (items) imdb_free(items);
}

/* ---- TTL commands ---- */

static void cmd_expire(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    if (arg_count(cmd) < 3) {
        resp_write_error(reply, "ERR wrong number of arguments for 'EXPIRE' command");
        return;
    }
    int64_t secs = strtoll(get_arg(cmd, 2), NULL, 10);
    resp_write_integer(reply, db_expire(db, get_arg(cmd, 1), secs));
}

static void cmd_ttl(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    if (arg_count(cmd) < 2) {
        resp_write_error(reply, "ERR wrong number of arguments for 'TTL' command");
        return;
    }
    resp_write_integer(reply, db_ttl(db, get_arg(cmd, 1)));
}

static void cmd_persist(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv;
    if (arg_count(cmd) < 2) {
        resp_write_error(reply, "ERR wrong number of arguments for 'PERSIST' command");
        return;
    }
    resp_write_integer(reply, db_persist(db, get_arg(cmd, 1)));
}

/* ---- Server commands ---- */

static void cmd_dbsize(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv; (void)cmd;
    resp_write_integer(reply, (int64_t)db_size(db));
}

static void cmd_flushdb(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv; (void)cmd;
    db_flush(db);
    resp_write_simple_string(reply, "OK");
}

static void cmd_info(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv; (void)cmd;
    char info[512];
    snprintf(info, sizeof(info),
        "# Server\r\n"
        "inmemdb_version:1.0.0\r\n"
        "# Keyspace\r\n"
        "db0:keys=%zu\r\n",
        db_size(db));
    resp_write_bulk_string(reply, info, strlen(info));
}

static void cmd_save(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)srv; (void)cmd;
    if (persist_save(db, "dump.rdb") == 0) {
        resp_write_simple_string(reply, "OK");
    } else {
        resp_write_error(reply, "ERR failed to save database");
    }
}

static void cmd_shutdown(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    (void)cmd;
    persist_save(db, "dump.rdb");
    resp_write_simple_string(reply, "OK");
    if (srv) server_stop(srv);
}

/* ---- Command dispatch table ---- */

typedef void (*cmd_handler_t)(database_t *, server_t *, resp_value_t *, resp_buf_t *);

typedef struct {
    const char *name;
    cmd_handler_t handler;
} cmd_entry_t;

static cmd_entry_t command_table[] = {
    {"PING",    cmd_ping},
    {"SET",     cmd_set},
    {"GET",     cmd_get},
    {"DEL",     cmd_del},
    {"EXISTS",  cmd_exists},
    {"INCR",    cmd_incr},
    {"DECR",    cmd_decr},
    {"MSET",    cmd_mset},
    {"MGET",    cmd_mget},
    {"LPUSH",   cmd_lpush},
    {"RPUSH",   cmd_rpush},
    {"LPOP",    cmd_lpop},
    {"RPOP",    cmd_rpop},
    {"LLEN",    cmd_llen},
    {"LRANGE",  cmd_lrange},
    {"EXPIRE",  cmd_expire},
    {"TTL",     cmd_ttl},
    {"PERSIST", cmd_persist},
    {"DBSIZE",  cmd_dbsize},
    {"FLUSHDB", cmd_flushdb},
    {"INFO",    cmd_info},
    {"SAVE",    cmd_save},
    {"SHUTDOWN",cmd_shutdown},
    {NULL,      NULL}
};

void command_execute(database_t *db, server_t *srv, resp_value_t *cmd, resp_buf_t *reply) {
    if (!cmd || cmd->type != RESP_ARRAY || cmd->data.array.count == 0) {
        resp_write_error(reply, "ERR invalid command format");
        return;
    }

    const char *name = get_arg(cmd, 0);
    if (!name) {
        resp_write_error(reply, "ERR invalid command");
        return;
    }

    for (cmd_entry_t *e = command_table; e->name; e++) {
        if (imdb_strcasecmp(name, e->name) == 0) {
            e->handler(db, srv, cmd, reply);
            return;
        }
    }

    char err[128];
    snprintf(err, sizeof(err), "ERR unknown command '%s'", name);
    resp_write_error(reply, err);
}
