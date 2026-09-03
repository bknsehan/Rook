#include "parse.h"
#include "diag.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_errbuf[1024];

typedef struct Parser {
    const char* src;
    int slen;
    Token* toks;
    int n;
    int idx;
    int depth;      /* brace depth while scanning raw regions */
    int last_end;   /* offset just past the last consumed token */
    int silent;     /* suppress errors (backtracking) */
    int no_postfix; /* switch case labels */
    int err;
} Parser;

static Token* cur(Parser* p) {
    return &p->toks[p->idx];
}

static Token* peek(Parser* p, int k) {
    int i = p->idx + k;
    if (i >= p->n) i = p->n - 1;
    if (i < 0) i = 0;
    return &p->toks[i];
}

static void adv(Parser* p) {
    p->last_end = p->toks[p->idx].end;
    p->idx++;
}

static int is_kw(Token* t, const char* kw) {
    return t->kind == TK_IDENT && (int)strlen(kw) == t->len &&
           memcmp(t->text, kw, t->len) == 0;
}

static int tok_is(Token* t, const char* s) {
    return t->kind == TK_PUNCT && (int)strlen(s) == t->len &&
           memcmp(t->text, s, t->len) == 0;
}

static void error_at(Parser* p, Token* t, const char* fmt, ...) {
    if (p->silent) return;
    p->err = 1;
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    diag_render(p->src, t->start, t->len < 1 ? 1 : t->len, "error", msg,
                g_errbuf, sizeof g_errbuf);
}

const char* parse_error(void) {
    return g_errbuf;
}

/* Attach source location (from a token) to a freshly built node so the
   semantic checker can emit source-accurate diagnostics. */
static Expr* e_at(Expr* e, Token* t) {
    if (e) { e->start = t->start; e->len = t->len; e->line = t->line; e->col = t->col; }
    return e;
}
static void e_inherit(Expr* e, Expr* from) {
    if (e && from) { e->start = from->start; e->len = from->len; e->line = from->line; e->col = from->col; }
}
static Stmt* s_at(Stmt* s, Token* t) {
    if (s) { s->start = t->start; s->len = t->len; s->line = t->line; s->col = t->col; }
    return s;
}

static char* tok_strdup(Token* t) {
    char* s = malloc(t->len + 1);
    if (!s) exit(1);
    memcpy(s, t->text, t->len);
    s[t->len] = '\0';
    return s;
}

static AstType* parse_type(Parser* p);
static Expr* parse_expr(Parser* p);
static Expr* parse_assignment(Parser* p);
static Expr* parse_ternary(Parser* p);
static Expr* parse_or(Parser* p);
static Expr* parse_and(Parser* p);
static Expr* parse_eq(Parser* p);
static Expr* parse_rel(Parser* p);
static Expr* parse_add(Parser* p);
static Expr* parse_mul(Parser* p);
static Expr* parse_unary(Parser* p);
static Expr* parse_postfix(Parser* p);
static Expr* parse_primary(Parser* p);
static Expr* parse_paren_or_cast(Parser* p);
static Expr* parse_init(Parser* p);
static Stmt* parse_stmt(Parser* p);
static Stmt* parse_block(Parser* p);
static Stmt* parse_if(Parser* p);
static Stmt* parse_for(Parser* p);
static Stmt* parse_switch(Parser* p);
static Expr* parse_match(Parser* p);
static FnDef* parse_fn_def(Parser* p);
static Decl* parse_typed_decl(Parser* p, int expect_semi);
static Decl* parse_c_decl(Parser* p, int expect_semi);

static int expect_punct(Parser* p, const char* s) {
    if (!tok_is(cur(p), s)) {
        error_at(p, cur(p), "expected '%s'", s);
        return 0;
    }
    adv(p);
    return 1;
}

static int expect_kw(Parser* p, const char* kw) {
    if (!is_kw(cur(p), kw)) {
        error_at(p, cur(p), "expected '%s'", kw);
        return 0;
    }
    adv(p);
    return 1;
}

static char* ident(Parser* p) {
    Token* t = cur(p);
    if (t->kind != TK_IDENT) {
        error_at(p, t, "expected identifier");
        return NULL;
    }
    char* s = tok_strdup(t);
    adv(p);
    return s;
}

/* ------------------------------ types ------------------------------ */

static int is_qual_word(Token* t) {
    return is_kw(t, "const") || is_kw(t, "unsigned") || is_kw(t, "signed") ||
           is_kw(t, "short") || is_kw(t, "long");
}

static AstType* parse_type(Parser* p) {
    AstType* t = ast_type_new();
    Token* tok = cur(p);
    if (is_kw(tok, "dyn")) {
        error_at(p, tok, "'dyn' trait objects are not supported");
        free(t);
        return NULL;
    }
    if (tok_is(tok, "*")) {
        error_at(p, tok, "invalid pointer syntax '*Type'; Rook standardizes on postfix 'Type*' (e.g. 'int*')");
        free(t);
        return NULL;
    }
    if (tok->kind != TK_IDENT) {
        error_at(p, tok, "expected type");
        free(t);
        return NULL;
    }
    while (is_qual_word(tok)) {
        int len = t->qual ? (int)strlen(t->qual) : 0;
        t->qual = realloc(t->qual, len + tok->len + 2);
        if (!t->qual) exit(1);
        memcpy(t->qual + len, tok->text, tok->len);
        t->qual[len + tok->len] = ' ';
        t->qual[len + tok->len + 1] = '\0';
        adv(p);
        tok = cur(p);
        if (tok->kind != TK_IDENT) {
            error_at(p, tok, "expected type after qualifier");
            return NULL;
        }
    }
    t->name = tok_strdup(tok);
    adv(p);
    while (tok_is(cur(p), "*")) {
        adv(p);
        t->ptrs++;
    }
    return t;
}

static AstType* try_parse_type(Parser* p) {
    int save = p->idx;
    int saved_silent = p->silent;
    p->silent = 1;
    AstType* t = parse_type(p);
    if (!t) p->idx = save;
    p->silent = saved_silent;
    return t;
}

