#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* dupstr(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* p = malloc(n + 1);
    if (!p) exit(1);
    memcpy(p, s, n + 1);
    return p;
}

AstType* ast_type_new(void) {
    AstType* t = calloc(1, sizeof *t);
    if (!t) exit(1);
    return t;
}

Expr* ast_expr_new(ExprKind k) {
    Expr* e = calloc(1, sizeof *e);
    if (!e) exit(1);
    e->kind = k;
    return e;
}

Stmt* ast_stmt_new(StmtKind k) {
    Stmt* s = calloc(1, sizeof *s);
    if (!s) exit(1);
    s->kind = k;
    return s;
}

Decl* ast_decl_new(DeclStyle st, const char* name, AstType* type, Expr* init) {
    Decl* d = calloc(1, sizeof *d);
    if (!d) exit(1);
    d->style = st;
    d->name = dupstr(name);
    d->type = type;
    d->init = init;
    return d;
}

Item* ast_item_new(TopKind k) {
    Item* it = calloc(1, sizeof *it);
    if (!it) exit(1);
    it->kind = k;
    return it;
}

void ast_program_add(Program* p, Item* it) {
    p->items = realloc(p->items, (p->nitems + 1) * sizeof *p->items);
    if (!p->items) exit(1);
    p->items[p->nitems++] = it;
}

static int str_eq(const char* a, const char* b) {
    if (!a || !b) return a == b;
    return strcmp(a, b) == 0;
}

int type_eq(AstType* a, AstType* b) {
    if (!a || !b) return a == b;
    if (!str_eq(a->qual, b->qual)) return 0;
    if (!str_eq(a->name, b->name)) return 0;
    if (a->ptrs != b->ptrs) return 0;
    return 1;
}

int expr_eq(Expr* a, Expr* b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind) return 0;
    if (!str_eq(a->str, b->str)) return 0;
    if (!type_eq(a->type, b->type)) return 0;
    if (!expr_eq(a->a, b->a)) return 0;
    if (!expr_eq(a->b, b->b)) return 0;
    if (!expr_eq(a->c, b->c)) return 0;
    if (a->nitems != b->nitems) return 0;
    for (int i = 0; i < a->nitems; i++)
        if (!expr_eq(a->items[i], b->items[i])) return 0;
    if (a->nnfields != b->nnfields) return 0;
    for (int i = 0; i < a->nnfields; i++) {
        if (!str_eq(a->nfields[i].name, b->nfields[i].name)) return 0;
        if (!expr_eq(a->nfields[i].e, b->nfields[i].e)) return 0;
    }
    if (a->ncitems != b->ncitems) return 0;
    for (int i = 0; i < a->ncitems; i++) {
        if (!str_eq(a->citems[i].name, b->citems[i].name)) return 0;
        if (!expr_eq(a->citems[i].e, b->citems[i].e)) return 0;
    }
    if (a->kind == E_MATCH) {
        if (a->nmarms != b->nmarms) return 0;
        for (int i = 0; i < a->nmarms; i++) {
            if (!expr_eq(a->marms[i].pattern, b->marms[i].pattern)) return 0;
            if (!expr_eq(a->marms[i].body, b->marms[i].body)) return 0;
        }
    }
    return 1;
}

static int decl_eq(Decl* a, Decl* b) {
    if (!a || !b) return a == b;
    if (a->style != b->style) return 0;
    if (!str_eq(a->name, b->name)) return 0;
    if (!type_eq(a->type, b->type)) return 0;
    if (!expr_eq(a->dim, b->dim)) return 0;
    return expr_eq(a->init, b->init);
}

