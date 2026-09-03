#ifndef RK_PARSE_H
#define RK_PARSE_H

#include "ast.h"
#include "lexer.h"

Program* parse_program(const char* src, int len, Token* toks, int ntoks);
const char* parse_error(void);

#endif