/* ------------------------------ exprs ------------------------------ */

static Expr* expr_bin(Expr* lhs, const char* op, Expr* rhs) {
    Expr* e = ast_expr_new(E_BINARY);
    e_inherit(e, lhs);
    e->str = (char*)op;   /* op points into the static ops[] table, read-only */
    e->a = lhs;
    e->b = rhs;
    return e;
}

static Expr* parse_binop(Parser* p, Expr* (*next)(Parser*), const char** ops, int nops) {
    Expr* e = next(p);
    for (;;) {
        Token* t = cur(p);
        int match = -1;
        if (t->kind == TK_PUNCT) {
            for (int i = 0; i < nops; i++) {
                if (tok_is(t, ops[i])) { match = i; break; }
            }
        }
        if (match < 0) return e;
        adv(p);
        e = expr_bin(e, ops[match], next(p));
    }
}

static Expr* parse_or(Parser* p) {
    static const char* ops[] = {"||"};
    return parse_binop(p, parse_and, ops, 1);
}

static Expr* parse_and(Parser* p) {
    static const char* ops[] = {"&&"};
    return parse_binop(p, parse_eq, ops, 1);
}

static Expr* parse_eq(Parser* p) {
    static const char* ops[] = {"==", "!="};
    return parse_binop(p, parse_rel, ops, 2);
}

static Expr* parse_rel(Parser* p) {
    static const char* ops[] = {"<", ">", "<=", ">="};
    return parse_binop(p, parse_add, ops, 4);
}

static Expr* parse_add(Parser* p) {
    static const char* ops[] = {"+", "-"};
    return parse_binop(p, parse_mul, ops, 2);
}

static Expr* parse_mul(Parser* p) {
    static const char* ops[] = {"*", "/", "%"};
    return parse_binop(p, parse_unary, ops, 3);
}

static int is_primary_start(Token* t) {
    if (t->kind == TK_IDENT || t->kind == TK_NUMBER || t->kind == TK_STRING ||
        t->kind == TK_CHAR)
        return 1;
    if (t->kind == TK_PUNCT) {
        return tok_is(t, "(") || tok_is(t, "-") || tok_is(t, "!") ||
               tok_is(t, "*") || tok_is(t, "&");
    }
    return 0;
}

static Expr* parse_ternary(Parser* p) {
    return parse_or(p);
}

static int is_assign_op(Token* t) {
    return t->kind == TK_PUNCT && (tok_is(t, "=") || tok_is(t, "+=") || tok_is(t, "-=") ||
                                   tok_is(t, "*=") || tok_is(t, "/=") || tok_is(t, "%="));
}

static Expr* parse_assignment(Parser* p) {
    Expr* lhs = parse_ternary(p);
    Token* t = cur(p);
    if (is_assign_op(t)) {
        char* op = tok_strdup(t);
        adv(p);
        Expr* rhs = parse_assignment(p);
        if (!rhs) return NULL;
        Expr* e = ast_expr_new(E_ASSIGN);
        e_inherit(e, lhs);
        e->str = op;
        e->a = lhs;
        e->b = rhs;
        return e;
    }
    return lhs;
}

static Expr* parse_expr(Parser* p) {
    return parse_assignment(p);
}

static Expr* parse_unary(Parser* p) {
    Token* t = cur(p);
    if (is_kw(t, "sizeof")) {
        adv(p);
        if (!expect_punct(p, "(")) return NULL;
        AstType* ty = try_parse_type(p);
        if (ty && tok_is(cur(p), ")")) {
            adv(p);
            Expr* e = e_at(ast_expr_new(E_SIZEOF_T), t);
            e->type = ty;
            return e;
        }
        free(ty);
        Expr* inner = parse_expr(p);
        if (!inner) return NULL;
        if (!expect_punct(p, ")")) return NULL;
        Expr* e = e_at(ast_expr_new(E_SIZEOF_E), t);
        e->a = inner;
        return e;
    }
    if (t->kind == TK_PUNCT) {
        if (tok_is(t, "(")) return parse_paren_or_cast(p);
        if (tok_is(t, "-") || tok_is(t, "!") || tok_is(t, "*") || tok_is(t, "&") ||
            tok_is(t, "++") || tok_is(t, "--")) {
            char* op = tok_strdup(t);
            adv(p);
            Expr* a = parse_unary(p);
            if (!a) return NULL;
            Expr* e = e_at(ast_expr_new(E_UNARY), t);
            e->str = op;
            e->a = a;
            return e;
        }
    }
    return parse_postfix(p);
}

static Expr* parse_paren_or_cast(Parser* p) {
    Token* lp = cur(p);
    int save = p->idx;
    adv(p); /* ( */
    AstType* ty = try_parse_type(p);
    if (ty && tok_is(cur(p), ")")) {
        int save2 = p->idx;
        adv(p); /* ) */
        Token* nt = cur(p);
        if (tok_is(nt, "{")) {
            adv(p);
                Expr* e = e_at(ast_expr_new(E_COMPOUND), lp);
                e->type = ty;
            while (!tok_is(cur(p), "}")) {
                CompItem item;
                item.name = NULL;
                item.e = NULL;
                if (tok_is(cur(p), ".")) {
                    adv(p);
                    item.name = ident(p);
                    if (!item.name) return NULL;
                    if (!expect_punct(p, "=")) return NULL;
                }
                item.e = parse_expr(p);
                if (!item.e) return NULL;
                e->citems = realloc(e->citems, (e->ncitems + 1) * sizeof *e->citems);
                if (!e->citems) exit(1);
                e->citems[e->ncitems++] = item;
                if (tok_is(cur(p), ",")) {
                    adv(p);
                    continue;
                }
                break;
            }
            if (!expect_punct(p, "}")) return NULL;
            return e;
        }
        if (is_primary_start(nt)) {
            Expr* operand = parse_unary(p);
            if (!operand) return NULL;
            Expr* e = ast_expr_new(E_CAST);
            e->type = ty;
            e->a = operand;
            e_inherit(e, operand);
            return e;
        }
        p->idx = save2;
        free(ty);
    } else {
        free(ty);
    }
    p->idx = save;
    adv(p); /* ( */
    Expr* inner = parse_expr(p);
    if (!inner) return NULL;
    if (!expect_punct(p, ")")) return NULL;
    Expr* e = ast_expr_new(E_PAREN);
    e->a = inner;
    e_inherit(e, inner);
    return e;
}

