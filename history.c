#include "history.h"

#include <stdio.h>
#include <stdlib.h>

#include "memory.h"

static char **history_entries = NULL;
static int history_count = 0;
static int history_capacity = 0;

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
    return 0;
}

int history_print(void) {
    for (int i = 0; i < history_count; i++) {
        printf("%d %s\n", i + 1, history_entries[i]);
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
}
