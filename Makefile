CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
SRC_DIR = src
CLI_DIR = cli
BUILD_DIR = build

# Platform detection
ifeq ($(OS),Windows_NT)
    LDFLAGS = -lws2_32
    SERVER_BIN = $(BUILD_DIR)/inmemdb-server.exe
    CLI_BIN = $(BUILD_DIR)/inmemdb-cli.exe
    MKDIR = if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
    RM = del /Q
    RMDIR = if exist $(BUILD_DIR) rmdir /S /Q $(BUILD_DIR)
else
    LDFLAGS =
    SERVER_BIN = $(BUILD_DIR)/inmemdb-server
    CLI_BIN = $(BUILD_DIR)/inmemdb-cli
    MKDIR = mkdir -p $(BUILD_DIR)
    RM = rm -f
    RMDIR = rm -rf $(BUILD_DIR)
endif

SERVER_SRCS = $(SRC_DIR)/main.c \
              $(SRC_DIR)/server.c \
              $(SRC_DIR)/command.c \
              $(SRC_DIR)/db.c \
              $(SRC_DIR)/hashtable.c \
              $(SRC_DIR)/list.c \
              $(SRC_DIR)/object.c \
              $(SRC_DIR)/resp.c \
              $(SRC_DIR)/persist.c \
              $(SRC_DIR)/util.c

CLI_SRCS = $(CLI_DIR)/cli.c

.PHONY: all server cli clean

all: server cli

server: $(SERVER_BIN)

cli: $(CLI_BIN)

$(SERVER_BIN): $(SERVER_SRCS)
	$(MKDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CLI_BIN): $(CLI_SRCS)
	$(MKDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	$(RMDIR)
