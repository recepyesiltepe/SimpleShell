#ifndef BUILTINS_H
#define BUILTINS_H

#include <stdbool.h>

#include "ast.h"

bool is_builtin(const Command *cmd);
int run_builtin_parent(Command *cmd, bool *should_exit);
int run_builtin_child(Command *cmd);

#endif
