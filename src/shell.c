#include <stdbool.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aliases.h"
#include "ast.h"
#include "builtins.h"
#include "execute.h"
#include "history.h"
#include "jobs.h"
#include "line_editor.h"
#include "parser.h"
#include "tokenizer.h"

#define MAX_LINE_LEN 4096

static bool has_non_whitespace(const char *text) {
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; cursor++) {
        if (!isspace(*cursor)) {
            return true;
        }
    }
    return false;
}

static const char *resolve_history_expansion(const char *line) {
    if (strcmp(line, "!!") == 0) {
        return history_get_last();
    }
    if (line[0] == '!' && line[1] != '\0') {
        char *end = NULL;
        long entry_number = strtol(line + 1, &end, 10);
        if (*end == '\0' && entry_number > 0 && entry_number <= INT_MAX) {
            return history_get_by_number((int)entry_number);
        }
    }
    return NULL;
}

static void format_prompt(int exit_status, char *prompt, size_t prompt_size) {
    char *cwd = getcwd(NULL, 0);
    if (!cwd) {
        perror("getcwd");
        snprintf(prompt, prompt_size, "[%d] ? $ ", exit_status);
        return;
    }

    const char *color = exit_status == 0 ? "\033[32m" : "\033[31m";
    snprintf(prompt, prompt_size, "%s[%d]\033[0m %s $ ", color, exit_status, cwd);
    free(cwd);
}

int main(void) {
    char line[MAX_LINE_LEN];
    char expanded_line[MAX_LINE_LEN];
    char prompt[8192];
    int exit_status = 0;

    if (history_init() != 0) {
        fprintf(stderr, "warning: failed to initialize history file\n");
    }

    while (1) {
        builtins_reap_jobs();
        format_prompt(exit_status, prompt, sizeof(prompt));

        int read_status = read_command_line(prompt, line, sizeof(line));
        if (read_status == 0) {
            if (isatty(STDIN_FILENO)) {
                putchar('\n');
            }
            break;
        }
        if (read_status < 0) {
            perror("read_command_line");
            continue;
        }

        const char *line_to_run = line;
        const char *expanded = resolve_history_expansion(line);
        if (expanded) {
            size_t expanded_len = strlen(expanded);
            if (expanded_len >= sizeof(expanded_line)) {
                fprintf(stderr, "Error: expanded history command too long\n");
                continue;
            }
            memcpy(expanded_line, expanded, expanded_len + 1);
            line_to_run = expanded_line;
            printf("%s\n", line_to_run);
        } else if (line[0] == '!' && line[1] != '\0') {
            fprintf(stderr, "history: event not found: %s\n", line);
            continue;
        }

        if (has_non_whitespace(line_to_run)) {
            history_add(line_to_run);
        }

        char *aliased_line = aliases_expand_line(line_to_run);

        TokenList tokens;
        token_list_init(&tokens);
        if (tokenize_line(aliased_line, &tokens, exit_status) != 0) {
            token_list_free(&tokens);
            free(aliased_line);
            continue;
        }

        AstNode *root = NULL;
        int parse_status = parse_tokens(&tokens, &root);
        if (parse_status == 1) {
            token_list_free(&tokens);
            free(aliased_line);
            continue;
        }
        if (parse_status != 0) {
            token_list_free(&tokens);
            free(aliased_line);
            continue;
        }

        bool should_exit = false;
        exit_status = execute_ast(root, &should_exit);

        ast_node_free(root);
        token_list_free(&tokens);
        free(aliased_line);

        if (should_exit) {
            break;
        }
    }

    history_cleanup();
    aliases_cleanup();
    jobs_cleanup();
    return exit_status;
}
