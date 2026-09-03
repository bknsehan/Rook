#ifndef RK_LEXER_H
#define RK_LEXER_H

typedef enum { TK_IDENT, TK_NUMBER, TK_STRING, TK_CHAR, TK_PUNCT, TK_EOF } TokKind;

typedef struct Token {
    TokKind kind;
    const char* text;   /* lexeme, points into source */
    int len;
    int start;          /* byte offsets into source */
    int end;
    int line;           /* 1-based */
    int col;            /* 1-based */
    int bol;            /* first non-ws token on its line */
} Token;

Token* lex_all(const char* src, int len, int* out_n);
void lex_free(Token* toks);

#endif
