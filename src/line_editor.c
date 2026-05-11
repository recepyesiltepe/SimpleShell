#define _POSIX_C_SOURCE 200809L

#include "line_editor.h"

#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "history.h"

#define CTRL_C 3
#define CTRL_D 4
#define CTRL_A 1
#define CTRL_E 5
#define CTRL_R 18
#define BACKSPACE 127
#define ESCAPE 27

static void redraw_line(const char *prompt, const char *buffer) {
    printf("\r\033[2K%s%s", prompt, buffer);
    fflush(stdout);
}

static void redraw_line_with_cursor(const char *prompt, const char *buffer, size_t cursor) {
    redraw_line(prompt, buffer);
    size_t len = strlen(buffer);
    if (cursor < len) {
        printf("\033[%zuD", len - cursor);
        fflush(stdout);
    }
}

static int is_word_char(unsigned char ch) {
    return !isspace(ch);
}

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
    char first_match[4096];
    size_t common_len;
} CompletionMatches;

static void completion_matches_init(CompletionMatches *matches) {
    matches->items = NULL;
    matches->count = 0;
    matches->capacity = 0;
    matches->first_match[0] = '\0';
    matches->common_len = 0;
}

static void completion_matches_free(CompletionMatches *matches) {
    for (size_t i = 0; i < matches->count; i++) {
        free(matches->items[i]);
    }
    free(matches->items);
}

static int completion_matches_contains(const CompletionMatches *matches, const char *candidate) {
    for (size_t i = 0; i < matches->count; i++) {
        if (strcmp(matches->items[i], candidate) == 0) {
            return 1;
        }
    }
    return 0;
}

static int completion_matches_add(CompletionMatches *matches, const char *candidate) {
    if (completion_matches_contains(matches, candidate)) {
        return 0;
    }

    if (matches->count == matches->capacity) {
        size_t new_capacity = matches->capacity == 0 ? 16 : matches->capacity * 2;
        char **new_items = realloc(matches->items, new_capacity * sizeof(char *));
        if (!new_items) {
            return 1;
        }
        matches->items = new_items;
        matches->capacity = new_capacity;
    }

    char *copied = strdup(candidate);
    if (!copied) {
        return 1;
    }

    if (matches->count == 0) {
        snprintf(matches->first_match, sizeof(matches->first_match), "%s", candidate);
        matches->common_len = strlen(matches->first_match);
    } else {
        size_t i = 0;
        while (i < matches->common_len && candidate[i] != '\0' &&
               matches->first_match[i] == candidate[i]) {
            i++;
        }
        matches->common_len = i;
    }

    matches->items[matches->count++] = copied;
    return 0;
}

static int collect_matches_from_dir(CompletionMatches *matches, const char *directory,
                                    const char *prefix, size_t prefix_len,
                                    int executable_only) {
    DIR *dir = opendir(directory);
    if (!dir) {
        return 0;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, prefix, prefix_len) != 0) {
            continue;
        }
        if (executable_only) {
            char full_path[8192];
            int written = snprintf(full_path, sizeof(full_path), "%s/%s", directory, entry->d_name);
            if (written < 0 || (size_t)written >= sizeof(full_path) || access(full_path, X_OK) != 0) {
                continue;
            }
        }
        if (completion_matches_add(matches, entry->d_name) != 0) {
            closedir(dir);
            return 1;
        }
    }

    closedir(dir);
    return 0;
}

