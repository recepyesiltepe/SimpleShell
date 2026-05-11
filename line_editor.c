#define _POSIX_C_SOURCE 200809L

#include "line_editor.h"

#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "history.h"

#define CTRL_C 3
#define CTRL_D 4
#define CTRL_R 18
#define BACKSPACE 127

static void redraw_line(const char *prompt, const char *buffer) {
    printf("\r\033[2K%s%s", prompt, buffer);
    fflush(stdout);
}

static int is_word_char(unsigned char ch) {
    return !isspace(ch);
}

static void apply_tab_completion(char *buffer, size_t *len, size_t buffer_size, const char *prompt) {
    size_t word_start = *len;
    while (word_start > 0 && is_word_char((unsigned char)buffer[word_start - 1])) {
        word_start--;
    }

    const char *prefix = buffer + word_start;
    size_t prefix_len = *len - word_start;
    if (prefix_len == 0 || strchr(prefix, '/') != NULL) {
        putchar('\a');
        fflush(stdout);
        return;
    }

    DIR *dir = opendir(".");
    if (!dir) {
        putchar('\a');
        fflush(stdout);
        return;
    }

    char first_match[4096];
    first_match[0] = '\0';
    size_t common_len = 0;
    int match_count = 0;

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, prefix, prefix_len) != 0) {
            continue;
        }
        if (match_count == 0) {
            snprintf(first_match, sizeof(first_match), "%s", entry->d_name);
            common_len = strlen(first_match);
        } else {
            size_t i = 0;
            while (i < common_len && entry->d_name[i] != '\0' && first_match[i] == entry->d_name[i]) {
                i++;
            }
            common_len = i;
        }
        match_count++;
    }
    closedir(dir);

    if (match_count == 0) {
        putchar('\a');
        fflush(stdout);
        return;
    }

    if (common_len > prefix_len) {
        size_t extra_len = common_len - prefix_len;
        if (*len + extra_len >= buffer_size) {
            putchar('\a');
            fflush(stdout);
            return;
        }
        memcpy(buffer + *len, first_match + prefix_len, extra_len);
        *len += extra_len;
        buffer[*len] = '\0';
        redraw_line(prompt, buffer);
        return;
    }

    if (match_count == 1) {
        size_t match_len = strlen(first_match);
        size_t new_len = word_start + match_len;
        if (new_len + 1 >= buffer_size) {
            putchar('\a');
            fflush(stdout);
            return;
        }
        memcpy(buffer + word_start, first_match, match_len);
        *len = new_len;
        buffer[*len] = '\0';
        if (*len + 1 < buffer_size) {
            buffer[(*len)++] = ' ';
            buffer[*len] = '\0';
        }
        redraw_line(prompt, buffer);
        return;
    }

    printf("\n");
    dir = opendir(".");
    if (!dir) {
        redraw_line(prompt, buffer);
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, prefix, prefix_len) == 0) {
            printf("%s  ", entry->d_name);
        }
    }
    closedir(dir);
    printf("\n");
    redraw_line(prompt, buffer);
}

static int read_tty_line(const char *prompt, char *buffer, size_t buffer_size) {
    struct termios original = {0};
    struct termios raw = {0};
    if (tcgetattr(STDIN_FILENO, &original) != 0) {
        return -1;
    }
    raw = original;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return -1;
    }

    buffer[0] = '\0';
    size_t len = 0;
    printf("%s", prompt);
    fflush(stdout);

    while (1) {
        unsigned char ch = 0;
        ssize_t bytes_read = read(STDIN_FILENO, &ch, 1);
        if (bytes_read <= 0) {
            tcsetattr(STDIN_FILENO, TCSANOW, &original);
            return -1;
        }

        if (ch == '\n' || ch == '\r') {
            printf("\n");
            break;
        }
        if (ch == CTRL_D) {
            if (len == 0) {
                tcsetattr(STDIN_FILENO, TCSANOW, &original);
                return 0;
            }
            continue;
        }
        if (ch == CTRL_C) {
            printf("^C\n");
            buffer[0] = '\0';
            len = 0;
            break;
        }
        if (ch == CTRL_R) {
            const char *match = history_find_latest_containing(buffer);
            if (!match) {
                putchar('\a');
                fflush(stdout);
                continue;
            }
            size_t match_len = strlen(match);
            if (match_len >= buffer_size) {
                putchar('\a');
                fflush(stdout);
                continue;
            }
            memcpy(buffer, match, match_len + 1);
            len = match_len;
            redraw_line(prompt, buffer);
            continue;
        }
        if (ch == '\t') {
            apply_tab_completion(buffer, &len, buffer_size, prompt);
            continue;
        }
        if (ch == BACKSPACE || ch == '\b') {
            if (len > 0) {
                len--;
                buffer[len] = '\0';
                redraw_line(prompt, buffer);
            }
            continue;
        }
        if (isprint(ch)) {
            if (len + 1 < buffer_size) {
                buffer[len++] = (char)ch;
                buffer[len] = '\0';
                putchar((char)ch);
                fflush(stdout);
            } else {
                putchar('\a');
                fflush(stdout);
            }
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &original);
    return 1;
}

int read_command_line(const char *prompt, char *buffer, size_t buffer_size) {
    if (!prompt || !buffer || buffer_size == 0) {
        return -1;
    }

    if (!isatty(STDIN_FILENO)) {
        if (!fgets(buffer, (int)buffer_size, stdin)) {
            return feof(stdin) ? 0 : -1;
        }
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
        return 1;
    }

    return read_tty_line(prompt, buffer, buffer_size);
}
