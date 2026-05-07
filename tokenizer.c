#include "tokenizer.h"

#include <ctype.h>
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
    token_list_push(tokens, token);
}

static int push_word(TokenList *tokens, const char *word) {
    Token token;
    token.type = TOKEN_WORD;
    token.text = xstrdup(word);
    if (!token.text) {
        perror("strdup");
        return -1;
    }
    token_list_push(tokens, token);
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

int tokenize_line(const char *line, TokenList *tokens) {
    const char *cursor = line;
    while (*cursor != '\0') {
        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        if (*cursor == '&' && *(cursor + 1) == '&') {
            push_operator(tokens, TOKEN_AND_IF);
            cursor += 2;
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

        while (*cursor != '\0' && !isspace((unsigned char)*cursor) &&
               *cursor != '&' && *cursor != '|' && *cursor != ';' &&
               *cursor != '<' && *cursor != '>') {
            if (*cursor == '\'' || *cursor == '"') {
                char quote = *cursor++;
                while (*cursor != '\0' && *cursor != quote) {
                    if (index + 1 >= sizeof(buffer)) {
                        fprintf(stderr, "Error: token too long\n");
                        return -1;
                    }
                    buffer[index++] = *cursor++;
                }
                if (*cursor != quote) {
                    fprintf(stderr, "Error: unmatched quote\n");
                    return -1;
                }
                cursor++;
            } else {
                if (index + 1 >= sizeof(buffer)) {
                    fprintf(stderr, "Error: token too long\n");
                    return -1;
                }
                buffer[index++] = *cursor++;
            }
        }

        buffer[index] = '\0';
        if (index > 0 && push_word(tokens, buffer) != 0) {
            return -1;
        }
    }

    return 0;
}
