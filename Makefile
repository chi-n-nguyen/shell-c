CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude
SRCS    = src/main.c src/shell.c src/parser.c src/executor.c src/builtins.c src/history.c src/jobs.c src/expand.c
TARGET  = shell-c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)

test: $(TARGET)
	@bash tests/run.sh
