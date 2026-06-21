#ifndef TOKENIZER_H
#define TOKENIZER_H

typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE,
    TOKEN_REDIR_IN,
    TOKEN_REDIR_OUT,
    TOKEN_REDIR_APPEND,
    TOKEN_SEMICOLON,
    TOKEN_AND_IF,
    TOKEN_OR_IF,
    TOKEN_BACKGROUND
} TokenType;

typedef struct {
    TokenType type;
    char *text;
    int quoted;
} Token;

typedef struct {
    Token *items;
    int count;
    int capacity;
} TokenList;

void token_list_init(TokenList *list);
void token_list_free(TokenList *list);
int tokenize_line(const char *line, TokenList *tokens, int last_exit_status);

#endif
