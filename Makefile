CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude
SRCS    = src/main.c src/shell.c src/parser.c src/executor.c src/builtins.c src/history.c
TARGET  = shell-c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
