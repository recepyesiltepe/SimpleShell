#ifndef EXECUTE_H
#define EXECUTE_H

#include <stdbool.h>

#include "ast.h"

int execute_ast(AstNode *node, bool *should_exit);

#endif