static Expr* parse_postfix(Parser* p) {
    Expr* e = parse_primary(p);
    if (!e) return NULL;
    if (p->no_postfix) return e;
    for (;;) {
        Token* t = cur(p);
        if (t->kind != TK_PUNCT) return e;
        if (tok_is(t, ".") || tok_is(t, "->")) {
            ExprKind k = t->len == 2 && memcmp(t->text, "->", 2) == 0 ? E_ARROW : E_MEMBER;
            Token* mt = cur(p);
            adv(p);
            char* name = ident(p);
            if (!name) return NULL;
            Expr* m = e_at(ast_expr_new(k), mt);
            m->str = name;
            m->a = e;
            e = m;
        } else if (tok_is(t, "(")) {
            adv(p);
            Expr* call = ast_expr_new(E_CALL);
            call->a = e;
            e_inherit(call, e);
            while (!tok_is(cur(p), ")")) {
                Expr* arg = parse_expr(p);
                if (!arg) return NULL;
                call->items = realloc(call->items, (call->nitems + 1) * sizeof *call->items);
                if (!call->items) exit(1);
                call->items[call->nitems++] = arg;
                if (tok_is(cur(p), ",")) {
                    adv(p);
                    continue;
                }
                break;
            }
            if (!expect_punct(p, ")")) return NULL;
            e = call;
        } else if (tok_is(t, "[")) {
            adv(p);
            Expr* idx = parse_expr(p);
            if (!idx) return NULL;
            if (!expect_punct(p, "]")) return NULL;
            Expr* in = ast_expr_new(E_INDEX);
            in->a = e;
            in->b = idx;
            e_inherit(in, e);
            e = in;
        } else if (tok_is(t, "++") || tok_is(t, "--")) {
            char* op = tok_strdup(t);
            adv(p);
            Expr* po = e_at(ast_expr_new(E_POST), t);
            po->str = op;
            po->a = e;
            e = po;
        } else if (tok_is(t, "?")) {
            Token* qt = cur(p);
            adv(p);
            Expr* q = e_at(ast_expr_new(E_QUESTION), qt);
            q->a = e;
            e_inherit(q, e);
            e = q;
        } else {
            return e;
        }
    }
}

static Expr* parse_primary(Parser* p) {
    Token* t = cur(p);
    if (is_kw(t, "match")) return parse_match(p);
    switch (t->kind) {
    case TK_NUMBER:
    case TK_STRING:
    case TK_CHAR: {
        Expr* e = e_at(ast_expr_new(E_LITERAL), t);
        e->str = tok_strdup(t);
        adv(p);
        return e;
    }
    case TK_IDENT: {
        int save = p->idx;
        char* name = tok_strdup(t);
        adv(p);
        {
            /* Probe whether `name` is followed by a generic suffix and a `{`,
               i.e. a type constructor `Name<T>{...}` / `Name!T{...}`. */
            int save2 = save;
            p->idx = save2; /* rewind to the ident before probing as a type */
            AstType* probe = try_parse_type(p);
            int is_cons = (probe && tok_is(cur(p), "{"));
            if (probe) ast_type_free(probe);
            p->idx = save2;
            if (is_cons) {
                AstType* ty = try_parse_type(p);
                adv(p);
                Expr* e = e_at(ast_expr_new(E_NAMED_INIT), t);
                e->type = ty;
                while (!tok_is(cur(p), "}")) {
                    char* fname = ident(p);
                    if (!fname) return NULL;
                    Expr* fe = NULL;
                    if (tok_is(cur(p), ":")) {
                        adv(p);
                        fe = parse_expr(p);
                    } else {
                        /* shorthand: `field` means `field: field` (for match patterns) */
                        fe = ast_expr_new(E_IDENT);
                        fe->str = fname;
                    }
                    if (!fe) return NULL;
                    e->nfields = realloc(e->nfields, (e->nnfields + 1) * sizeof *e->nfields);
                    if (!e->nfields) exit(1);
                    e->nfields[e->nnfields].name = fname;
                    e->nfields[e->nnfields].e = fe;
                    e->nnfields++;
                    if (tok_is(cur(p), ",")) {
                        adv(p);
                        continue;
                    }
                    break;
                }
                if (!expect_punct(p, "}")) return NULL;
                return e;
            }
        }
        adv(p); /* probe rewound to the ident start; consume it now */
        Expr* e = e_at(ast_expr_new(E_IDENT), t);
        e->str = name;
        return e;
    }
    case TK_PUNCT:
        if (tok_is(t, "[")) {
            adv(p);
            Expr* e = e_at(ast_expr_new(E_ARR_LIT), t);
            while (!tok_is(cur(p), "]")) {
                Expr* item = parse_expr(p);
                if (!item) return NULL;
                e->items = realloc(e->items, (e->nitems + 1) * sizeof *e->items);
                if (!e->items) exit(1);
                e->items[e->nitems++] = item;
                if (tok_is(cur(p), ",")) {
                    adv(p);
                    continue;
                }
                break;
            }
            if (!expect_punct(p, "]")) return NULL;
            return e;
        }
        if (tok_is(t, "(")) return parse_paren_or_cast(p);
        error_at(p, t, "unexpected '%s' in expression", t->text);
        return NULL;
    default:
        error_at(p, t, "unexpected token in expression");
        return NULL;
    }
}

/* ------------------------------ decls ------------------------------ */

static Expr* parse_init(Parser* p) {
    Token* t0 = cur(p);
    if (tok_is(cur(p), "{")) {
        adv(p);
        Expr* e = e_at(ast_expr_new(E_BRACE_INIT), t0);
        while (!tok_is(cur(p), "}")) {
            Expr* item = parse_expr(p);
            if (!item) return NULL;
            e->items = realloc(e->items, (e->nitems + 1) * sizeof *e->items);
            if (!e->items) exit(1);
            e->items[e->nitems++] = item;
            if (tok_is(cur(p), ",")) {
                adv(p);
                continue;
            }
            break;
        }
        if (!expect_punct(p, "}")) return NULL;
        return e;
    }
    return parse_expr(p);
}

