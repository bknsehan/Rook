#include "emit.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

typedef struct Emit {
    SB sb;
} Emit;

static void e_indent(Emit* e, int n) {
    for (int i = 0; i < n; i++) sb_append(&e->sb, "    ");
}

static void e_type(Emit* e, AstType* t) {
    if (!t) return;
    if (t->qual) sb_append(&e->sb, t->qual);
    sb_append(&e->sb, t->name);
    for (int i = 0; i < t->ptrs; i++) sb_append(&e->sb, "*");
}

static void e_expr(Emit* e, Expr* x);

static void e_items(Emit* e, Expr** items, int n) {
    for (int i = 0; i < n; i++) {
        if (i) sb_append(&e->sb, ", ");
        e_expr(e, items[i]);
    }
}

static void e_expr(Emit* e, Expr* x) {
    if (!x) return;
    switch (x->kind) {
    case E_LITERAL:
    case E_IDENT:
        sb_append(&e->sb, x->str);
        break;
    case E_CALL:
        e_expr(e, x->a);
        sb_append(&e->sb, "(");
        e_items(e, x->items, x->nitems);
        sb_append(&e->sb, ")");
        break;
    case E_MEMBER:
        e_expr(e, x->a);
        sb_append(&e->sb, ".");
        sb_append(&e->sb, x->str);
        break;
    case E_ARROW:
        e_expr(e, x->a);
        sb_append(&e->sb, "->");
        sb_append(&e->sb, x->str);
        break;
    case E_INDEX:
        e_expr(e, x->a);
        sb_append(&e->sb, "[");
        e_expr(e, x->b);
        sb_append(&e->sb, "]");
        break;
    case E_UNARY:
        sb_append(&e->sb, x->str);
        e_expr(e, x->a);
        break;
    case E_POST:
        e_expr(e, x->a);
        sb_append(&e->sb, x->str);
        break;
    case E_BINARY:
    case E_ASSIGN:
        e_expr(e, x->a);
        sb_append(&e->sb, " ");
        sb_append(&e->sb, x->str);
        sb_append(&e->sb, " ");
        e_expr(e, x->b);
        break;
    case E_TERNARY:
        e_expr(e, x->a);
        sb_append(&e->sb, " ? ");
        e_expr(e, x->b);
        sb_append(&e->sb, " : ");
        e_expr(e, x->c);
        break;
    case E_CAST:
        sb_append(&e->sb, "(");
        e_type(e, x->type);
        sb_append(&e->sb, ")");
        e_expr(e, x->a);
        break;
    case E_COMPOUND:
        sb_append(&e->sb, "(");
        e_type(e, x->type);
        sb_append(&e->sb, "){ ");
        for (int i = 0; i < x->ncitems; i++) {
            if (i) sb_append(&e->sb, ", ");
            if (x->citems[i].name) {
                sb_append(&e->sb, ".");
                sb_append(&e->sb, x->citems[i].name);
                sb_append(&e->sb, " = ");
            }
            e_expr(e, x->citems[i].e);
        }
        sb_append(&e->sb, " }");
        break;
    case E_NAMED_INIT:
        e_type(e, x->type);
        sb_append(&e->sb, " { ");
        for (int i = 0; i < x->nnfields; i++) {
            if (i) sb_append(&e->sb, ", ");
            sb_append(&e->sb, x->nfields[i].name);
            sb_append(&e->sb, ": ");
            e_expr(e, x->nfields[i].e);
        }
        sb_append(&e->sb, " }");
        break;
    case E_BRACE_INIT:
        sb_append(&e->sb, "{ ");
        e_items(e, x->items, x->nitems);
        sb_append(&e->sb, " }");
        break;
    case E_PAREN:
        sb_append(&e->sb, "(");
        e_expr(e, x->a);
        sb_append(&e->sb, ")");
        break;
    case E_SIZEOF_T:
        sb_append(&e->sb, "sizeof(");
        e_type(e, x->type);
        sb_append(&e->sb, ")");
        break;
    case E_SIZEOF_E:
        sb_append(&e->sb, "sizeof(");
        e_expr(e, x->a);
        sb_append(&e->sb, ")");
        break;
    case E_ARR_LIT:
        sb_append(&e->sb, "[");
        e_items(e, x->items, x->nitems);
        sb_append(&e->sb, "]");
        break;
    case E_RANGE:
        e_expr(e, x->a);
        sb_append(&e->sb, "..=");
        e_expr(e, x->b);
        break;
    case E_QUESTION:
        e_expr(e, x->a);
        sb_append(&e->sb, "?");
        break;
    case E_MATCH:
        sb_append(&e->sb, "match (");
        e_expr(e, x->a);
        sb_append(&e->sb, ") { ");
        for (int i = 0; i < x->nmarms; i++) {
            if (i) sb_append(&e->sb, ", ");
            e_expr(e, x->marms[i].pattern);
            sb_append(&e->sb, " => ");
            e_expr(e, x->marms[i].body);
        }
        sb_append(&e->sb, " }");
        break;
    }
}

