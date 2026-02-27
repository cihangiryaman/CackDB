# inMemDb — In-Memory Key-Value Database

A Redis-like in-memory key-value database written in C, featuring a TCP server with RESP2 protocol compatibility, TTL-based key expiration, and RDB-style persistence.

## Features

- **Key-Value Store** — Open-addressing hash table with Robin Hood hashing
- **Data Types** — Strings, integers, and lists
- **TTL Expiration** — Per-key time-to-live with lazy + periodic sweep
- **RESP2 Protocol** — Compatible with `redis-cli` and other Redis clients
- **Persistence** — RDB-style binary snapshots (SAVE/BGSAVE)
- **Cross-Platform** — Works on Windows and Linux

## Building

### Prerequisites
- GCC (MinGW on Windows, or any C11-compatible compiler)
- Make

### Compile
```bash
make          # Build both server and CLI
make server   # Build server only
make cli      # Build CLI only
make clean    # Remove build artifacts
```

### Manual compilation (Windows)
```cmd
mkdir build
gcc -Wall -Wextra -O2 -std=c11 -o build/inmemdb-server.exe src/main.c src/server.c src/command.c src/db.c src/hashtable.c src/list.c src/object.c src/resp.c src/persist.c src/util.c -lws2_32
gcc -Wall -Wextra -O2 -std=c11 -o build/inmemdb-cli.exe cli/cli.c -lws2_32
```

## Usage

### Start the server
```bash
./build/inmemdb-server              # Default port 6399
./build/inmemdb-server --port 7000  # Custom port
```

### Connect with the CLI
```bash
./build/inmemdb-cli                          # Default localhost:6399
./build/inmemdb-cli --host 192.168.1.5 -p 7000  # Custom host/port
```

### Or use redis-cli (RESP2 compatible)
```bash
redis-cli -p 6399
```

## Supported Commands

### String / Integer
| Command | Description | Example |
|---------|-------------|---------|
| `SET key value [EX seconds]` | Set a key | `SET name "Alice" EX 60` |
| `GET key` | Get value | `GET name` |
| `DEL key [key ...]` | Delete key(s) | `DEL name age` |
| `EXISTS key` | Check existence | `EXISTS name` |
| `INCR key` | Increment by 1 | `INCR counter` |
| `DECR key` | Decrement by 1 | `DECR counter` |
| `MSET key val [key val ...]` | Set multiple | `MSET a 1 b 2 c 3` |
| `MGET key [key ...]` | Get multiple | `MGET a b c` |

### List
| Command | Description | Example |
|---------|-------------|---------|
| `LPUSH key val [val ...]` | Prepend to list | `LPUSH mylist a b c` |
| `RPUSH key val [val ...]` | Append to list | `RPUSH mylist x y z` |
| `LPOP key` | Pop from head | `LPOP mylist` |
| `RPOP key` | Pop from tail | `RPOP mylist` |
| `LLEN key` | List length | `LLEN mylist` |
| `LRANGE key start stop` | Get range | `LRANGE mylist 0 -1` |

### TTL
| Command | Description | Example |
|---------|-------------|---------|
| `EXPIRE key seconds` | Set timeout | `EXPIRE session 300` |
| `TTL key` | Get remaining TTL | `TTL session` |
| `PERSIST key` | Remove timeout | `PERSIST session` |

### Server
| Command | Description |
|---------|-------------|
| `PING` | Health check (returns PONG) |
| `INFO` | Server information |
| `DBSIZE` | Number of keys |
| `FLUSHDB` | Delete all keys |
| `SAVE` | Snapshot to disk |
| `SHUTDOWN` | Save and exit |

## Architecture

```
Client (CLI/redis-cli) ──TCP/RESP──► Server (select loop)
                                        │
                                   Command Processor
                                        │
                                   Storage Engine
                                   (Hash Table + TTL)
                                        │
                                   Persistence (RDB)
```

## Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `--port` / `-p` | 6399 | TCP listen port |

## File Format

The `dump.rdb` file uses a simple binary format:
- 8-byte magic header (`IMDB0001`)
- Key-value entries with type, TTL, key, and value data
- EOF marker (`0xFF`)

## License

MIT