static Decl* parse_typed_decl(Parser* p, int expect_semi) {
    Token* t0 = cur(p);
    char* name = ident(p);
    if (!name) return NULL;
    if (!expect_punct(p, ":")) return NULL;
    AstType* ty = parse_type(p);
    if (!ty) return NULL;
    Expr* init = NULL;
    if (tok_is(cur(p), "=")) {
        adv(p);
        init = parse_init(p);
        if (!init) return NULL;
    }
    if (expect_semi && !expect_punct(p, ";")) return NULL;
    Decl* d = ast_decl_new(DECL_TYPED, name, ty, init);
    if (t0) { d->start = t0->start; d->len = t0->len; d->line = t0->line; d->col = t0->col; }
    return d;
}

static Decl* parse_c_decl(Parser* p, int expect_semi) {
    Token* t0 = cur(p);
    AstType* ty = parse_type(p);
    if (!ty) return NULL;
    char* name = ident(p);
    if (!name) return NULL;
    Expr* dim = NULL;
    if (tok_is(cur(p), "[")) {
        adv(p);
        dim = parse_expr(p);
        if (!dim) return NULL;
        if (!expect_punct(p, "]")) return NULL;
    }
    Expr* init = NULL;
    if (tok_is(cur(p), "=")) {
        adv(p);
        init = parse_init(p);
        if (!init) return NULL;
    }
    if (expect_semi && !expect_punct(p, ";")) return NULL;
    Decl* d = ast_decl_new(DECL_C, name, ty, init);
    d->dim = dim;
    if (t0) { d->start = t0->start; d->len = t0->len; d->line = t0->line; d->col = t0->col; }
    return d;
}

/* ------------------------------ stmts ------------------------------ */

static int is_expr_cont(Token* t) {
    if (t->kind != TK_PUNCT) return 0;
    return tok_is(t, ".") || tok_is(t, "->") || tok_is(t, "(") || tok_is(t, "[") ||
           tok_is(t, "=") || tok_is(t, "+=") || tok_is(t, "-=") || tok_is(t, "*=") ||
           tok_is(t, "/=") || tok_is(t, "%=") || tok_is(t, "++") || tok_is(t, "--");
}

static Stmt* stmt_expr(Expr* e) {
    Stmt* s = ast_stmt_new(S_EXPR);
    s->e = e;
    if (e) { s->start = e->start; s->len = e->len; s->line = e->line; s->col = e->col; }
    return s;
}

static Stmt* stmt_decl(Decl* d) {
    Stmt* s = ast_stmt_new(S_DECL);
    s->decl = d;
    if (d) { s->start = d->start; s->len = d->len; s->line = d->line; s->col = d->col; }
    return s;
}

static Stmt* parse_stmt(Parser* p) {
    Token* t = cur(p);
    if (tok_is(t, "{")) return parse_block(p);
    /* Rook rejects goto in source code. */
    if (is_kw(t, "goto")) {
        error_at(p, t, "'goto' is not supported in Rook");
        return NULL;
    }
    /* `let name = expr;` desugars to typed-decl with auto-inferred type */
    if (is_kw(t, "let")) {
        adv(p);
        char* name = ident(p);
        if (!name) return NULL;
        AstType* ty = NULL;
        Expr* init = NULL;
        if (tok_is(cur(p), ":")) {
            adv(p);
            ty = parse_type(p);
            if (!ty) return NULL;
        }
        if (tok_is(cur(p), "=")) {
            adv(p);
            init = parse_init(p);
            if (!init) return NULL;
        }
        if (!expect_punct(p, ";")) return NULL;
        Decl* d = ast_decl_new(DECL_LET, name, ty, init);
        d->start = t->start; d->len = t->len; d->line = t->line; d->col = t->col;
        return stmt_decl(d);
    }
    if (is_kw(t, "if")) return parse_if(p);
    if (is_kw(t, "while")) {
        adv(p);
        if (!expect_punct(p, "(")) return NULL;
        Expr* cond = parse_expr(p);
        if (!cond) return NULL;
        if (!expect_punct(p, ")")) return NULL;
        Stmt* body = parse_stmt(p);
        if (!body) return NULL;
        Stmt* s = ast_stmt_new(S_WHILE);
        s->cond = cond;
        s->body = body;
        return s;
    }
    if (is_kw(t, "for")) return parse_for(p);
    if (is_kw(t, "switch")) return parse_switch(p);
    if (is_kw(t, "match")) return stmt_expr(parse_match(p));
    if (is_kw(t, "return")) {
        adv(p);
        Stmt* s = ast_stmt_new(S_RETURN);
        if (!tok_is(cur(p), ";")) {
            s->e = parse_expr(p);
            if (!s->e) return NULL;
        }
        if (!expect_punct(p, ";")) return NULL;
        return s;
    }
    if (is_kw(t, "break")) {
        adv(p);
        if (!expect_punct(p, ";")) return NULL;
        return ast_stmt_new(S_BREAK);
    }
    if (is_kw(t, "continue")) {
        adv(p);
        if (!expect_punct(p, ";")) return NULL;
        return ast_stmt_new(S_CONTINUE);
    }
    if (is_kw(t, "defer")) {
        adv(p);
        Stmt* body = parse_stmt(p);
        if (!body) return NULL;
        Stmt* s = ast_stmt_new(S_DEFER);
        s->defer = body;
        s->start = t->start; s->len = t->len; s->line = t->line; s->col = t->col;
        return s;
    }
    if (tok_is(t, ";")) {
        adv(p);
        return ast_stmt_new(S_EMPTY);
    }
    if (t->kind == TK_IDENT) {
        Token* n = peek(p, 1);
        if (tok_is(n, ":")) {
            Decl* d = parse_typed_decl(p, 1);
            return d ? stmt_decl(d) : NULL;
        }
        if (is_expr_cont(n)) {
            Expr* e = parse_expr(p);
            if (!e) return NULL;
            if (!expect_punct(p, ";")) return NULL;
            return stmt_expr(e);
        }
        Decl* d = parse_c_decl(p, 1);
        return d ? stmt_decl(d) : NULL;
    }
    if (t->kind == TK_PUNCT && is_primary_start(t)) {
        Expr* e = parse_expr(p);
        if (!e) return NULL;
        if (!expect_punct(p, ";")) return NULL;
        return stmt_expr(e);
    }
    error_at(p, t, "unexpected '%s' in statement", t->text);
    return NULL;
}