static void e_decl(Emit* e, Decl* d) {
    if (!d) return;
    if (d->style == DECL_LET) {
        sb_append(&e->sb, "let ");
        sb_append(&e->sb, d->name);
        if (d->type) {
            sb_append(&e->sb, ": ");
            e_type(e, d->type);
        }
    } else if (d->style == DECL_TYPED) {
        sb_append(&e->sb, d->name);
        sb_append(&e->sb, ": ");
        e_type(e, d->type);
    } else {
        e_type(e, d->type);
        sb_append(&e->sb, " ");
        sb_append(&e->sb, d->name);
        if (d->dim) {
            sb_append(&e->sb, "[");
            e_expr(e, d->dim);
            sb_append(&e->sb, "]");
        }
    }
    if (d->init) {
        sb_append(&e->sb, " = ");
        e_expr(e, d->init);
    }
}

static void e_stmt(Emit* e, Stmt* s, int ind);
static void e_stmt_plain(Emit* e, Stmt* s, int ind);

static void e_block_stmts(Emit* e, Stmt* block, int ind) {
    for (int i = 0; i < block->nstmts; i++) {
        e_stmt(e, block->stmts[i], ind);
        sb_append(&e->sb, "\n");
    }
}

static void e_stmt(Emit* e, Stmt* s, int ind) {
    e_indent(e, ind);
    e_stmt_plain(e, s, ind);
}

/* Print `s` (which may be a bare stmt or a block) as the body of an
   if/while/for/switch arm: a block keeps its braces; other stmts are
   wrapped in a brace for round-trip stability. */
static void e_body(Emit* e, Stmt* s, int ind) {
    if (s && s->kind == S_BLOCK) {
        sb_append(&e->sb, "{\n");
        e_block_stmts(e, s, ind + 1);
        e_indent(e, ind);
        sb_append(&e->sb, "}");
        return;
    }
    e_stmt(e, s, ind);
}

