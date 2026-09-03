#include "lexer.h"

#include <stdlib.h>
#include <string.h>

static int is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_ident_cont(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

typedef struct Lx {
    const char* src;
    int len;
    int pos;
    int line;
    int col;
    int bol;    /* next token starts its line */
} Lx;

static void adv(Lx* lx) {
    if (lx->pos < lx->len && lx->src[lx->pos] == '\n') {
        lx->line++;
        lx->col = 1;
        lx->bol = 1;
    } else {
        lx->col++;
    }
    lx->pos++;
}

static void skip_ws(Lx* lx) {
    while (lx->pos < lx->len) {
        char c = lx->src[lx->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            adv(lx);
        } else if (c == '/' && lx->pos + 1 < lx->len) {
            char n = lx->src[lx->pos + 1];
            if (n == '/') {
                while (lx->pos < lx->len && lx->src[lx->pos] != '\n') adv(lx);
            } else if (n == '*') {
                adv(lx);
                adv(lx);
                while (lx->pos + 1 < lx->len &&
                       !(lx->src[lx->pos] == '*' && lx->src[lx->pos + 1] == '/'))
                    adv(lx);
                if (lx->pos + 1 < lx->len) { adv(lx); adv(lx); }
            } else {
                return;
            }
        } else {
            return;
        }
    }
}

static void tok_begin(Token* t, Lx* lx) {
    t->text = lx->src + lx->pos;
    t->start = lx->pos;
    t->line = lx->line;
    t->col = lx->col;
    t->bol = lx->bol;
}

static void tok_end(Token* t, Lx* lx) {
    t->len = lx->pos - t->start;
    t->end = lx->pos;
    lx->bol = 0;
}

static Token lex_string(Lx* lx) {
    Token t;
    t.kind = TK_STRING;
    tok_begin(&t, lx);
    adv(lx); /* opening quote */
    while (lx->pos < lx->len) {
        char c = lx->src[lx->pos];
        if (c == '\\') {
            adv(lx);
            if (lx->pos < lx->len) adv(lx);
        } else if (c == '"') {
            adv(lx);
            break;
        } else if (c == '\n') {
            break;
        } else {
            adv(lx);
        }
    }
    tok_end(&t, lx);
    return t;
}

static Token lex_char(Lx* lx) {
    Token t;
    t.kind = TK_CHAR;
    tok_begin(&t, lx);
    adv(lx); /* opening quote */
    while (lx->pos < lx->len) {
        char c = lx->src[lx->pos];
        if (c == '\\') {
            adv(lx);
            if (lx->pos < lx->len) adv(lx);
        } else if (c == '\'') {
            adv(lx);
            break;
        } else if (c == '\n') {
            break;
        } else {
            adv(lx);
        }
    }
    tok_end(&t, lx);
    return t;
}

static Token lex_number(Lx* lx) {
    Token t;
    t.kind = TK_NUMBER;
    tok_begin(&t, lx);
    while (lx->pos < lx->len && is_digit(lx->src[lx->pos])) adv(lx);
    /* only consume '.' for float literals, not for range operators like ..= */
    if (lx->pos < lx->len && lx->src[lx->pos] == '.' &&
        lx->pos + 1 < lx->len && is_digit(lx->src[lx->pos + 1])) {
        adv(lx);
        while (lx->pos < lx->len && is_digit(lx->src[lx->pos])) adv(lx);
    }
    if (lx->pos < lx->len && (lx->src[lx->pos] == 'e' || lx->src[lx->pos] == 'E')) {
        int save = lx->pos;
        adv(lx);
        if (lx->pos < lx->len && (lx->src[lx->pos] == '+' || lx->src[lx->pos] == '-'))
            adv(lx);
        if (lx->pos < lx->len && is_digit(lx->src[lx->pos])) {
            while (lx->pos < lx->len && is_digit(lx->src[lx->pos])) adv(lx);
        } else {
            lx->pos = save;
        }
    }
    tok_end(&t, lx);
    return t;
}

static Token lex_ident(Lx* lx) {
    Token t;
    t.kind = TK_IDENT;
    tok_begin(&t, lx);
    while (lx->pos < lx->len && is_ident_cont(lx->src[lx->pos])) adv(lx);
    tok_end(&t, lx);
    return t;
}

typedef struct Punct {
    const char* text;
    int len;
} Punct;

static const Punct puncts[] = {
    {"..=", 3},
    {"<=", 2}, {">=", 2}, {"==", 2}, {"!=", 2},
    {"&&", 2}, {"||", 2}, {"<<", 2}, {">>", 2}, {"+=", 2}, {"-=", 2}, {"*=", 2}, {"/=", 2},
    {"%=", 2}, {"++", 2}, {"--", 2}, {"->", 2}, {"=>", 2}, {"..", 2},
    {"{", 1}, {"}", 1}, {"(", 1}, {")", 1}, {"[", 1}, {"]", 1},
    {";", 1}, {",", 1}, {".", 1}, {":", 1}, {"?", 1}, {"=", 1},
    {"<", 1}, {">", 1}, {"!", 1}, {"&", 1}, {"|", 1}, {"^", 1},
    {"~", 1}, {"+", 1}, {"-", 1}, {"*", 1}, {"/", 1}, {"%", 1}, {"#", 1},
};

static Token lex_punct(Lx* lx) {
    Token t;
    t.kind = TK_PUNCT;
    tok_begin(&t, lx);
    for (int i = 0; i < (int)(sizeof puncts / sizeof puncts[0]); i++) {
        int pl = puncts[i].len;
        if (lx->pos + pl <= lx->len && memcmp(lx->src + lx->pos, puncts[i].text, pl) == 0) {
            for (int j = 0; j < pl; j++) adv(lx);
            tok_end(&t, lx);
            return t;
        }
    }
    /* unknown byte: consume as single-char punct */
    adv(lx);
    tok_end(&t, lx);
    return t;
}

Token* lex_all(const char* src, int len, int* out_n) {
    Lx lx;
    lx.src = src;
    lx.len = len;
    lx.pos = 0;
    lx.line = 1;
    lx.col = 1;
    lx.bol = 1;

    Token* arr = NULL;
    int n = 0;

    while (lx.pos < lx.len) {
        skip_ws(&lx);
        if (lx.pos >= lx.len) break;
        char c = lx.src[lx.pos];
        Token t;
        if (c == '"') {
            t = lex_string(&lx);
        } else if (c == '\'') {
            t = lex_char(&lx);
        } else if (is_digit(c) || (c == '.' && lx.pos + 1 < lx.len &&
                                   is_digit(lx.src[lx.pos + 1]))) {
            t = lex_number(&lx);
        } else if (is_ident_start(c)) {
            t = lex_ident(&lx);
        } else {
            t = lex_punct(&lx);
        }
        arr = realloc(arr, (n + 1) * sizeof *arr);
        if (!arr) exit(1);
        arr[n++] = t;
    }

    Token eof;
    eof.kind = TK_EOF;
    eof.text = src + len;
    eof.len = 0;
    eof.start = len;
    eof.end = len;
    eof.line = lx.line;
    eof.col = lx.col;
    eof.bol = 0;
    arr = realloc(arr, (n + 1) * sizeof *arr);
    if (!arr) exit(1);
    arr[n++] = eof;

    *out_n = n;
    return arr;
}

void lex_free(Token* toks) {
    free(toks);
}