int stmt_eq(Stmt* a, Stmt* b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind) return 0;
    switch (a->kind) {
    case S_BLOCK:
        if (a->nstmts != b->nstmts) return 0;
        for (int i = 0; i < a->nstmts; i++)
            if (!stmt_eq(a->stmts[i], b->stmts[i])) return 0;
        return 1;
    case S_EXPR:
        return expr_eq(a->e, b->e);
    case S_DECL:
        return decl_eq(a->decl, b->decl);
    case S_IF:
        return expr_eq(a->cond, b->cond) && stmt_eq(a->then, b->then) &&
               stmt_eq(a->els, b->els);
    case S_WHILE:
        return expr_eq(a->cond, b->cond) && stmt_eq(a->body, b->body);
    case S_FOR:
        return decl_eq(a->init_decl, b->init_decl) &&
               expr_eq(a->init_expr, b->init_expr) &&
               expr_eq(a->cond, b->cond) && expr_eq(a->step, b->step) &&
               stmt_eq(a->body, b->body);
    case S_FORIN:
        return str_eq(a->var, b->var) && expr_eq(a->iter, b->iter) &&
               stmt_eq(a->body, b->body);
    case S_SWITCH:
        if (!expr_eq(a->e, b->e)) return 0;
        if (a->narms != b->narms) return 0;
        for (int i = 0; i < a->narms; i++) {
            SwitchArm* x = &a->arms[i];
            SwitchArm* y = &b->arms[i];
            if (x->nlabels != y->nlabels) return 0;
            for (int j = 0; j < x->nlabels; j++)
                if (!expr_eq(x->labels[j], y->labels[j])) return 0;
            if (x->is_default != y->is_default) return 0;
            if (x->arrow != y->arrow) return 0;
            if (!stmt_eq(x->body, y->body)) return 0;
        }
        return 1;
    case S_MATCH:
        if (!expr_eq(a->e, b->e)) return 0;
        if (a->nmarms != b->nmarms) return 0;
        for (int i = 0; i < a->nmarms; i++) {
            if (!expr_eq(a->marms[i].pattern, b->marms[i].pattern)) return 0;
            if (!expr_eq(a->marms[i].body, b->marms[i].body)) return 0;
        }
        return 1;
    case S_RETURN:
        return expr_eq(a->e, b->e);
    default:
        return 1;
    }
}

static int fn_eq(FnDef* a, FnDef* b) {
    if (!a || !b) return a == b;
    if (!str_eq(a->name, b->name)) return 0;
    if (a->nparams != b->nparams) return 0;
    for (int i = 0; i < a->nparams; i++) {
        if (!str_eq(a->params[i].name, b->params[i].name)) return 0;
        if (!type_eq(a->params[i].type, b->params[i].type)) return 0;
    }
    if (!type_eq(a->ret, b->ret)) return 0;
    return stmt_eq(a->body, b->body);
}

static int struct_eq(StructDef* a, StructDef* b) {
    if (a->is_object != b->is_object) return 0;
    if (!str_eq(a->name, b->name)) return 0;
    if (!str_eq(a->parent, b->parent)) return 0;
    if (a->nfields != b->nfields) return 0;
    for (int i = 0; i < a->nfields; i++) {
        StructField* x = &a->fields[i];
        StructField* y = &b->fields[i];
        if (x->style != y->style) return 0;
        if (!str_eq(x->name, y->name)) return 0;
        if (!type_eq(x->type, y->type)) return 0;
        if (!expr_eq(x->dim, y->dim)) return 0;
    }
    return 1;
}

static int impl_eq(ImplDef* a, ImplDef* b) {
    if (!type_eq(a->target, b->target)) return 0;
    if (a->nmethods != b->nmethods) return 0;
    for (int i = 0; i < a->nmethods; i++)
        if (!fn_eq(a->methods[i], b->methods[i])) return 0;
    return 1;
}

static int structfield_eq(StructField* x, StructField* y) {
    if (!x || !y) return 0;
    if (x->style != y->style) return 0;
    if (!str_eq(x->name, y->name)) return 0;
    if (!type_eq(x->type, y->type)) return 0;
    if (!expr_eq(x->dim, y->dim)) return 0;
    return 1;
}

static int variant_eq(EnumVariant* a, EnumVariant* b) {
    if (!str_eq(a->name, b->name)) return 0;
    if (a->nfields != b->nfields) return 0;
    for (int i = 0; i < a->nfields; i++)
        if (!structfield_eq(&a->fields[i], &b->fields[i])) return 0;
    return 1;
}

