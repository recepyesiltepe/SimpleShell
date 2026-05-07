#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "execute.h"
#include "parser.h"
#include "tokenizer.h"

#define MAX_LINE_LEN 4096

int main(void) {
    char line[MAX_LINE_LEN];
    int exit_status = 0;

    while (1) {
        printf("SimpleShell$ ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            if (feof(stdin)) {
                putchar('\n');
                break;
            }
            perror("fgets");
            continue;
        }

        size_t line_len = strlen(line);
        if (line_len > 0 && line[line_len - 1] == '\n') {
            line[line_len - 1] = '\0';
        }

        TokenList tokens;
        token_list_init(&tokens);
        if (tokenize_line(line, &tokens, exit_status) != 0) {
            token_list_free(&tokens);
            continue;
        }

        AstNode *root = NULL;
        int parse_status = parse_tokens(&tokens, &root);
        if (parse_status == 1) {
            token_list_free(&tokens);
            continue;
        }
        if (parse_status != 0) {
            token_list_free(&tokens);
            continue;
        }

        bool should_exit = false;
        exit_status = execute_ast(root, &should_exit);

        ast_node_free(root);
        token_list_free(&tokens);

        if (should_exit) {
            break;
        }
    }

    return exit_status;
}