static int is_command_position(const char *buffer, size_t word_start) {
    for (size_t i = 0; i < word_start; i++) {
        if (!isspace((unsigned char)buffer[i])) {
            return 0;
        }
    }
    return 1;
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

    CompletionMatches matches;
    completion_matches_init(&matches);

    int command_position = is_command_position(buffer, word_start);
    if (collect_matches_from_dir(&matches, ".", prefix, prefix_len, command_position) != 0) {
        completion_matches_free(&matches);
        putchar('\a');
        fflush(stdout);
        return;
    }

    if (command_position) {
        const char *path_value = getenv("PATH");
        const char *cursor = path_value ? path_value : "";
        while (*cursor != '\0') {
            const char *segment_end = cursor;
            while (*segment_end != '\0' && *segment_end != ':') {
                segment_end++;
            }

            char directory[4096];
            size_t segment_len = (size_t)(segment_end - cursor);
            if (segment_len == 0) {
                snprintf(directory, sizeof(directory), ".");
            } else if (segment_len >= sizeof(directory)) {
                cursor = *segment_end == ':' ? segment_end + 1 : segment_end;
                continue;
            } else {
                memcpy(directory, cursor, segment_len);
                directory[segment_len] = '\0';
            }

            if (collect_matches_from_dir(&matches, directory, prefix, prefix_len, 1) != 0) {
                completion_matches_free(&matches);
                putchar('\a');
                fflush(stdout);
                return;
            }

            cursor = *segment_end == ':' ? segment_end + 1 : segment_end;
        }
    }

    if (matches.count == 0) {
        completion_matches_free(&matches);
        putchar('\a');
        fflush(stdout);
        return;
    }

    if (matches.common_len > prefix_len) {
        size_t extra_len = matches.common_len - prefix_len;
        if (*len + extra_len >= buffer_size) {
            completion_matches_free(&matches);
            putchar('\a');
            fflush(stdout);
            return;
        }
        memcpy(buffer + *len, matches.first_match + prefix_len, extra_len);
        *len += extra_len;
        buffer[*len] = '\0';
        completion_matches_free(&matches);
        redraw_line(prompt, buffer);
        return;
    }

    if (matches.count == 1) {
        size_t match_len = strlen(matches.first_match);
        size_t new_len = word_start + match_len;
        if (new_len + 1 >= buffer_size) {
            completion_matches_free(&matches);
            putchar('\a');
            fflush(stdout);
            return;
        }
        memcpy(buffer + word_start, matches.first_match, match_len);
        *len = new_len;
        buffer[*len] = '\0';
        if (*len + 1 < buffer_size) {
            buffer[(*len)++] = ' ';
            buffer[*len] = '\0';
        }
        completion_matches_free(&matches);
        redraw_line(prompt, buffer);
        return;
    }

    printf("\n");
    for (size_t i = 0; i < matches.count; i++) {
        printf("%s  ", matches.items[i]);
    }
    printf("\n");
    completion_matches_free(&matches);
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
    size_t cursor = 0;
    int history_count = history_get_count();
    int history_position = history_count + 1;
    int browsing_history = 0;
    char current_line[4096];
    current_line[0] = '\0';
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
        if (ch == CTRL_A) {
            cursor = 0;
            redraw_line_with_cursor(prompt, buffer, cursor);
            continue;
        }
        if (ch == CTRL_E) {
            cursor = len;
            redraw_line_with_cursor(prompt, buffer, cursor);
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
            cursor = len;
            redraw_line_with_cursor(prompt, buffer, cursor);
            continue;
        }
        if (ch == '\t') {
            if (cursor != len) {
                putchar('\a');
                fflush(stdout);
                continue;
            }
            apply_tab_completion(buffer, &len, buffer_size, prompt);
            cursor = len;
            continue;
        }
        if (ch == BACKSPACE || ch == '\b') {
            if (cursor > 0) {
                memmove(buffer + cursor - 1, buffer + cursor, len - cursor + 1);
                cursor--;
                len--;
                redraw_line_with_cursor(prompt, buffer, cursor);
            }
            continue;
        }
        if (ch == ESCAPE) {
            unsigned char seq[2] = {0};
            if (read(STDIN_FILENO, &seq[0], 1) <= 0 || read(STDIN_FILENO, &seq[1], 1) <= 0) {
                continue;
            }
            if (seq[0] != '[') {
                continue;
            }
            if (seq[1] == 'D') {
                if (cursor > 0) {
                    cursor--;
                    printf("\033[1D");
                    fflush(stdout);
                }
                continue;
            }
            if (seq[1] == 'C') {
                if (cursor < len) {
                    cursor++;
                    printf("\033[1C");
                    fflush(stdout);
                }
                continue;
            }
            if (seq[1] == 'A') {
                if (!browsing_history) {
                    snprintf(current_line, sizeof(current_line), "%s", buffer);
                    browsing_history = 1;
                }
                if (history_position > 1) {
                    history_position--;
                } else {
                    putchar('\a');
                    fflush(stdout);
                }
                const char *entry = history_get_by_number(history_position);
                if (entry) {
                    size_t entry_len = strlen(entry);
                    if (entry_len < buffer_size) {
                        memcpy(buffer, entry, entry_len + 1);
                        len = entry_len;
                        cursor = len;
                        redraw_line_with_cursor(prompt, buffer, cursor);
                    }
                }
                continue;
            }
            if (seq[1] == 'B') {
                if (!browsing_history) {
                    putchar('\a');
                    fflush(stdout);
                    continue;
                }
                if (history_position < history_count + 1) {
                    history_position++;
                } else {
                    putchar('\a');
                    fflush(stdout);
                }
                if (history_position == history_count + 1) {
                    size_t current_len = strlen(current_line);
                    if (current_len >= buffer_size) {
                        current_len = buffer_size - 1;
                    }
                    memcpy(buffer, current_line, current_len);
                    buffer[current_len] = '\0';
                    len = current_len;
                    cursor = len;
                    redraw_line_with_cursor(prompt, buffer, cursor);
                } else {
                    const char *entry = history_get_by_number(history_position);
                    if (entry) {
                        size_t entry_len = strlen(entry);
                        if (entry_len < buffer_size) {
                            memcpy(buffer, entry, entry_len + 1);
                            len = entry_len;
                            cursor = len;
                            redraw_line_with_cursor(prompt, buffer, cursor);
                        }
                    }
                }
                continue;
            }
            if (seq[1] == '3') {
                unsigned char tail = 0;
                if (read(STDIN_FILENO, &tail, 1) <= 0 || tail != '~') {
                    continue;
                }
                if (cursor < len) {
                    memmove(buffer + cursor, buffer + cursor + 1, len - cursor);
                    len--;
                    redraw_line_with_cursor(prompt, buffer, cursor);
                }
                continue;
            }
            continue;
        }
        if (isprint(ch)) {
            if (len + 1 < buffer_size) {
                memmove(buffer + cursor + 1, buffer + cursor, len - cursor + 1);
                buffer[cursor] = (char)ch;
                cursor++;
                len++;
                redraw_line_with_cursor(prompt, buffer, cursor);
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