static Stmt* parse_block(Parser* p) {
    if (!expect_punct(p, "{")) return NULL;
    Stmt* b = ast_stmt_new(S_BLOCK);
    while (!tok_is(cur(p), "}")) {
        Stmt* s = parse_stmt(p);
        if (!s) return NULL;
        b->stmts = realloc(b->stmts, (b->nstmts + 1) * sizeof *b->stmts);
        if (!b->stmts) exit(1);
        b->stmts[b->nstmts++] = s;
    }
    if (!expect_punct(p, "}")) return NULL;
    return b;
}

static Stmt* parse_if(Parser* p) {
    if (!expect_kw(p, "if")) return NULL;
    if (!expect_punct(p, "(")) return NULL;
    Expr* cond = parse_expr(p);
    if (!cond) return NULL;
    if (!expect_punct(p, ")")) return NULL;
    Stmt* then = parse_stmt(p);
    if (!then) return NULL;
    Stmt* els = NULL;
    if (is_kw(cur(p), "else")) {
        adv(p);
        if (is_kw(cur(p), "if")) {
            els = parse_if(p);
        } else {
            els = parse_stmt(p);
        }
        if (!els) return NULL;
    }
    Stmt* s = ast_stmt_new(S_IF);
    s->cond = cond;
    s->then = then;
    s->els = els;
    return s;
}

static Stmt* parse_for(Parser* p) {
    if (!expect_kw(p, "for")) return NULL;
    if (cur(p)->kind == TK_IDENT && is_kw(peek(p, 1), "in")) {
        char* var = ident(p);
        if (!var) return NULL;
        adv(p); /* in */
        Expr* iter = parse_expr(p);
        if (!iter) return NULL;
        Stmt* body = parse_stmt(p);
        if (!body) return NULL;
        Stmt* s = ast_stmt_new(S_FORIN);
        s->var = var;
        s->iter = iter;
        s->body = body;
        return s;
    }
    if (!expect_punct(p, "(")) return NULL;
    Stmt* s = ast_stmt_new(S_FOR);
    Token* t = cur(p);
    if (t->kind == TK_IDENT) {
        Token* n = peek(p, 1);
        if (tok_is(n, ":")) {
            s->init_decl = parse_typed_decl(p, 0);
            if (!s->init_decl) return NULL;
        } else if (is_expr_cont(n)) {
            s->init_expr = parse_expr(p);
            if (!s->init_expr) return NULL;
        } else {
            s->init_decl = parse_c_decl(p, 0);
            if (!s->init_decl) return NULL;
        }
    } else {
        s->init_expr = parse_expr(p);
        if (!s->init_expr) return NULL;
    }
    if (!expect_punct(p, ";")) return NULL;
    s->cond = parse_expr(p);
    if (!s->cond) return NULL;
    if (!expect_punct(p, ";")) return NULL;
    s->step = parse_expr(p);
    if (!s->step) return NULL;
    if (!expect_punct(p, ")")) return NULL;
    s->body = parse_stmt(p);
    if (!s->body) return NULL;
    return s;
}

static Stmt* parse_switch_colon_body(Parser* p) {
    Stmt* b = ast_stmt_new(S_BLOCK);
    while (!tok_is(cur(p), "}") && !is_kw(cur(p), "case") && !is_kw(cur(p), "default") &&
           !is_kw(cur(p), "else")) {
        Stmt* s = parse_stmt(p);
        if (!s) return NULL;
        b->stmts = realloc(b->stmts, (b->nstmts + 1) * sizeof *b->stmts);
        if (!b->stmts) exit(1);
        b->stmts[b->nstmts++] = s;
    }
    return b;
}

static Stmt* parse_switch(Parser* p) {
    if (!expect_kw(p, "switch")) return NULL;
    if (!expect_punct(p, "(")) return NULL;
    Expr* e = parse_expr(p);
    if (!e) return NULL;
    if (!expect_punct(p, ")")) return NULL;
    if (!expect_punct(p, "{")) return NULL;
    Stmt* s = ast_stmt_new(S_SWITCH);
    s->e = e;
    while (!tok_is(cur(p), "}")) {
        SwitchArm arm;
        memset(&arm, 0, sizeof arm);
        if (is_kw(cur(p), "case")) {
            adv(p);
            int saved_postfix = p->no_postfix;
            p->no_postfix = 1;
            for (;;) {
                Expr* label = parse_expr(p);
                if (!label) return NULL;
                arm.labels = realloc(arm.labels, (arm.nlabels + 1) * sizeof *arm.labels);
                if (!arm.labels) exit(1);
                arm.labels[arm.nlabels++] = label;
                if (is_kw(cur(p), "case")) {
                    adv(p);
                    continue;
                }
                break;
            }
            p->no_postfix = saved_postfix;
            if (tok_is(cur(p), ":")) {
                adv(p);
                arm.body = parse_switch_colon_body(p);
                if (!arm.body) return NULL;
            } else if (tok_is(cur(p), "->")) {
                adv(p);
                arm.arrow = 1;
                arm.body = parse_stmt(p);
                if (!arm.body) return NULL;
            } else {
                error_at(p, cur(p), "expected ':' or '->' after case label");
                return NULL;
            }
        } else if (is_kw(cur(p), "default")) {
            adv(p);
            if (!expect_punct(p, ":")) return NULL;
            arm.is_default = 1;
            arm.body = parse_switch_colon_body(p);
            if (!arm.body) return NULL;
        } else if (is_kw(cur(p), "else")) {
            adv(p);
            if (!expect_punct(p, "->")) return NULL;
            arm.is_default = 1;
            arm.arrow = 1;
            arm.body = parse_stmt(p);
            if (!arm.body) return NULL;
        } else {
            error_at(p, cur(p), "expected 'case', 'default' or 'else' in switch");
            return NULL;
        }
        s->arms = realloc(s->arms, (s->narms + 1) * sizeof *s->arms);
        if (!s->arms) exit(1);
        s->arms[s->narms++] = arm;
    }
    if (!expect_punct(p, "}")) return NULL;
    return s;
}