static int enumdef_eq(EnumDef* a, EnumDef* b) {
    if (!str_eq(a->name, b->name)) return 0;
    if (a->nvariants != b->nvariants) return 0;
    for (int i = 0; i < a->nvariants; i++)
        if (!variant_eq(&a->variants[i], &b->variants[i])) return 0;
    return 1;
}

int item_eq(Item* a, Item* b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind) return 0;
    switch (a->kind) {
    case TOP_RAW:
        return a->raw_len == b->raw_len &&
               (a->raw_len == 0 || memcmp(a->raw, b->raw, a->raw_len) == 0);
    case TOP_FN:
        return fn_eq(a->fn, b->fn);
    case TOP_STRUCT:
        return struct_eq(a->st, b->st);
    case TOP_IMPL:
        return impl_eq(a->im, b->im);
    case TOP_ENUM:
        return enumdef_eq(a->ed, b->ed);
    case TOP_MODULE:
        return (a->modname && b->modname) ? (strcmp(a->modname, b->modname) == 0) : (a->modname == b->modname);
    case TOP_IMPORT:
        return (a->impname && b->impname) ? (strcmp(a->impname, b->impname) == 0) : (a->impname == b->impname);
    }
    return 0;
}

int program_eq(Program* a, Program* b) {
    if (a->nitems != b->nitems) return 0;
    for (int i = 0; i < a->nitems; i++)
        if (!item_eq(a->items[i], b->items[i])) return 0;
    return 1;
}

/* ------------------------- dump (debug aid) ------------------------- */

static void indent_out(int n) {
    for (int i = 0; i < n; i++) printf("  ");
}

static void dump_type(AstType* t) {
    if (!t) { printf("-"); return; }
    printf("%s%s", t->qual ? t->qual : "", t->name);
    for (int i = 0; i < t->ptrs; i++) printf("*");
}

static void dump_expr(Expr* e) {
    if (!e) { printf("-"); return; }
    switch (e->kind) {
    case E_LITERAL: printf("lit(%s)", e->str); break;
    case E_IDENT: printf("id(%s)", e->str); break;
    case E_CALL:
        dump_expr(e->a);
        printf("(");
        for (int i = 0; i < e->nitems; i++) {
            if (i) printf(", ");
            dump_expr(e->items[i]);
        }
        printf(")");
        break;
    case E_MEMBER: dump_expr(e->a); printf(".%s", e->str); break;
    case E_ARROW: dump_expr(e->a); printf("->%s", e->str); break;
    case E_INDEX: dump_expr(e->a); printf("["); dump_expr(e->b); printf("]"); break;
    case E_UNARY: printf("%s", e->str); dump_expr(e->a); break;
    case E_POST: dump_expr(e->a); printf("%s", e->str); break;
    case E_BINARY:
        printf("("); dump_expr(e->a); printf(" %s ", e->str); dump_expr(e->b); printf(")");
        break;
    case E_TERNARY:
        printf("("); dump_expr(e->a); printf(" ? "); dump_expr(e->b);
        printf(" : "); dump_expr(e->c); printf(")");
        break;
    case E_ASSIGN:
        printf("("); dump_expr(e->a); printf(" %s ", e->str); dump_expr(e->b); printf(")");
        break;
    case E_CAST:
        printf("("); dump_type(e->type); printf(")"); dump_expr(e->a);
        break;
    case E_COMPOUND:
        printf("("); dump_type(e->type); printf("){");
        for (int i = 0; i < e->ncitems; i++) {
            if (i) printf(", ");
            if (e->citems[i].name) printf(".%s = ", e->citems[i].name);
            dump_expr(e->citems[i].e);
        }
        printf("}");
        break;
    case E_NAMED_INIT:
        dump_type(e->type);
        printf("{");
        for (int i = 0; i < e->nnfields; i++) {
            if (i) printf(", ");
            printf("%s: ", e->nfields[i].name);
            dump_expr(e->nfields[i].e);
        }
        printf("}");
        break;
    case E_BRACE_INIT:
        printf("{");
        for (int i = 0; i < e->nitems; i++) {
            if (i) printf(", ");
            dump_expr(e->items[i]);
        }
        printf("}");
        break;
    case E_PAREN: printf("("); dump_expr(e->a); printf(")"); break;
    case E_SIZEOF_T: printf("sizeof("); dump_type(e->type); printf(")"); break;
    case E_SIZEOF_E: printf("sizeof("); dump_expr(e->a); printf(")"); break;
    case E_ARR_LIT:
        printf("[");
        for (int i = 0; i < e->nitems; i++) {
            if (i) printf(", ");
            dump_expr(e->items[i]);
        }
        printf("]");
        break;
    case E_RANGE: dump_expr(e->a); printf("..="); dump_expr(e->b); break;
    case E_QUESTION: dump_expr(e->a); printf("?"); break;
    case E_MATCH: {
        printf("match ("); dump_expr(e->a); printf(") { ");
        for (int i = 0; i < e->nmarms; i++) {
            if (i) printf(", ");
            dump_expr(e->marms[i].pattern); printf(" => "); dump_expr(e->marms[i].body);
        }
        printf(" }");
        break;
    }
    }
}

