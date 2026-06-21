#define _POSIX_C_SOURCE 200809L

#include "runner.h"

#include <stdio.h>
#include <stdlib.h>

#include "aliases.h"
#include "ast.h"
#include "execute.h"
#include "parser.h"
#include "tokenizer.h"

int run_shell_line(const char *line, int *exit_status, bool *should_exit) {
    *should_exit = false;

    char *aliased_line = aliases_expand_line(line);

    TokenList tokens;
    token_list_init(&tokens);
    if (tokenize_line(aliased_line, &tokens, *exit_status) != 0) {
        token_list_free(&tokens);
        free(aliased_line);
        return 1;
    }

    AstNode *root = NULL;
    int parse_status = parse_tokens(&tokens, &root);
    if (parse_status == 1) {
        token_list_free(&tokens);
        free(aliased_line);
        return 0;
    }
    if (parse_status != 0) {
        token_list_free(&tokens);
        free(aliased_line);
        return 1;
    }

    *exit_status = execute_ast(root, should_exit);

    ast_node_free(root);
    token_list_free(&tokens);
    free(aliased_line);
    return 0;
}

int run_shell_file(const char *path, int *exit_status, bool *should_exit) {
    *should_exit = false;

    FILE *file = fopen(path, "r");
    if (!file) {
        perror(path);
        return 1;
    }

    char *line = NULL;
    size_t line_capacity = 0;
    ssize_t line_len = 0;
    int status = 0;

    while ((line_len = getline(&line, &line_capacity, file)) != -1) {
        if (line_len > 0 && line[line_len - 1] == '\n') {
            line[line_len - 1] = '\0';
        }

        status = run_shell_line(line, exit_status, should_exit);
        if (status != 0 || *should_exit) {
            break;
        }
    }

    if (ferror(file)) {
        perror(path);
        status = 1;
    }

    free(line);
    fclose(file);
    return status;
}
