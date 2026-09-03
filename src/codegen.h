#ifndef RK_CODEGEN_H
#define RK_CODEGEN_H

#include "ast.h"
#include "sema.h"

char* codegen_program(Sema* sema, Program* prog, int* out_len, int bounds_check);
char* codegen_header(Sema* sema, Program* prog, int* out_len, const char* mod_name);

#endif