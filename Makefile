CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11 $(shell pkg-config --cflags libsystemd)
LDFLAGS = $(shell pkg-config --libs libsystemd)

SRC = src/main.c src/config.c src/bus_listener.c src/parser.c src/storage.c src/screenshot.c src/log.c
OBJ = $(SRC:.c=.o)
BIN = notif-archiver

.PHONY: all clean install test

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: tests/test_parser.c src/parser.c
	$(CC) $(CFLAGS) -o test_parser tests/test_parser.c src/parser.c -Isrc
	./test_parser

install: $(BIN)
	install -Dm755 $(BIN) $(HOME)/bin/$(BIN)
	install -Dm644 config/notif-archiver.conf $(HOME)/.config/notif-archiver/notif-archiver.conf
	install -Dm644 systemd/notif-archiver.service $(HOME)/.config/systemd/user/notif-archiver.service
	systemctl --user daemon-reload
	@echo "Run: systemctl --user enable --now notif-archive.service"

clean:
	rm -f $(OBJ) $(BIN) test_parser
