#include "tokenizer.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"

#define MAX_LINE_LEN 4096

static void token_list_push(TokenList *list, Token token) {
    if (list->count == list->capacity) {
        list->capacity *= 2;
        list->items = xrealloc(list->items, (size_t)list->capacity * sizeof(Token));
    }
    list->items[list->count++] = token;
}

static void push_operator(TokenList *tokens, TokenType type) {
    Token token;
    token.type = type;
    token.text = NULL;
    token.quoted = 0;
    token_list_push(tokens, token);
}

static int push_word(TokenList *tokens, const char *word, bool quoted) {
    Token token;
    token.type = TOKEN_WORD;
    token.text = xstrdup(word);
    token.quoted = quoted ? 1 : 0;
    if (!token.text) {
        perror("strdup");
        return -1;
    }
    token_list_push(tokens, token);
    return 0;
}

static int append_char(char *buffer, size_t *index, char ch) {
    if (*index + 1 >= MAX_LINE_LEN) {
        fprintf(stderr, "Error: token too long\n");
        return -1;
    }
    buffer[(*index)++] = ch;
    return 0;
}

static int append_string(char *buffer, size_t *index, const char *text) {
    while (*text != '\0') {
        if (append_char(buffer, index, *text++) != 0) {
            return -1;
        }
    }
    return 0;
}

static int append_variable_value(const char **cursor, char *buffer, size_t *index,
                                 int last_exit_status) {
    const char *start = *cursor;
    if (*start != '$') {
        return 0;
    }

    start++;
    if (*start == '?') {
        char status_text[32];
        snprintf(status_text, sizeof(status_text), "%d", last_exit_status);
        if (append_string(buffer, index, status_text) != 0) {
            return -1;
        }
        *cursor = start + 1;
        return 0;
    }

    char variable_name[MAX_LINE_LEN];
    size_t name_len = 0;

    if (*start == '{') {
        start++;
        while (*start != '\0' && *start != '}') {
            if (!(isalnum((unsigned char)*start) || *start == '_')) {
                break;
            }
            if (name_len + 1 >= sizeof(variable_name)) {
                fprintf(stderr, "Error: variable name too long\n");
                return -1;
            }
            variable_name[name_len++] = *start++;
        }
        if (*start == '}') {
            start++;
        } else {
            if (append_char(buffer, index, '$') != 0) {
                return -1;
            }
            *cursor = *cursor + 1;
            return 0;
        }
    } else {
        if (!(isalpha((unsigned char)*start) || *start == '_')) {
            if (append_char(buffer, index, '$') != 0) {
                return -1;
            }
            *cursor = *cursor + 1;
            return 0;
        }
        while (isalnum((unsigned char)*start) || *start == '_') {
            if (name_len + 1 >= sizeof(variable_name)) {
                fprintf(stderr, "Error: variable name too long\n");
                return -1;
            }
            variable_name[name_len++] = *start++;
        }
    }

    variable_name[name_len] = '\0';
    const char *value = getenv(variable_name);
    if (value && append_string(buffer, index, value) != 0) {
        return -1;
    }
    *cursor = start;
    return 0;
}

void token_list_init(TokenList *list) {
    list->count = 0;
    list->capacity = 16;
    list->items = xmalloc((size_t)list->capacity * sizeof(Token));
}

void token_list_free(TokenList *list) {
    for (int i = 0; i < list->count; i++) {
        free(list->items[i].text);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

int tokenize_line(const char *line, TokenList *tokens, int last_exit_status) {
    const char *cursor = line;
    while (*cursor != '\0') {
        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        if (*cursor == '#') {
            break;
        }

        if (*cursor == '&' && *(cursor + 1) == '&') {
            push_operator(tokens, TOKEN_AND_IF);
            cursor += 2;
            continue;
        }
        if (*cursor == '&') {
            push_operator(tokens, TOKEN_BACKGROUND);
            cursor++;
            continue;
        }
        if (*cursor == '|' && *(cursor + 1) == '|') {
            push_operator(tokens, TOKEN_OR_IF);
            cursor += 2;
            continue;
        }
        if (*cursor == '|') {
            push_operator(tokens, TOKEN_PIPE);
            cursor++;
            continue;
        }
        if (*cursor == ';') {
            push_operator(tokens, TOKEN_SEMICOLON);
            cursor++;
            continue;
        }
        if (*cursor == '<') {
            push_operator(tokens, TOKEN_REDIR_IN);
            cursor++;
            continue;
        }
        if (*cursor == '>') {
            if (*(cursor + 1) == '>') {
                push_operator(tokens, TOKEN_REDIR_APPEND);
                cursor += 2;
            } else {
                push_operator(tokens, TOKEN_REDIR_OUT);
                cursor++;
            }
            continue;
        }

        char buffer[MAX_LINE_LEN];
        size_t index = 0;
        bool token_started = false;
        bool quoted = false;

        while (*cursor != '\0' && !isspace((unsigned char)*cursor) &&
               *cursor != '&' && *cursor != '|' && *cursor != ';' &&
               *cursor != '<' && *cursor != '>') {
            token_started = true;
            if (*cursor == '\'' || *cursor == '"') {
                quoted = true;
                char quote = *cursor++;
                while (*cursor != '\0' && *cursor != quote) {
                    if (quote == '"' && *cursor == '\\' && *(cursor + 1) != '\0') {
                        cursor++;
                        if (append_char(buffer, &index, *cursor++) != 0) {
                            return -1;
                        }
                    } else if (quote == '"' && *cursor == '$') {
                        if (append_variable_value(&cursor, buffer, &index, last_exit_status) !=
                            0) {
                            return -1;
                        }
                    } else {
                        if (append_char(buffer, &index, *cursor++) != 0) {
                            return -1;
                        }
                    }
                }
                if (*cursor != quote) {
                    fprintf(stderr, "Error: unmatched quote\n");
                    return -1;
                }
                cursor++;
            } else {
                if (*cursor == '\\' && *(cursor + 1) != '\0') {
                    cursor++;
                    if (append_char(buffer, &index, *cursor++) != 0) {
                        return -1;
                    }
                } else if (*cursor == '$') {
                    if (append_variable_value(&cursor, buffer, &index, last_exit_status) != 0) {
                        return -1;
                    }
                } else {
                    if (append_char(buffer, &index, *cursor++) != 0) {
                        return -1;
                    }
                }
            }
        }

        buffer[index] = '\0';
        if (token_started && push_word(tokens, buffer, quoted) != 0) {
            return -1;
        }
    }

    return 0;
}