static Expr* parse_pattern(Parser* p) {
    Expr* lo = parse_expr(p);
    if (!lo) return NULL;
    if (tok_is(cur(p), "..=")) {
        adv(p);
        Expr* hi = parse_expr(p);
        if (!hi) return NULL;
        Expr* r = ast_expr_new(E_RANGE);
        r->a = lo;
        r->b = hi;
        return r;
    }
    return lo;
}

static Expr* parse_match(Parser* p) {
    if (!expect_kw(p, "match")) return NULL;
    if (!expect_punct(p, "(")) return NULL;
    Expr* scrut = parse_expr(p);
    if (!scrut) return NULL;
    if (!expect_punct(p, ")")) return NULL;
    if (!expect_punct(p, "{")) return NULL;
    Expr* x = ast_expr_new(E_MATCH);
    x->a = scrut;
    while (!tok_is(cur(p), "}")) {
        MatchArm arm;
        memset(&arm, 0, sizeof arm);
        arm.pattern = parse_pattern(p);
        if (!arm.pattern) return NULL;
        if (!expect_punct(p, "=>")) return NULL;
        arm.body = parse_expr(p);
        if (!arm.body) return NULL;
        x->marms = realloc(x->marms, (x->nmarms + 1) * sizeof *x->marms);
        if (!x->marms) exit(1);
        x->marms[x->nmarms++] = arm;
        if (tok_is(cur(p), ",")) {
            adv(p);
            continue;
        }
        break;
    }
    if (!expect_punct(p, "}")) return NULL;
    return x;
}

/* --------------------------- top-level items --------------------------- */

static FnDef* parse_fn_def(Parser* p) {
    FnDef* f = calloc(1, sizeof *f);
    if (!f) exit(1);
    /* C-style: return type written first. No `fn` keyword. */
    f->ret = parse_type(p);
    if (!f->ret) return NULL;
    Token* name_tok = cur(p);
    f->name = ident(p);
    if (!f->name) return NULL;
    if (name_tok) { f->line = name_tok->line; f->col = name_tok->col; }
    if (!expect_punct(p, "(")) return NULL;
    while (!tok_is(cur(p), ")")) {
        Param param;
        memset(&param, 0, sizeof param);
        Token* a = cur(p);
        Token* b = peek(p, 1);
        if (is_kw(a, "self") &&
                   (tok_is(b, ",") || tok_is(b, ")"))) {
            /* bare `self` parameter (no type annotation) */
            param.name = ident(p);
            param.type = NULL;
        } else {
            /* C-style `type name` parameter form */
            AstType* ty = parse_type(p);
            if (!ty) return NULL;
            char* nm = ident(p);
            if (!nm) return NULL;
            param.name = nm;
            param.type = ty;
        }
        f->params = realloc(f->params, (f->nparams + 1) * sizeof *f->params);
        if (!f->params) exit(1);
        f->params[f->nparams++] = param;
        if (tok_is(cur(p), ",")) {
            adv(p);
            continue;
        }
        break;
    }
    if (!expect_punct(p, ")")) return NULL;
    if (tok_is(cur(p), ";")) { adv(p); }
    else if (tok_is(cur(p), "{")) { f->body = parse_block(p); }
    else { error_at(p, cur(p), "expected function body '{' or ';'"); return NULL; }
    if (!f->body) f->is_extern = 1; /* signature-only = extern declaration */
    return f;
}

/* Parse a dotted name like `gimmick.ui` (each segment is an identifier). */
static char* parse_dotted_name(Parser* p) {
    char* first = ident(p);
    if (!first) return NULL;
    size_t cap = strlen(first) + 1;
    char* out = malloc(cap);
    if (!out) exit(1);
    memcpy(out, first, cap);
    free(first);
    while (tok_is(cur(p), ".")) {
        adv(p);
        char* part = ident(p);
        if (!part) { free(out); return NULL; }
        size_t need = strlen(out) + 1 + strlen(part) + 1;
        out = realloc(out, need);
        if (!out) exit(1);
        strcat(out, ".");
        strcat(out, part);
        free(part);
    }
    return out;
}

/* Lookahead: is the current top-level position the start of a D-style
   free function `Type name ( ... )`? Used by the top-level dispatcher so it
   parses such functions instead of treating them as raw C. */
static int looks_like_dstyle_fn(Parser* p) {
    int save = p->idx;
    AstType* ty = try_parse_type(p);
    if (!ty) { p->idx = save; return 0; }
    int ok = (cur(p)->kind == TK_IDENT && tok_is(peek(p, 1), "("));
    ast_type_free(ty);
    p->idx = save;
    return ok;
}

/* Parse `sum Name { Variant { field: type; }; Unit; }` into a TOP_ENUM item.
   Variants may be unit (`A`) or carry named payload fields (`B { r: float; }`).
   Positional `B(type)` payloads are rejected (use named fields or C enums). */