static void e_stmt_plain(Emit* e, Stmt* s, int ind) {
    if (!s) return;
    switch (s->kind) {
    case S_BLOCK:
        if (s->nstmts == 1) {
            /* a bare body stmt: emit it inline so it reparses as the
               original bare stmt (not wrapped in a block) */
            e_stmt_plain(e, s->stmts[0], ind);
            break;
        }
        sb_append(&e->sb, "{\n");
        e_block_stmts(e, s, ind + 1);
        e_indent(e, ind);
        sb_append(&e->sb, "}");
        break;
    case S_EXPR:
        e_expr(e, s->e);
        /* Block-like expressions (match) take no terminating ';' in statement
           position, matching the parser (parse_stmt consumes `match` with no
           trailing semicolon). */
        if (!(s->e && s->e->kind == E_MATCH))
            sb_append(&e->sb, ";");
        break;
    case S_DECL:
        e_decl(e, s->decl);
        sb_append(&e->sb, ";");
        break;
    case S_IF:
        sb_append(&e->sb, "if (");
        e_expr(e, s->cond);
        sb_append(&e->sb, ") ");
        e_body(e, s->then, ind);
        if (s->els) {
            if (s->els->kind == S_IF) {
                sb_append(&e->sb, " else ");
                e_stmt_plain(e, s->els, ind);
            } else {
                sb_append(&e->sb, " else ");
                e_body(e, s->els, ind);
            }
        }
        break;
    case S_WHILE:
        sb_append(&e->sb, "while (");
        e_expr(e, s->cond);
        sb_append(&e->sb, ") ");
        e_body(e, s->body, ind);
        break;
    case S_FOR:
        sb_append(&e->sb, "for (");
        if (s->init_decl) e_decl(e, s->init_decl);
        if (s->init_expr) e_expr(e, s->init_expr);
        sb_append(&e->sb, "; ");
        e_expr(e, s->cond);
        sb_append(&e->sb, "; ");
        e_expr(e, s->step);
        sb_append(&e->sb, ") ");
        e_body(e, s->body, ind);
        break;
    case S_FORIN:
        sb_append(&e->sb, "for ");
        sb_append(&e->sb, s->var);
        sb_append(&e->sb, " in ");
        e_expr(e, s->iter);
        sb_append(&e->sb, " ");
        e_body(e, s->body, ind);
        break;
    case S_SWITCH:
        sb_append(&e->sb, "switch (");
        e_expr(e, s->e);
        sb_append(&e->sb, ") {\n");
        for (int i = 0; i < s->narms; i++) {
            SwitchArm* a = &s->arms[i];
            if (a->arrow) {
                e_indent(e, ind + 1);
                if (a->is_default && a->nlabels == 0) {
                    sb_append(&e->sb, "else -> ");
                } else {
                    for (int j = 0; j < a->nlabels; j++) {
                        if (j) sb_append(&e->sb, " ");
                        sb_append(&e->sb, "case ");
                        e_expr(e, a->labels[j]);
                    }
                    sb_append(&e->sb, " -> ");
                }
                e_stmt_plain(e, a->body, ind + 1);
                sb_append(&e->sb, "\n");
            } else {
                for (int j = 0; j < a->nlabels; j++) {
                    e_indent(e, ind + 1);
                    sb_append(&e->sb, "case ");
                    e_expr(e, a->labels[j]);
                    sb_append(&e->sb, ":\n");
                }
                if (a->is_default && a->nlabels == 0) {
                    e_indent(e, ind + 1);
                    sb_append(&e->sb, "default:\n");
                }
                if (a->body->kind == S_BLOCK) {
                    e_block_stmts(e, a->body, ind + 2);
                } else {
                    e_stmt(e, a->body, ind + 1);
                    sb_append(&e->sb, "\n");
                }
            }
        }
        e_indent(e, ind);
        sb_append(&e->sb, "}");
        break;
    case S_MATCH:
        sb_append(&e->sb, "match (");
        e_expr(e, s->e);
        sb_append(&e->sb, ") {\n");
        for (int i = 0; i < s->nmarms; i++) {
            MatchArm* a = &s->marms[i];
            e_indent(e, ind + 1);
            e_expr(e, a->pattern);
            sb_append(&e->sb, " => ");
            e_expr(e, a->body);
            sb_append(&e->sb, ",\n");
        }
        e_indent(e, ind);
        sb_append(&e->sb, "}");
        break;
    case S_RETURN:
        sb_append(&e->sb, "return");
        if (s->e) {
            sb_append(&e->sb, " ");
            e_expr(e, s->e);
        }
        sb_append(&e->sb, ";");
        break;
    case S_BREAK:
        sb_append(&e->sb, "break;");
        break;
    case S_CONTINUE:
        sb_append(&e->sb, "continue;");
        break;
    case S_EMPTY:
        sb_append(&e->sb, ";");
        break;
    case S_DEFER:
        sb_append(&e->sb, "defer ");
        e_stmt_plain(e, s->defer, ind);
        break;
    }
}

/* Print a method/function signature only (no body), used for `trait` items
   whose methods are declared without implementation. C-style: ret name(params); */
__attribute__((unused)) static void e_fn_sig(Emit* e, FnDef* f, int ind) {
    e_indent(e, ind);
    if (f->ret) {
        e_type(e, f->ret);
        sb_append(&e->sb, " ");
    }
    sb_append(&e->sb, f->name);
    sb_append(&e->sb, "(");
    for (int i = 0; i < f->nparams; i++) {
        if (i) sb_append(&e->sb, ", ");
        /* C-style: type name */
        if (f->params[i].type) {
            e_type(e, f->params[i].type);
            sb_append(&e->sb, " ");
        }
        sb_append(&e->sb, f->params[i].name);
    }
    sb_append(&e->sb, ");");
    return;
}