static void dump_decl(Decl* d) {
    if (!d) { printf("-"); return; }
    if (d->style == DECL_LET) {
        printf("let %s", d->name);
        if (d->type) { printf(": "); dump_type(d->type); }
    } else if (d->style == DECL_TYPED) {
        printf("%s: ", d->name);
        dump_type(d->type);
    } else {
        dump_type(d->type);
        printf(" %s", d->name);
        if (d->dim) { printf("["); dump_expr(d->dim); printf("]"); }
    }
    if (d->init) { printf(" = "); dump_expr(d->init); }
}

static void dump_stmt(Stmt* s, int d) {
    if (!s) return;
    switch (s->kind) {
    case S_BLOCK:
        printf("{\n");
        for (int i = 0; i < s->nstmts; i++) {
            indent_out(d + 1);
            dump_stmt(s->stmts[i], d + 1);
        }
        indent_out(d);
        printf("}");
        break;
    case S_EXPR: dump_expr(s->e); break;
    case S_DECL: dump_decl(s->decl); break;
    case S_IF:
        printf("if ("); dump_expr(s->cond); printf(") ");
        dump_stmt(s->then, d + 1);
        if (s->els) {
            printf(" else ");
            dump_stmt(s->els, d);
        }
        break;
    case S_WHILE:
        printf("while ("); dump_expr(s->cond); printf(") ");
        dump_stmt(s->body, d);
        break;
    case S_FOR:
        printf("for (");
        if (s->init_decl) dump_decl(s->init_decl);
        if (s->init_expr) dump_expr(s->init_expr);
        printf("; "); dump_expr(s->cond); printf("; "); dump_expr(s->step);
        printf(") ");
        dump_stmt(s->body, d);
        break;
    case S_FORIN:
        printf("for %s in ", s->var);
        dump_expr(s->iter);
        printf(" ");
        dump_stmt(s->body, d);
        break;
    case S_SWITCH:
        printf("switch ("); dump_expr(s->e); printf(") {\n");
        for (int i = 0; i < s->narms; i++) {
            SwitchArm* a = &s->arms[i];
            indent_out(d + 1);
            for (int j = 0; j < a->nlabels; j++) {
                printf("case "); dump_expr(a->labels[j]); printf(":\n");
                indent_out(d + 1);
            }
            if (a->is_default) printf("default:\n");
            if (a->arrow) printf("-> ");
            dump_stmt(a->body, d + 2);
            printf("\n");
        }
        indent_out(d);
        printf("}");
        break;
    case S_MATCH:
        printf("match ("); dump_expr(s->e); printf(") {\n");
        for (int i = 0; i < s->nmarms; i++) {
            indent_out(d + 1);
            dump_expr(s->marms[i].pattern);
            printf(" => ");
            dump_expr(s->marms[i].body);
            printf(",\n");
        }
        indent_out(d);
        printf("}");
        break;
    case S_RETURN:
        printf("return");
        if (s->e) { printf(" "); dump_expr(s->e); }
        break;
    case S_BREAK: printf("break"); break;
    case S_CONTINUE: printf("continue"); break;
    case S_EMPTY: printf(";"); break;
    case S_DEFER:
        printf("defer ");
        dump_stmt(s->body, d);
        break;
    }
}

