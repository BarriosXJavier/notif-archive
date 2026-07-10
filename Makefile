# Compiler
CC ?= cc

# Build configuration
CPPFLAGS := $(shell pkg-config --cflags libsystemd)
CFLAGS   := -std=c11 -Wall -Wextra -O2 -MMD -MP
LDFLAGS  :=
LDLIBS   := $(shell pkg-config --libs libsystemd)

# Project
BIN := notif-archiver

SRC := \
	src/main.c \
	src/config.c \
	src/bus_listener.c \
	src/parser.c \
	src/storage.c \
	src/screenshot.c \
	src/log.c

OBJ := $(SRC:.c=.o)
DEP := $(OBJ:.o=.d)

PREFIX ?= $(HOME)

.PHONY: all clean install enable test

all: $(BIN)

# Link
$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

# Compile
%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Tests
test: test_parser
	./test_parser

test_parser: tests/test_parser.c src/parser.c
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		tests/test_parser.c src/parser.c \
		-Isrc \
		-o $@

# Install
install: $(BIN)
	install -Dm755 $(BIN) $(PREFIX)/bin/$(BIN)
	install -Dm644 config/notif-archiver.conf \
		$(HOME)/.config/notif-archiver/notif-archiver.conf
	install -Dm644 systemd/notif-archiver.service \
		$(HOME)/.config/systemd/user/notif-archiver.service

# Enable user service
enable:
	systemctl --user daemon-reload
	systemctl --user enable --now notif-archiver.service

# Cleanup
clean:
	rm -f $(OBJ) $(DEP) $(BIN) test_parser

# Include generated dependency files
-include $(DEP)
