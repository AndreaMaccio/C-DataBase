CC = gcc
CFLAGS = -Wall -Wextra -pthread -Iinclude
LDFLAGS = -pthread

SRCDIR = src
INCDIR = include
BINDIR = bin

SERVER_SRCS = $(SRCDIR)/main.c $(SRCDIR)/server.c $(SRCDIR)/storage.c $(SRCDIR)/hashmap.c $(SRCDIR)/signalhandling.c
SERVER_OBJS = $(SERVER_SRCS:$(SRCDIR)/%.c=$(BINDIR)/%.o)
SERVER_EXEC = $(BINDIR)/server

CLI_SRCS = $(SRCDIR)/cli.c
CLI_OBJS = $(CLI_SRCS:$(SRCDIR)/%.c=$(BINDIR)/%.o)
CLI_EXEC = $(BINDIR)/cli

.PHONY: all clean directories

all: directories $(SERVER_EXEC) $(CLI_EXEC)

directories:
	@mkdir -p $(BINDIR)
	@mkdir -p data

$(SERVER_EXEC): $(SERVER_OBJS)
	$(CC) $(SERVER_OBJS) -o $@ $(LDFLAGS)

$(CLI_EXEC): $(CLI_OBJS)
	$(CC) $(CLI_OBJS) -o $@ $(LDFLAGS)

$(BINDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BINDIR)
