CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -Iinclude
TARGET = bin/SimpleShell
SRC = src/shell.c \
      src/line_editor.c \
      src/memory.c \
      src/aliases.c \
      src/expansion.c \
      src/runner.c \
      src/history.c \
      src/ast.c \
      src/tokenizer.c \
      src/parser.c \
      src/builtins.c \
      src/jobs.c \
      src/redirection.c \
      src/execute.c

.PHONY: all clean run-ui test

all: $(TARGET)

$(TARGET): $(SRC)
	mkdir -p $(dir $(TARGET))
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

run-ui: $(TARGET)
	python3 ui/simpleshell_gtk4.py

test: $(TARGET)
	python3 tests/run_tests.py
