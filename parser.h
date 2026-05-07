#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "tokenizer.h"

/*
 * Returns:
 *   0 on success (out_root set)
 *   1 for empty input
 *  -1 on parse error
 */
int parse_tokens(TokenList *tokens, AstNode **out_root);

#endif