static void dump_fn(FnDef* f) {
    printf("fn %s(", f->name);
    for (int i = 0; i < f->nparams; i++) {
        if (i) printf(", ");
        printf("%s: ", f->params[i].name);
        dump_type(f->params[i].type);
    }
    printf(")");
    if (f->ret) { printf(" "); dump_type(f->ret); }
    if (!f->body) { printf(";\n"); return; }
    printf("\n");
    dump_stmt(f->body, 1);
    printf("\n");
}

void ast_dump(Program* p) {
    for (int i = 0; i < p->nitems; i++) {
        Item* it = p->items[i];
        switch (it->kind) {
        case TOP_RAW:
            printf("raw[%d] '%.60s%s'\n", it->raw_len, it->raw,
                   it->raw_len > 60 ? "..." : "");
            break;
        case TOP_FN:
            dump_fn(it->fn);
            break;
        case TOP_STRUCT:
            printf("struct %s", it->st->name);
            if (it->st->parent) printf(" : %s", it->st->parent);
            printf(" {\n");
            for (int j = 0; j < it->st->nfields; j++) {
                StructField* f = &it->st->fields[j];
                indent_out(1);
                if (f->style == FIELD_YUP) {
                    printf("%s: ", f->name);
                    dump_type(f->type);
                } else {
                    dump_type(f->type);
                    printf(" %s", f->name);
                    if (f->dim) { printf("["); dump_expr(f->dim); printf("]"); }
                    printf(";");
                }
                printf("\n");
            }
            printf("}\n");
            break;
        case TOP_IMPL:
            printf("impl ");
            dump_type(it->im->target);
            printf(" {\n");
            for (int j = 0; j < it->im->nmethods; j++) {
                dump_fn(it->im->methods[j]);
            }
            printf("}\n");
            break;
        case TOP_ENUM:
            printf("enum %s", it->ed->name);
            printf(" {\n");
             for (int j = 0; j < it->ed->nvariants; j++) {
                 EnumVariant* v = &it->ed->variants[j];
                 indent_out(1);
                 printf("%s", v->name);
                 if (v->nfields) {
                     printf(" { ");
                     for (int k = 0; k < v->nfields; k++) {
                         if (k) printf(", ");
                         dump_type(v->fields[k].type);
                         printf(" %s", v->fields[k].name);
                     }
                     printf(" };");
                 }
                 printf(",\n");
            }
            printf("}\n");
            break;
        case TOP_MODULE:
            printf("module %s;\n", it->modname ? it->modname : "");
            break;
        case TOP_IMPORT:
            printf("import %s;\n", it->impname ? it->impname : "");
            break;
        }
    }
}

/* ── Ownership-freeing (used at process exit to keep sanitizers clean) ──
   A small pointer set guards against double-free when AST nodes are shared
   (notably AstType objects referenced from many expression/parameter nodes). */

static void** g_freed;
static size_t g_freed_n, g_freed_cap;

void freed_init(void) { g_freed_n = 0; g_freed_cap = 0; g_freed = NULL; }
static int  freed_has(void* p) {
    for (size_t i = 0; i < g_freed_n; i++) if (g_freed[i] == p) return 1;
    return 0;
}
static void freed_add(void* p) {
    if (!p || freed_has(p)) return;
    if (g_freed_n == g_freed_cap) {
        g_freed_cap = g_freed_cap ? g_freed_cap * 2 : 1024;
        g_freed = realloc(g_freed, g_freed_cap * sizeof(void*));
        if (!g_freed) exit(1);
    }
    g_freed[g_freed_n++] = p;
}

/* NOTE: the AST mixes owned node structs with *borrowed* string fields (name,
   str, var, etc. point into the lexer's token buffer, freed by main).
   These free helpers therefore free node structs and pointer/struct arrays only
   — never the string fields — to avoid double-frees against the token buffer.
   AstType name/qual are likewise borrowed and not freed here. */

void ast_type_free(AstType* t) {
    if (!t || freed_has(t)) return;
    freed_add(t);
    free(t);
}

