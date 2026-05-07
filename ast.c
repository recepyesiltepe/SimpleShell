#include "ast.h"

#include <stdlib.h>

#include "memory.h"

void command_init(Command *cmd) {
    cmd->argc = 0;
    cmd->argv_capacity = 8;
    cmd->argv = xmalloc((size_t)cmd->argv_capacity * sizeof(char *));
    cmd->argv[0] = NULL;
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->append_output = false;
}

void command_add_arg(Command *cmd, const char *arg) {
    if (cmd->argc + 1 >= cmd->argv_capacity) {
        cmd->argv_capacity *= 2;
        cmd->argv = xrealloc(cmd->argv, (size_t)cmd->argv_capacity * sizeof(char *));
    }
    cmd->argv[cmd->argc++] = xstrdup(arg);
    cmd->argv[cmd->argc] = NULL;
}

void command_free(Command *cmd) {
    for (int i = 0; i < cmd->argc; i++) {
        free(cmd->argv[i]);
    }
    free(cmd->argv);
    free(cmd->input_file);
    free(cmd->output_file);

    cmd->argv = NULL;
    cmd->argc = 0;
    cmd->argv_capacity = 0;
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->append_output = false;
}

void pipeline_init(Pipeline *pipeline) {
    pipeline->count = 0;
    pipeline->capacity = 4;
    pipeline->commands = xmalloc((size_t)pipeline->capacity * sizeof(Command));
}

void pipeline_push_command(Pipeline *pipeline, Command *cmd) {
    if (pipeline->count == pipeline->capacity) {
        pipeline->capacity *= 2;
        pipeline->commands =
            xrealloc(pipeline->commands, (size_t)pipeline->capacity * sizeof(Command));
    }
    pipeline->commands[pipeline->count++] = *cmd;
}

void pipeline_free(Pipeline *pipeline) {
    for (int i = 0; i < pipeline->count; i++) {
        command_free(&pipeline->commands[i]);
    }
    free(pipeline->commands);
    pipeline->commands = NULL;
    pipeline->count = 0;
    pipeline->capacity = 0;
}

AstNode *ast_node_new_pipeline(Pipeline *pipeline) {
    AstNode *node = xmalloc(sizeof(AstNode));
    node->type = AST_NODE_PIPELINE;
    node->left = NULL;
    node->right = NULL;
    node->pipeline = *pipeline;
    return node;
}

AstNode *ast_node_new_binary(AstNodeType type, AstNode *left, AstNode *right) {
    AstNode *node = xmalloc(sizeof(AstNode));
    node->type = type;
    node->left = left;
    node->right = right;
    node->pipeline.commands = NULL;
    node->pipeline.count = 0;
    node->pipeline.capacity = 0;
    return node;
}

void ast_node_free(AstNode *node) {
    if (!node) {
        return;
    }
    ast_node_free(node->left);
    ast_node_free(node->right);
    if (node->type == AST_NODE_PIPELINE) {
        pipeline_free(&node->pipeline);
    }
    free(node);
}