static void e_fn(Emit* e, FnDef* f, int ind) {
    e_indent(e, ind);
    if (!f->body) sb_append(&e->sb, "extern ");
    /* C-style: ret name(params) { body } */
    if (f->ret) {
        e_type(e, f->ret);
        sb_append(&e->sb, " ");
    }
    sb_append(&e->sb, f->name);
    sb_append(&e->sb, "(");
    for (int i = 0; i < f->nparams; i++) {
        if (i) sb_append(&e->sb, ", ");
        /* C-style: type name */
        if (f->params[i].type) {
            e_type(e, f->params[i].type);
            sb_append(&e->sb, " ");
        }
        sb_append(&e->sb, f->params[i].name);
    }
    sb_append(&e->sb, ")");
    if (!f->body) {
        sb_append(&e->sb, ";");
        return;
    }
    sb_append(&e->sb, " {\n");
    e_block_stmts(e, f->body, ind + 1);
    e_indent(e, ind);
    sb_append(&e->sb, "}");
}

static void e_struct(Emit* e, StructDef* st) {
    sb_append(&e->sb, st->is_object ? "object " : "struct ");
    sb_append(&e->sb, st->name);
    if (st->parent) {
        sb_append(&e->sb, " : ");
        sb_append(&e->sb, st->parent);
    }
    if (st->nfields == 0) {
        sb_append(&e->sb, ";\n");
        return;
    }
    sb_append(&e->sb, " {\n");
    for (int i = 0; i < st->nfields; i++) {
        StructField* f = &st->fields[i];
        e_indent(e, 1);
        /* All fields emitted as C-style: type name; */
        e_type(e, f->type);
        sb_append(&e->sb, " ");
        sb_append(&e->sb, f->name);
        if (f->dim) {
            sb_append(&e->sb, "[");
            e_expr(e, f->dim);
            sb_append(&e->sb, "]");
        }
        sb_append(&e->sb, ";\n");
    }
    sb_append(&e->sb, "}");
}

static void e_enum(Emit* e, EnumDef* ed) {
    if (ed->is_c_enum) {
        sb_append(&e->sb, "enum ");
        sb_append(&e->sb, ed->name);
        sb_append(&e->sb, " { ");
        for (int i = 0; i < ed->nvariants; i++) {
            if (i) sb_append(&e->sb, ", ");
            sb_append(&e->sb, ed->variants[i].name);
        }
        sb_append(&e->sb, " }");
        return;
    }
    sb_append(&e->sb, "sum ");
    sb_append(&e->sb, ed->name);
    sb_append(&e->sb, " { ");
    for (int i = 0; i < ed->nvariants; i++) {
        if (i) sb_append(&e->sb, "; ");
        sb_append(&e->sb, ed->variants[i].name);
        if (ed->variants[i].nfields) {
            sb_append(&e->sb, " { ");
            for (int j = 0; j < ed->variants[i].nfields; j++) {
                if (j) sb_append(&e->sb, " ");
                sb_append(&e->sb, ed->variants[i].fields[j].name);
                sb_append(&e->sb, ": ");
                e_type(e, ed->variants[i].fields[j].type);
                sb_append(&e->sb, ";");
            }
            sb_append(&e->sb, " }");
        }
    }
    sb_append(&e->sb, " }");
}

static void e_impl(Emit* e, ImplDef* im) {
    sb_append(&e->sb, "impl ");
    e_type(e, im->target);
    sb_append(&e->sb, " {\n");
    for (int i = 0; i < im->nmethods; i++) {
        e_fn(e, im->methods[i], 1);
    }
    sb_append(&e->sb, "}");
}

char* emit_program(Program* p, int* out_len) {
    Emit e;
    sb_init(&e.sb);
    for (int i = 0; i < p->nitems; i++) {
        Item* it = p->items[i];
        switch (it->kind) {
        case TOP_RAW:
            sb_appendn(&e.sb, it->raw, it->raw_len);
            break;
        case TOP_FN:
            e_fn(&e, it->fn, 0);
            break;
        case TOP_STRUCT:
            e_struct(&e, it->st);
            break;
        case TOP_IMPL:
            e_impl(&e, it->im);
            break;
        case TOP_ENUM:
            e_enum(&e, it->ed);
            break;
        case TOP_MODULE:
            if (it->modname) sb_appendf(&e.sb, "module %s;\n", it->modname);
            break;
        case TOP_IMPORT:
            if (it->impname) sb_appendf(&e.sb, "import %s;\n", it->impname);
            break;
        }
    }
    if (out_len) *out_len = e.sb.len;
    return e.sb.data;
}
