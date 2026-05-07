#include "parser.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"

typedef struct {
    TokenList *tokens;
    int pos;
} ParserState;

static bool is_at_end(ParserState *state) {
    return state->pos >= state->tokens->count;
}

static Token *peek_token(ParserState *state) {
    if (is_at_end(state)) {
        return NULL;
    }
    return &state->tokens->items[state->pos];
}

static bool match_token(ParserState *state, TokenType type) {
    Token *token = peek_token(state);
    if (!token || token->type != type) {
        return false;
    }
    state->pos++;
    return true;
}

static bool token_starts_command(Token *token) {
    if (!token) {
        return false;
    }
    return token->type == TOKEN_WORD || token->type == TOKEN_REDIR_IN ||
           token->type == TOKEN_REDIR_OUT || token->type == TOKEN_REDIR_APPEND;
}

static void parse_error(const char *message) {
    fprintf(stderr, "Syntax error: %s\n", message);
}

static int parse_command(ParserState *state, Command *cmd) {
    bool saw_word = false;

    while (!is_at_end(state)) {
        Token *token = peek_token(state);
        if (!token_starts_command(token)) {
            break;
        }

        if (token->type == TOKEN_WORD) {
            command_add_arg(cmd, token->text);
            saw_word = true;
            state->pos++;
            continue;
        }

        TokenType redir_type = token->type;
        state->pos++;

        Token *target = peek_token(state);
        if (!target || target->type != TOKEN_WORD) {
            if (redir_type == TOKEN_REDIR_IN) {
                parse_error("expected file after '<'");
            } else if (redir_type == TOKEN_REDIR_OUT) {
                parse_error("expected file after '>'");
            } else {
                parse_error("expected file after '>>'");
            }
            return -1;
        }

        if (redir_type == TOKEN_REDIR_IN) {
            free(cmd->input_file);
            cmd->input_file = xstrdup(target->text);
        } else {
            free(cmd->output_file);
            cmd->output_file = xstrdup(target->text);
            cmd->append_output = (redir_type == TOKEN_REDIR_APPEND);
        }

        state->pos++;
    }

    if (!saw_word) {
        parse_error("missing command");
        return -1;
    }

    return 0;
}

static int parse_pipeline(ParserState *state, AstNode **out_node) {
    Pipeline pipeline;
    pipeline_init(&pipeline);

    Command current;
    command_init(&current);

    if (parse_command(state, &current) != 0) {
        command_free(&current);
        pipeline_free(&pipeline);
        return -1;
    }
    pipeline_push_command(&pipeline, &current);

    while (match_token(state, TOKEN_PIPE)) {
        command_init(&current);
        if (parse_command(state, &current) != 0) {
            command_free(&current);
            pipeline_free(&pipeline);
            return -1;
        }
        pipeline_push_command(&pipeline, &current);
    }

    *out_node = ast_node_new_pipeline(&pipeline);
    return 0;
}

static int parse_and_or(ParserState *state, AstNode **out_node) {
    AstNode *left = NULL;
    if (parse_pipeline(state, &left) != 0) {
        return -1;
    }

    while (true) {
        AstNodeType type;
        if (match_token(state, TOKEN_AND_IF)) {
            type = AST_NODE_AND;
        } else if (match_token(state, TOKEN_OR_IF)) {
            type = AST_NODE_OR;
        } else {
            break;
        }

        AstNode *right = NULL;
        if (parse_pipeline(state, &right) != 0) {
            ast_node_free(left);
            return -1;
        }
        left = ast_node_new_binary(type, left, right);
    }

    *out_node = left;
    return 0;
}

static int parse_sequence(ParserState *state, AstNode **out_node) {
    AstNode *left = NULL;
    if (parse_and_or(state, &left) != 0) {
        return -1;
    }

    while (match_token(state, TOKEN_SEMICOLON)) {
        if (is_at_end(state)) {
            parse_error("trailing ';'");
            ast_node_free(left);
            return -1;
        }

        AstNode *right = NULL;
        if (parse_and_or(state, &right) != 0) {
            ast_node_free(left);
            return -1;
        }
        left = ast_node_new_binary(AST_NODE_SEQUENCE, left, right);
    }

    *out_node = left;
    return 0;
}

int parse_tokens(TokenList *tokens, AstNode **out_root) {
    *out_root = NULL;
    if (tokens->count == 0) {
        return 1;
    }

    ParserState state;
    state.tokens = tokens;
    state.pos = 0;

    if (parse_sequence(&state, out_root) != 0) {
        return -1;
    }

    if (!is_at_end(&state)) {
        parse_error("unexpected token");
        ast_node_free(*out_root);
        *out_root = NULL;
        return -1;
    }

    return 0;
}
