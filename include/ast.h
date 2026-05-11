#ifndef AST_H
#define AST_H

#include <stdbool.h>

typedef struct {
    char **argv;
    int argc;
    int argv_capacity;
    char *input_file;
    char *output_file;
    bool append_output;
} Command;

typedef struct {
    Command *commands;
    int count;
    int capacity;
} Pipeline;

typedef enum {
    AST_NODE_PIPELINE,
    AST_NODE_SEQUENCE,
    AST_NODE_AND,
    AST_NODE_OR,
    AST_NODE_BACKGROUND
} AstNodeType;

typedef struct AstNode {
    AstNodeType type;
    struct AstNode *left;
    struct AstNode *right;
    Pipeline pipeline;
} AstNode;

void command_init(Command *cmd);
void command_add_arg(Command *cmd, const char *arg);
void command_free(Command *cmd);

void pipeline_init(Pipeline *pipeline);
void pipeline_push_command(Pipeline *pipeline, Command *cmd);
void pipeline_free(Pipeline *pipeline);

AstNode *ast_node_new_pipeline(Pipeline *pipeline);
AstNode *ast_node_new_binary(AstNodeType type, AstNode *left, AstNode *right);
void ast_node_free(AstNode *node);

#endif