static EnumDef* parse_sum_def(Parser* p, int is_c_enum) {
    EnumDef* ed = calloc(1, sizeof *ed);
    if (!ed) exit(1);
    ed->is_c_enum = is_c_enum;
    Token* name_tok = cur(p);
    ed->name = ident(p);
    if (!ed->name) return NULL;
    if (name_tok) { ed->line = name_tok->line; ed->col = name_tok->col; }
    if (!expect_punct(p, "{")) return NULL;
    while (!tok_is(cur(p), "}")) {
        EnumVariant v;
        memset(&v, 0, sizeof v);
        Token* vtok = cur(p);
        v.name = ident(p);
        if (!v.name) { error_at(p, cur(p), "expected variant name in sum"); return NULL; }
        if (vtok) { v.line = vtok->line; v.col = vtok->col; }
        if (tok_is(cur(p), "{")) {
            if (is_c_enum) {
                error_at(p, cur(p),
                    "C-style 'enum' cannot carry payload fields; use 'sum Name { Variant { field: type } }'");
                return NULL;
            }
            adv(p);
            while (!tok_is(cur(p), "}")) {
                StructField sf;
                memset(&sf, 0, sizeof sf);
                sf.style = FIELD_YUP;
                sf.name = ident(p);
                if (!sf.name) {
                    error_at(p, cur(p), "expected field name in variant %s", v.name);
                    return NULL;
                }
                if (!expect_punct(p, ":")) return NULL;
                sf.type = parse_type(p);
                if (!sf.type) return NULL;
                if (!expect_punct(p, ";")) return NULL;
                v.fields = realloc(v.fields, (v.nfields + 1) * sizeof *v.fields);
                if (!v.fields) exit(1);
                v.fields[v.nfields++] = sf;
            }
            if (!expect_punct(p, "}")) return NULL;
        } else if (tok_is(cur(p), "(")) {
            error_at(p, cur(p), "variant %s uses positional payload; use named fields: { field: type }", v.name);
            return NULL;
        }
        ed->variants = realloc(ed->variants, (ed->nvariants + 1) * sizeof *ed->variants);
        if (!ed->variants) exit(1);
        ed->variants[ed->nvariants++] = v;
        if (tok_is(cur(p), ",") || tok_is(cur(p), ";")) { adv(p); continue; }
        break;
    }
    if (!expect_punct(p, "}")) return NULL;
    return ed;
}



static FnDef* parse_extern_fn(Parser* p) {
    /* C-style: `extern int puts(const char* s);` — no `fn` keyword. */
    FnDef* f = calloc(1, sizeof *f);
    if (!f) exit(1);
    f->is_extern = 1;
    f->ret = parse_type(p);
    if (!f->ret) return NULL;
    Token* name_tok = cur(p);
    f->name = ident(p);
    if (!f->name) return NULL;
    if (name_tok) { f->line = name_tok->line; f->col = name_tok->col; }
    if (!expect_punct(p, "(")) return NULL;
    while (!tok_is(cur(p), ")")) {
        Param param;
        memset(&param, 0, sizeof param);
        Token* a = cur(p);
        Token* b = peek(p, 1);
        if (is_kw(a, "self") &&
                   (tok_is(b, ",") || tok_is(b, ")"))) {
            param.name = ident(p);
            param.type = NULL;
        } else {
            AstType* ty = parse_type(p);
            if (!ty) return NULL;
            char* nm = ident(p);
            if (!nm) return NULL;
            param.name = nm;
            param.type = ty;
        }
        f->params = realloc(f->params, (f->nparams + 1) * sizeof *f->params);
        if (!f->params) exit(1);
        f->params[f->nparams++] = param;
        if (tok_is(cur(p), ",")) { adv(p); continue; }
        break;
    }
    if (!expect_punct(p, ")")) return NULL;
    if (!expect_punct(p, ";")) return NULL;
    return f;
}