static void expr_free(Expr* e) {
    if (!e || freed_has(e)) return;
    freed_add(e);
    ast_type_free(e->type);
    expr_free(e->a); expr_free(e->b); expr_free(e->c);
    for (int i = 0; i < e->nitems; i++) expr_free(e->items[i]);
    free(e->items);
    for (int i = 0; i < e->nnfields; i++) expr_free(e->nfields[i].e);
    free(e->nfields);
    for (int i = 0; i < e->ncitems; i++) expr_free(e->citems[i].e);
    free(e->citems);
    if (e->marms) {
        for (int i = 0; i < e->nmarms; i++) {
            expr_free(e->marms[i].pattern);
            expr_free(e->marms[i].body);
        }
        free(e->marms);
    }
    free(e);
}

static void decl_free(Decl* d) {
    if (!d || freed_has(d)) return;
    freed_add(d);
    ast_type_free(d->type);
    expr_free(d->dim);
    expr_free(d->init);
    free(d);
}

static void stmt_free(Stmt* s) {
    if (!s || freed_has(s)) return;
    freed_add(s);
    expr_free(s->e);
    decl_free(s->decl);
    expr_free(s->cond);
    stmt_free(s->then); stmt_free(s->els); stmt_free(s->body);
    decl_free(s->init_decl);
    expr_free(s->init_expr); expr_free(s->step);
    expr_free(s->iter);
    for (int i = 0; i < s->narms; i++) {
        for (int j = 0; j < s->arms[i].nlabels; j++) expr_free(s->arms[i].labels[j]);
        free(s->arms[i].labels);
        stmt_free(s->arms[i].body);
    }
    free(s->arms);
    for (int i = 0; i < s->nmarms; i++) {
        expr_free(s->marms[i].pattern); expr_free(s->marms[i].body);
    }
    free(s->marms);
    for (int i = 0; i < s->nstmts; i++) stmt_free(s->stmts[i]);
    free(s->stmts);
    stmt_free(s->defer);
    free(s);
}

static void param_free(Param* p) {
    if (!p) return;
    /* Param lives inside FnDef.params[]; free only its owned type node. */
    ast_type_free(p->type);
}

static void fndef_free(FnDef* f) {
    if (!f || freed_has(f)) return;
    freed_add(f);
    for (int i = 0; i < f->nparams; i++) param_free(&f->params[i]);
    free(f->params);              /* array of Param structs (name is borrowed) */
    ast_type_free(f->ret);
    stmt_free(f->body);
    free(f);
}

static void structdef_free(StructDef* s) {
    if (!s || freed_has(s)) return;
    freed_add(s);
    for (int i = 0; i < s->nfields; i++) {
        ast_type_free(s->fields[i].type);
        expr_free(s->fields[i].dim);
    }
    free(s->fields);             /* array of StructField (name is borrowed) */
    free(s);
}

static void impldef_free(ImplDef* im) {
    if (!im || freed_has(im)) return;
    freed_add(im);
    ast_type_free(im->target);
    for (int i = 0; i < im->nmethods; i++) fndef_free(im->methods[i]);
    free(im->methods);
    free(im);
}

static void enumdef_free(EnumDef* e) {
    if (!e || freed_has(e)) return;
    freed_add(e);
    for (int i = 0; i < e->nvariants; i++) {
        EnumVariant* v = &e->variants[i];
        for (int j = 0; j < v->nfields; j++) {
            StructField* f = &v->fields[j];
            free(f->name);
            ast_type_free(f->type);
            free(f->dim);
        }
        free(v->fields);
    }
    free(e->variants);
    free(e);
}

static void item_free(Item* it) {
    if (!it || freed_has(it)) return;
    freed_add(it);
    /* it->raw is a borrowed slice into the source buffer (owned by main),
       not independently allocated — do not free it. */
    fndef_free(it->fn);
    structdef_free(it->st);
    impldef_free(it->im);
    enumdef_free(it->ed);
    free(it->modname);
    free(it->impname);
    free(it);
}

void program_free(Program* p) {
    if (!p) return;
    freed_init();
    for (int i = 0; i < p->nitems; i++) item_free(p->items[i]);
    free(p->items);
    free(p);
    free(g_freed); g_freed = NULL;
}
