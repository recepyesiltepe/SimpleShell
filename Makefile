CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11
TARGET = SimpleShell
SRC = shell.c \
      memory.c \
      history.c \
      ast.c \
      tokenizer.c \
      parser.c \
      builtins.c \
      jobs.c \
      redirection.c \
      execute.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
