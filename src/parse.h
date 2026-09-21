#ifndef RK_PARSE_H
#define RK_PARSE_H

#include "ast.h"
#include "lexer.h"

#define RK_PARSE_MAX_DIAGS 64

typedef struct {
    int line;       /* 1-based, in original file coordinates */
    int col;        /* 1-based */
    int len;        /* token span in bytes (>=1) */
    char code[16];  /* e.g. "E0001" (syntax) */
    char file[512]; /* original file via @rk:src markers (may be "") */
    char msg[512];  /* short message without location prefix */
} ParseDiag;

Program* parse_program(const char* src, int len, Token* toks, int ntoks);
Program* parse_program_tolerant(const char* src, int len, Token* toks, int ntoks);
const char* parse_error(void);

/* Multi-error access for analyzer/LSP tooling.
   parse_error() stays as the first-error string for CLI compat. */
int parse_error_count(void);
const ParseDiag* parse_error_get(int idx);
void parse_error_clear(void);

#endif
