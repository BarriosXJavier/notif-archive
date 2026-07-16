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
TEST_BINS := test_parser test_config test_storage test_screenshot

PREFIX ?= $(HOME)

.PHONY: all clean install enable test

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: $(TEST_BINS)
	./test_parser
	./test_config
	./test_storage
	./test_screenshot

test_parser: tests/test_parser.c src/parser.c
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_parser.c src/parser.c -Isrc -o $@

test_config: tests/test_config.c src/config.c src/log.c
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_config.c src/config.c src/log.c -Isrc -o $@

test_storage: tests/test_storage.c src/storage.c src/log.c
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_storage.c src/storage.c src/log.c -Isrc -o $@

test_screenshot: tests/test_screenshot.c src/screenshot.c src/log.c
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_screenshot.c src/screenshot.c src/log.c -Isrc -o $@

install: $(BIN)
	install -Dm755 $(BIN) $(PREFIX)/bin/$(BIN)
	install -Dm644 config/notif-archiver.conf \
		$(HOME)/.config/notif-archiver/notif-archiver.conf
	install -Dm644 systemd/notif-archiver.service \
		$(HOME)/.config/systemd/user/notif-archiver.service

enable:
	systemctl --user daemon-reload
	systemctl --user enable --now notif-archiver.service

clean:
	rm -f $(OBJ) $(DEP) $(BIN) $(TEST_BINS) test_parser.d test_config.d \
		test_storage.d test_screenshot.d

-include $(DEP)
