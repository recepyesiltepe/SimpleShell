#define _POSIX_C_SOURCE 200809L

#include "history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memory.h"

static char **history_entries = NULL;
static int history_count = 0;
static int history_capacity = 0;
static char history_file_path[4096];
static int history_file_ready = 0;

static int set_history_file_path(const char *path) {
    int written = snprintf(history_file_path, sizeof(history_file_path), "%s", path);
    if (written < 0 || (size_t)written >= sizeof(history_file_path)) {
        return 1;
    }
    return 0;
}

static int ensure_history_file_writable(void) {
    FILE *file = fopen(history_file_path, "a");
    if (!file) {
        return 1;
    }
    fclose(file);
    return 0;
}

static void ensure_history_capacity(void) {
    if (history_count < history_capacity) {
        return;
    }
    history_capacity = history_capacity == 0 ? 16 : history_capacity * 2;
    history_entries = xrealloc(history_entries, (size_t)history_capacity * sizeof(char *));
}

int history_add(const char *line) {
    ensure_history_capacity();
    history_entries[history_count++] = xstrdup(line);

    if (history_file_ready) {
        FILE *file = fopen(history_file_path, "a");
        if (file) {
            fprintf(file, "%s\n", line);
            fclose(file);
        }
    }
    return 0;
}

int history_print(void) {
    for (int i = 0; i < history_count; i++) {
        printf("%d %s\n", i + 1, history_entries[i]);
    }
    return 0;
}

int history_print_last(int count) {
    if (count < 0) {
        return 1;
    }
    if (count > history_count) {
        count = history_count;
    }

    int start_index = history_count - count;
    for (int i = start_index; i < history_count; i++) {
        printf("%d %s\n", i + 1, history_entries[i]);
    }
    return 0;
}

int history_clear(void) {
    for (int i = 0; i < history_count; i++) {
        free(history_entries[i]);
        history_entries[i] = NULL;
    }
    history_count = 0;
    if (history_file_ready) {
        FILE *file = fopen(history_file_path, "w");
        if (file) {
            fclose(file);
        }
    }
    return 0;
}

const char *history_get_last(void) {
    if (history_count == 0) {
        return NULL;
    }
    return history_entries[history_count - 1];
}

const char *history_get_by_number(int entry_number) {
    if (entry_number <= 0 || entry_number > history_count) {
        return NULL;
    }
    return history_entries[entry_number - 1];
}

const char *history_find_latest_containing(const char *query) {
    if (!query || query[0] == '\0') {
        return history_get_last();
    }
    for (int i = history_count - 1; i >= 0; i--) {
        if (strstr(history_entries[i], query) != NULL) {
            return history_entries[i];
        }
    }
    return NULL;
}

const char *history_find_previous_containing(const char *query, int before_entry_number,
                                             int *found_entry_number) {
    if (before_entry_number <= 1) {
        return NULL;
    }
    if (before_entry_number > history_count + 1) {
        before_entry_number = history_count + 1;
    }

    for (int i = before_entry_number - 2; i >= 0; i--) {
        if (!query || query[0] == '\0' || strstr(history_entries[i], query) != NULL) {
            if (found_entry_number) {
                *found_entry_number = i + 1;
            }
            return history_entries[i];
        }
    }
    return NULL;
}

int history_get_count(void) {
    return history_count;
}

int history_init(void) {
    const char *home = getenv("HOME");
    if (home && home[0] != '\0') {
        char preferred_path[4096];
        int written = snprintf(preferred_path, sizeof(preferred_path), "%s/.simpleshell_history", home);
        if (written >= 0 && (size_t)written < sizeof(preferred_path) &&
            set_history_file_path(preferred_path) == 0 &&
            ensure_history_file_writable() == 0) {
            history_file_ready = 1;
        }
    }

    if (!history_file_ready) {
        if (set_history_file_path(".simpleshell_history") != 0 ||
            ensure_history_file_writable() != 0) {
            history_file_ready = 0;
            return 1;
        }
        history_file_ready = 1;
    }

    FILE *file = fopen(history_file_path, "r");
    if (file) {
        char *line = NULL;
        size_t line_cap = 0;
        ssize_t line_len = 0;
        while ((line_len = getline(&line, &line_cap, file)) != -1) {
            if (line_len > 0 && line[line_len - 1] == '\n') {
                line[line_len - 1] = '\0';
            }
            if (line[0] == '\0') {
                continue;
            }
            ensure_history_capacity();
            history_entries[history_count++] = xstrdup(line);
        }
        free(line);
        fclose(file);
    }

    return 0;
}

void history_cleanup(void) {
    for (int i = 0; i < history_count; i++) {
        free(history_entries[i]);
    }
    free(history_entries);
    history_entries = NULL;
    history_count = 0;
    history_capacity = 0;
    history_file_ready = 0;
    history_file_path[0] = '\0';
}