static Item* parse_top(Parser* p) {
    Token* t = cur(p);
    if (is_kw(t, "module")) {
        adv(p);
        char* n = parse_dotted_name(p);
        if (!n) return NULL;
        if (!expect_punct(p, ";")) { free(n); return NULL; }
        Item* it = ast_item_new(TOP_MODULE);
        it->modname = n;
        return it;
    }
    if (is_kw(t, "import")) {
        adv(p);
        char* n = parse_dotted_name(p);
        if (!n) return NULL;
        if (!expect_punct(p, ";")) { free(n); return NULL; }
        Item* it = ast_item_new(TOP_IMPORT);
        it->impname = n;
        return it;
    }
    if (is_kw(t, "alias")) {
        /* alias NAME = TYPE;  →  typedef TYPE NAME; */
        adv(p);
        char* name = ident(p);
        if (!name) return NULL;
        if (!expect_punct(p, "=")) { free(name); return NULL; }
        /* Read everything until ';' as the raw type expression */
        SB type_sb;
        sb_init(&type_sb);
        while (!tok_is(cur(p), ";")) {
            sb_appendn(&type_sb, cur(p)->text, cur(p)->len);
            sb_append(&type_sb, " ");
            adv(p);
        }
        if (!expect_punct(p, ";")) { free(name); sb_free(&type_sb); return NULL; }
        /* Emit as TOP_RAW: "typedef <type> <name>;\n" */
        SB raw_sb;
        sb_init(&raw_sb);
        sb_append(&raw_sb, "typedef ");
        sb_appendn(&raw_sb, type_sb.data, type_sb.len);
        sb_append(&raw_sb, name);
        sb_append(&raw_sb, ";\n");
        sb_free(&type_sb);
        Item* it = ast_item_new(TOP_RAW);
        it->raw = sb_strdup(&raw_sb);
        it->raw_len = raw_sb.len;
        sb_free(&raw_sb);
        free(name);
        return it;
    }
    if (is_kw(t, "extern")) {
        adv(p);
        if (is_kw(cur(p), "struct")) {
            adv(p);
            StructDef* st = calloc(1, sizeof *st);
            if (!st) exit(1);
            Token* name_tok = cur(p);
            st->name = ident(p);
            if (!st->name) return NULL;
            if (name_tok) { st->line = name_tok->line; st->col = name_tok->col; }
            if (!expect_punct(p, ";")) return NULL;
            Item* it = ast_item_new(TOP_STRUCT);
            it->st = st;
            return it;
        }
        /* C-style extern function: `extern int puts(const char* s);` */
        FnDef* f = parse_extern_fn(p);
        if (!f) return NULL;
        Item* it = ast_item_new(TOP_FN);
        it->fn = f;
        return it;
    }
    if (is_kw(t, "object") || is_kw(t, "struct")) {
        adv(p);
        StructDef* st = calloc(1, sizeof *st);
        if (!st) exit(1);
        st->is_object = is_kw(t, "object");
        Token* name_tok = cur(p);
        st->name = ident(p);
        if (!st->name) { free(st); return NULL; }
        if (name_tok) { st->line = name_tok->line; st->col = name_tok->col; }
        if (tok_is(cur(p), ":")) {
            if (!st->is_object) {
                error_at(p, cur(p),
                    "plain C 'struct' cannot inherit; use 'object Name : Parent' for OOP");
                free(st->name); free(st); return NULL;
            }
            adv(p);
            st->parent = ident(p);
            if (!st->parent) return NULL;
        }
        if (!expect_punct(p, "{")) return NULL;
        while (!tok_is(cur(p), "}")) {
            StructField f;
            memset(&f, 0, sizeof f);
            Token* ft = cur(p);
            if (ft->kind != TK_IDENT) {
                error_at(p, ft, "expected struct field");
                return NULL;
            }
            if (tok_is(peek(p, 1), ":")) {
                f.style = FIELD_YUP;
                f.name = ident(p);
                if (!f.name) return NULL;
                adv(p); /* : */
                f.type = parse_type(p);
                if (!f.type) return NULL;
            } else {
                f.style = FIELD_C;
                f.type = parse_type(p);
                if (!f.type) return NULL;
                f.name = ident(p);
                if (!f.name) return NULL;
                if (tok_is(cur(p), "[")) {
                    adv(p);
                    f.dim = parse_expr(p);
                    if (!f.dim) return NULL;
                    if (!expect_punct(p, "]")) return NULL;
                }
                if (tok_is(cur(p), ";")) adv(p);
            }
            st->fields = realloc(st->fields, (st->nfields + 1) * sizeof *st->fields);
            if (!st->fields) exit(1);
            st->fields[st->nfields++] = f;
        }
        if (!expect_punct(p, "}")) return NULL;
        Item* it = ast_item_new(TOP_STRUCT);
        it->st = st;
        return it;
    }
    if (is_kw(t, "impl")) {
        adv(p);
        ImplDef* im = calloc(1, sizeof *im);
        if (!im) exit(1);
        im->start = t->start;
        im->line = t->line;
        im->col = t->col;
        im->target = parse_type(p);
        if (!im->target) return NULL;
        if (!expect_punct(p, "{")) return NULL;
        while (!tok_is(cur(p), "}")) {
            FnDef* m = parse_fn_def(p);
            if (!m) return NULL;
            im->methods = realloc(im->methods, (im->nmethods + 1) * sizeof *im->methods);
            if (!im->methods) exit(1);
            im->methods[im->nmethods++] = m;
        }
        if (!expect_punct(p, "}")) return NULL;
         Item* it = ast_item_new(TOP_IMPL);
         it->im = im;
         return it;
     }
    if (is_kw(t, "enum") || is_kw(t, "sum")) {
        int is_c_enum = is_kw(t, "enum");
        adv(p);
        EnumDef* ed = parse_sum_def(p, is_c_enum);
        if (!ed) return NULL;
        Item* it = ast_item_new(TOP_ENUM);
        it->ed = ed;
        return it;
    }
     if (is_kw(t, "trait")) {
         error_at(p, t, "'trait' is not supported; Rook is plain C + object/impl");
         return NULL;
     }
    /* C-style free function: `Type name ( params ) { body }` */
    {
        int save = p->idx;
        AstType* rt = try_parse_type(p);
        if (rt && cur(p)->kind == TK_IDENT && tok_is(peek(p, 1), "(")) {
            ast_type_free(rt);
            p->idx = save;
            FnDef* f = parse_fn_def(p);
            if (!f) return NULL;
            Item* it = ast_item_new(TOP_FN);
            it->fn = f;
            return it;
        }
        if (rt) { ast_type_free(rt); p->idx = save; }
    }
    error_at(p, t, "expected 'struct', 'object', 'impl', 'sum', 'extern' or 'alias'");
    return NULL;
}

/* ------------------------------ program ------------------------------ */

static int is_construct_kw(Token* t) {
    return is_kw(t, "struct") || is_kw(t, "object") || is_kw(t, "impl") ||
           is_kw(t, "sum") || is_kw(t, "enum") || is_kw(t, "extern") ||
           is_kw(t, "alias");
}

Program* parse_program(const char* src, int len, Token* toks, int ntoks) {
    freed_init();
    Parser p;
    p.src = src;
    p.slen = len;
    p.toks = toks;
    p.n = ntoks;
    p.idx = 0;
    p.depth = 0;
    p.last_end = 0;
    p.silent = 0;
    p.no_postfix = 0;
    p.err = 0;

    Program* prog = calloc(1, sizeof *prog);
    if (!prog) exit(1);
    int raw_begin = 0;

    for (;;) {
        Token* t = cur(&p);
        if (t->kind == TK_EOF) break;
        int top_start = (t->bol && p.depth == 0 &&
                         (is_construct_kw(t) || is_kw(t, "module") ||
                          is_kw(t, "import") ||
                          (t->kind == TK_IDENT && looks_like_dstyle_fn(&p))));
        if (top_start) {
            Item* raw = ast_item_new(TOP_RAW);
            raw->raw = (char*)src + raw_begin;
            raw->raw_len = t->start - raw_begin;
            if (raw->raw_len > 0) ast_program_add(prog, raw);
            else free(raw);
            Item* it = parse_top(&p);
            if (!it) {
                free(prog);
                return NULL;
            }
            ast_program_add(prog, it);
            raw_begin = p.last_end;
            continue;
        }
        if (tok_is(t, "{")) {
            p.depth++;
        } else if (tok_is(t, "}")) {
            if (p.depth > 0) p.depth--;
        }
        adv(&p);
    }

    Item* raw = ast_item_new(TOP_RAW);
    raw->raw = (char*)src + raw_begin;
    raw->raw_len = len - raw_begin;
    if (raw->raw_len > 0) ast_program_add(prog, raw);
    else free(raw);
    return prog;
}
