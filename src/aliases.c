#include "aliases.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"

typedef struct {
    char *name;
    char *value;
} Alias;

static Alias *aliases = NULL;
static int aliases_count = 0;
static int aliases_capacity = 0;

static int is_valid_alias_name(const char *name) {
    if (!name || name[0] == '\0') {
        return 0;
    }
    for (const unsigned char *cursor = (const unsigned char *)name; *cursor != '\0'; cursor++) {
        if (isspace(*cursor) || *cursor == '=' || *cursor == '&' || *cursor == '|' ||
            *cursor == ';' || *cursor == '<' || *cursor == '>') {
            return 0;
        }
    }
    return 1;
}

static void ensure_alias_capacity(void) {
    if (aliases_count < aliases_capacity) {
        return;
    }
    aliases_capacity = aliases_capacity == 0 ? 8 : aliases_capacity * 2;
    aliases = xrealloc(aliases, (size_t)aliases_capacity * sizeof(Alias));
}

static int find_alias_index(const char *name) {
    for (int i = 0; i < aliases_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static const char *find_alias_value(const char *name) {
    int index = find_alias_index(name);
    return index >= 0 ? aliases[index].value : NULL;
}

static void print_alias(const Alias *alias) {
    printf("alias %s='%s'\n", alias->name, alias->value);
}

int aliases_set(const char *name, const char *value) {
    if (!is_valid_alias_name(name)) {
        fprintf(stderr, "alias: invalid name: %s\n", name ? name : "");
        return 1;
    }

    int index = find_alias_index(name);
    if (index >= 0) {
        char *new_value = xstrdup(value);
        free(aliases[index].value);
        aliases[index].value = new_value;
        return 0;
    }

    ensure_alias_capacity();
    aliases[aliases_count].name = xstrdup(name);
    aliases[aliases_count].value = xstrdup(value);
    aliases_count++;
    return 0;
}

int aliases_remove(const char *name) {
    int index = find_alias_index(name);
    if (index < 0) {
        fprintf(stderr, "unalias: %s: not found\n", name);
        return 1;
    }

    free(aliases[index].name);
    free(aliases[index].value);
    for (int i = index; i < aliases_count - 1; i++) {
        aliases[i] = aliases[i + 1];
    }
    aliases_count--;
    return 0;
}

int aliases_print(const char *name) {
    int index = find_alias_index(name);
    if (index < 0) {
        fprintf(stderr, "alias: %s: not found\n", name);
        return 1;
    }
    print_alias(&aliases[index]);
    return 0;
}

int aliases_print_all(void) {
    for (int i = 0; i < aliases_count; i++) {
        print_alias(&aliases[i]);
    }
    return 0;
}

static int find_first_word(const char *line, size_t *word_start, size_t *word_len) {
    size_t start = 0;
    while (isspace((unsigned char)line[start])) {
        start++;
    }
    if (line[start] == '\0' || line[start] == '\'' || line[start] == '"') {
        return 0;
    }

    size_t end = start;
    while (line[end] != '\0' && !isspace((unsigned char)line[end]) && line[end] != '&' &&
           line[end] != '|' && line[end] != ';' && line[end] != '<' && line[end] != '>') {
        end++;
    }
    if (end == start) {
        return 0;
    }

    *word_start = start;
    *word_len = end - start;
    return 1;
}

char *aliases_expand_line(const char *line) {
    char *expanded = xstrdup(line);

    for (int depth = 0; depth < 16; depth++) {
        size_t word_start = 0;
        size_t word_len = 0;
        if (!find_first_word(expanded, &word_start, &word_len)) {
            return expanded;
        }

        char *name = xmalloc(word_len + 1);
        memcpy(name, expanded + word_start, word_len);
        name[word_len] = '\0';

        const char *value = find_alias_value(name);
        free(name);
        if (!value) {
            return expanded;
        }

        size_t value_len = strlen(value);
        size_t expanded_len = strlen(expanded);
        size_t new_len = word_start + value_len + (expanded_len - word_start - word_len);
        char *replacement = xmalloc(new_len + 1);
        memcpy(replacement, expanded, word_start);
        memcpy(replacement + word_start, value, value_len);
        memcpy(replacement + word_start + value_len, expanded + word_start + word_len,
               expanded_len - word_start - word_len + 1);
        free(expanded);
        expanded = replacement;
    }

    fprintf(stderr, "alias: expansion limit reached\n");
    return expanded;
}

void aliases_cleanup(void) {
    for (int i = 0; i < aliases_count; i++) {
        free(aliases[i].name);
        free(aliases[i].value);
    }
    free(aliases);
    aliases = NULL;
    aliases_count = 0;
    aliases_capacity = 0;
}
