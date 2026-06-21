#ifndef EXPANSION_H
#define EXPANSION_H

#include "ast.h"

int expand_word_to_command_args(Command *cmd, const char *word, int quoted);
char *expand_redirection_target(const char *word, int quoted);

#endif
