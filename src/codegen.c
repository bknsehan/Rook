#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

typedef struct {
    Stmt** stmts;   /* deferred statements in this scope frame */
    int count;
    int cap;
    int loop_depth; /* loop depth when this frame was opened */
} DeferFrame;

typedef struct CG {
    SB sb;
    Sema* sema;
    int ind;
    char** local_names;
    AstType** local_types;
    int nlocals;
    int cap;
    int result_count;
    DeferFrame defer_stack[64];
    int defer_depth;
    int loop_depth;
    AstType* cur_ret;   /* return type of the function currently being emitted */
    int bounds_check;   /* -b flag: emit runtime bounds checks for array access */
} CG;

static void cg_stmt(CG* g, Stmt* s);

/* Record a deferred statement into the current scope frame. */
static void cg_add_defer(CG* g, Stmt* d) {
    DeferFrame* f = &g->defer_stack[g->defer_depth - 1];
    if (f->count >= f->cap) {
        f->cap = f->cap ? f->cap * 2 : 4;
        f->stmts = realloc(f->stmts, (size_t)f->cap * sizeof(Stmt*));
    }
    f->stmts[f->count++] = d;
}

/* Emit the deferred statements of a frame in LIFO order. */
static void cg_emit_defers(CG* g, DeferFrame* f) {
    for (int i = f->count - 1; i >= 0; i--) cg_stmt(g, f->stmts[i]);
}

/* Flush every currently-open defer frame (LIFO), as on a function return. */
static void cg_emit_all_defers(CG* g) {
    for (int d = g->defer_depth - 1; d >= 0; d--)
        cg_emit_defers(g, &g->defer_stack[d]);
}

/* Flush only defer frames that are not inside a deeper loop than the current
   one (used for break/continue, which do not leave the function). */
static void cg_emit_loop_defers(CG* g) {
    for (int d = g->defer_depth - 1; d >= 0; d--)
        if (g->defer_stack[d].loop_depth >= g->loop_depth)
            cg_emit_defers(g, &g->defer_stack[d]);
}

static void cg_indent(CG* g);
static void cg_type(CG* g, AstType* t);
static void cg_expr(CG* g, Expr* x);
static void cg_stmt(CG* g, Stmt* s);
static void cg_decl(CG* g, Decl* d);
static void cg_call(CG* g, Expr* callee, Expr** args, int nargs);
static void cg_items(CG* g, Expr** items, int n);
static char* type_to_str(AstType* t);
static AstType* infer_let_type(CG* g, Expr* init);
static void cg_struct(CG* g, StructDef* st);
static void cg_enum(CG* g, EnumDef* ed);
static EnumDef* find_enum_def(CG* g, const char* name);
static void cg_emit_variant_const(CG* g, const char* enum_name, const char* variant);
static int variant_index(EnumDef* ed, const char* name);
static int enum_has_payload(EnumDef* ed);
static void cg_fn(CG* g, FnDef* f, int ind, AstType* receiver);
static StructDef* find_struct_def(CG* g, const char* name);

static StructDef* lookup_struct(CG* g, const char* name) {
    if (!g->sema || !name) return NULL;
    Sym* sym = sema_lookup(g->sema, name);
    if (sym && (sym->kind == SYM_STRUCT || sym->kind == SYM_IMPL)) {
        if (sym->st) return sym->st;
        return find_struct_def(g, name);
    }
    return NULL;
}

static int struct_has_field(StructDef* st, const char* field) {
    if (!st || !field) return 0;
    for (int i = 0; i < st->nfields; i++) {
        if (strcmp(st->fields[i].name, field) == 0) return 1;
    }
    return 0;
}

/* Number of `._base` hops (via inheritance chain) needed to reach `field`. */
static int field_owner_levels(CG* g, StructDef* st, const char* field) {
    int levels = 0;
    StructDef* cur = st;
    while (cur) {
        if (struct_has_field(cur, field)) return levels;
        cur = cur->parent ? lookup_struct(g, cur->parent) : NULL;
        levels++;
    }
    return 0;
}

/* Find the type of a field reached via `obj_type` (walking the inheritance chain). */
static AstType* member_field_type(CG* g, AstType* obj_type, const char* field) {
    if (!obj_type || !obj_type->name || !field) return NULL;
    StructDef* st = lookup_struct(g, obj_type->name);
    while (st) {
        for (int i = 0; i < st->nfields; i++) {
            if (strcmp(st->fields[i].name, field) == 0) return st->fields[i].type;
        }
        st = st->parent ? lookup_struct(g, st->parent) : NULL;
    }
    return NULL;
}

static int impl_has_method(CG* g, const char* struct_name, const char* method) {
    if (!g->sema || !g->sema->prog || !struct_name || !method) return 0;
    for (int i = 0; i < g->sema->prog->nitems; i++) {
        Item* it = g->sema->prog->items[i];
        if (it->kind != TOP_IMPL) continue;
        if (strcmp(it->im->target->name, struct_name) != 0) continue;
        for (int j = 0; j < it->im->nmethods; j++)
            if (strcmp(it->im->methods[j]->name, method) == 0) return 1;
    }
    return 0;
}

/* Walk the inheritance chain from `struct_name` to find which struct's impl
   defines `method`. Returns owner name (malloc'd) and sets *steps to how many
   `_base` hops from the receiver are needed. */
static char* find_method_owner(CG* g, const char* struct_name, const char* method, int* steps) {
    const char* cur = struct_name;
    *steps = 0;
    while (cur) {
        if (impl_has_method(g, cur, method)) {
            char* r = malloc(strlen(cur) + 1);
            if (!r) exit(1);
            strcpy(r, cur);
            return r;
        }
        StructDef* st = lookup_struct(g, cur);
        if (!st || !st->parent) break;
        cur = st->parent;
        (*steps)++;
    }
    return NULL;
}

/* Forward declarations for helpers defined later in this file. */

static void cg_add_local(CG* g, const char* name, AstType* type) {
    if (g->nlocals == g->cap) {
        g->cap = g->cap ? g->cap * 2 : 8;
        g->local_names = realloc(g->local_names, g->cap * sizeof *g->local_names);
        g->local_types = realloc(g->local_types, g->cap * sizeof *g->local_types);
        if (!g->local_names || !g->local_types) exit(1);
    }
    g->local_names[g->nlocals] = strdup(name);
    g->local_types[g->nlocals] = type;
    g->nlocals++;
}

static void cg_clear_locals(CG* g) {
    for (int i = 0; i < g->nlocals; i++) {
        free(g->local_names[i]);
        if (g->local_types[i]) free(g->local_types[i]);
    }
    g->nlocals = 0;
}

static AstType* cg_lookup_local(CG* g, const char* name) {
    for (int i = 0; i < g->nlocals; i++)
        if (strcmp(g->local_names[i], name) == 0) return g->local_types[i];
    return NULL;
}

/* Resolve the type of a base expression for member access / method calls.
   Handles identifier locals and (possibly nested) field accesses. */
static AstType* cg_resolve_type(CG* g, Expr* e) {
    if (!e) return NULL;
    /* During sema we stamp the static type of call/index expressions; prefer
       that (it correctly resolves generic method return types and locals). */
    if (e->type) return e->type;
    if (e->kind == E_IDENT) {
        if (strcmp(e->str, "self") == 0) return cg_lookup_local(g, "self");
        return cg_lookup_local(g, e->str);
    }
    if (e->kind == E_MEMBER) {
        AstType* bt = cg_resolve_type(g, e->a);
        if (!bt) return NULL;
        StructDef* st = lookup_struct(g, bt->name);
        if (!st) return NULL;
        for (int i = 0; i < st->nfields; i++)
            if (strcmp(st->fields[i].name, e->str) == 0) return st->fields[i].type;
        return NULL;
    }
    if (e->kind == E_CALL) {
        /* A call used as a method/field receiver, e.g. `v.items()[0].method()`.
           Prefer the sema-stamped type; otherwise fall back to the callee's
           declared return type (identifier callee). */
        if (e->type) return e->type;
        if (e->a && e->a->kind == E_IDENT) {
            Sym* sym = sema_lookup(g->sema, e->a->str);
            if (sym && sym->kind == SYM_FN && sym->fn && sym->fn->ret)
                return sym->fn->ret;
        }
        return NULL;
    }
    if (e->kind == E_INDEX) {
        /* Index into a pointer/container produced by a call (e.g. `v.items()[0]`).
           Prefer the sema-stamped type; otherwise resolve recursively. */
        if (e->type) return e->type;
        return NULL;
    }
    return NULL;
}

static void cg_indent(CG* g) {
    for (int i = 0; i < g->ind; i++) sb_append(&g->sb, "    ");
}

static void cg_items(CG* g, Expr** items, int n) {
    for (int i = 0; i < n; i++) {
        if (i) sb_append(&g->sb, ", ");
        cg_expr(g, items[i]);
    }
}

static char* type_to_str(AstType* t) {
    SB sb;
    sb_init(&sb);
    if (t->qual) sb_append(&sb, t->qual);
    sb_append(&sb, t->name);
    for (int i = 0; i < t->ptrs; i++) sb_append(&sb, "*");
    char* r = sb_strdup(&sb);
    sb_free(&sb);
    return r;
}

static StructDef* find_struct_def(CG* g, const char* name) {
    if (!g->sema || !g->sema->prog || !name) return NULL;
    for (int i = 0; i < g->sema->prog->nitems; i++) {
        Item* it = g->sema->prog->items[i];
        if (it->kind == TOP_STRUCT && strcmp(it->st->name, name) == 0) return it->st;
    }
    return NULL;
}

static void cg_type(CG* g, AstType* t) {
    if (!t) return;
    if (t->qual) sb_append(&g->sb, t->qual);
    sb_append(&g->sb, t->name);
    for (int i = 0; i < t->ptrs; i++) sb_append(&g->sb, "*");
}

/* Shallow clone of a type (args are shared with the AST; the AST outlives codegen). */
static AstType* cg_clone_type(AstType* src) {
    if (!src) return NULL;
    AstType* r = calloc(1, sizeof(AstType));
    if (!r) exit(1);
    r->name = src->name;
    r->qual = src->qual;
    r->ptrs = src->ptrs;
    return r;
}

static AstType* infer_let_type(CG* g, Expr* init) {
    if (!init) return NULL;
    if (init->type) return cg_clone_type(init->type);
    if (init->kind == E_CALL && init->a && init->a->kind == E_IDENT) {
        Sym* sym = sema_lookup(g->sema, init->a->str);
        if (sym && sym->kind == SYM_FN && sym->fn && sym->fn->ret)
            return cg_clone_type(sym->fn->ret);
        const char* c_ret = sema_lookup_cfunc(init->a->str);
        if (c_ret) {
            AstType* t = ast_type_new();
            t->name = strdup(c_ret);
            return t;
        }
    }
    if (init->kind == E_LITERAL && init->str[0] == '\"') {
        AstType* t = ast_type_new();
        t->name = strdup("char");
        t->ptrs = 1;
        return t;
    }
    return NULL;
}

static void cg_expr(CG* g, Expr* x) {
    if (!x) return;
    switch (x->kind) {
    case E_LITERAL:
    case E_IDENT:
        if (x->kind == E_IDENT) {
            const char* en = sema_lookup_variant(g->sema, x->str);
            if (en) {
                EnumDef* ed = find_enum_def(g, en);
                int vi = ed ? variant_index(ed, x->str) : -1;
                if (ed && vi >= 0 && ed->variants[vi].nfields == 0 && enum_has_payload(ed)) {
                    /* Unit variant living in a payload (tagged-union) enum:
                       construct a one-field struct literal, not a bare tag. */
                    sb_appendf(&g->sb, "((%s){ ._tag = ", en);
                    cg_emit_variant_const(g, en, x->str);
                    sb_append(&g->sb, " })");
                    break;
                }
                if (ed && ed->is_c_enum) {
                    /* Plain C-style enum: bare enumerator name. */
                    sb_append(&g->sb, x->str);
                    break;
                }
                cg_emit_variant_const(g, en, x->str);
                break;
            }
        }
        sb_append(&g->sb, x->str);
        break;
    case E_CALL:
        /* Variant constructors use `Variant { field: val }` (named-field brace
           form), not positional calls; fall through to method/external calls below. */
        /* Inline a method call: `obj.method(args)` where obj is a known
           struct value/pointer. Resolve the owner (walking inheritance),
           then emit `Owner_method(&obj, args)`. */
        if (x->a && x->a->kind == E_MEMBER) {
            Expr* m = x->a;
            AstType* base = cg_resolve_type(g, m->a);
            char* owner = NULL;
            int steps = 0;
            if (base && base->name) {
                owner = find_method_owner(g, base->name, m->str, &steps);
            }
            if (owner) {
                SB tmp; sb_init(&tmp);
                sb_appendf(&tmp, "%s_%s", owner, m->str);
                sb_append(&g->sb, tmp.data);
                sb_append(&g->sb, "(");
                int is_ptr = (base && base->ptrs > 0) || (m->a->kind == E_IDENT && strcmp(m->a->str, "self") == 0);
                if (is_ptr) {
                    if (steps > 0) {
                        sb_append(&g->sb, "&");
                        cg_expr(g, m->a);
                        for (int i = 0; i < steps; i++) sb_append(&g->sb, "->_base");
                    } else {
                        cg_expr(g, m->a);
                    }
                } else {
                    sb_append(&g->sb, "&");
                    cg_expr(g, m->a);
                    for (int i = 0; i < steps; i++) sb_append(&g->sb, "._base");
                }
                for (int i = 0; i < x->nitems; i++) {
                    sb_append(&g->sb, ", ");
                    cg_expr(g, x->items[i]);
                }
                sb_append(&g->sb, ")");
                sb_free(&tmp);
                free(owner);
                break;
            }
        }
        cg_call(g, x->a, x->items, x->nitems);
        break;
    case E_QUESTION: {
        /* Without built-in Result/Option, `expr?` is just `expr` (the parser
           still flags `?` usage on non-Result types in sema, but here we just
           emit the inner expression so any user-defined Result-like enum
           value flows through unchanged). */
        cg_expr(g, x->a);
        break;
    }
    case E_MATCH: {
        AstType* rt = x->type;
        AstType* st = x->a ? x->a->type : NULL;
        EnumDef* ed = (st && st->name) ? find_enum_def(g, st->name) : NULL;
        int payload = ed && enum_has_payload(ed);
        int is_void = !rt || (rt->name && strcmp(rt->name, "void") == 0 && rt->ptrs == 0);
        const char* pfx = ed ? ed->name : "unknown";
        sb_append(&g->sb, "({ ");
        if (!is_void) { cg_type(g, rt); sb_append(&g->sb, " __rk_mv; "); }
        if (payload) {
            sb_appendf(&g->sb, "%s __rk_match = ", pfx);
            cg_expr(g, x->a);
            sb_append(&g->sb, "; ");
            int saw_default = 0;
            for (int i = 0; i < x->nmarms; i++) {
                Expr* p = x->marms[i].pattern;
                int is_wild = p && p->kind == E_IDENT && strcmp(p->str, "_") == 0;
                if (is_wild) {
                    saw_default = 1;
                    if (i == 0) sb_append(&g->sb, "if (1) { ");
                    else        sb_append(&g->sb, "} else { ");
                } else {
                    const char* vname = (p && p->kind == E_IDENT) ? p->str :
                                         (p && p->kind == E_CALL && p->a &&
                                          p->a->kind == E_IDENT) ? p->a->str :
                                         (p && p->kind == E_NAMED_INIT && p->type
                                          && p->type->name) ? p->type->name : NULL;
                    if (i == 0) sb_append(&g->sb, "if (__rk_match._tag == ");
                    else        sb_append(&g->sb, "} else if (__rk_match._tag == ");
                    if (vname && ed) cg_emit_variant_const(g, pfx, vname);
                    sb_append(&g->sb, ") { ");
                    int vi = (vname && ed) ? variant_index(ed, vname) : -1;
                    if (vi >= 0 && (p->kind == E_CALL || p->kind == E_NAMED_INIT)) {
                        EnumVariant* v = &ed->variants[vi];
                        if (p && p->kind == E_NAMED_INIT) {
                            for (int k = 0; k < p->nnfields; k++) {
                                const char* bind = p->nfields[k].name;
                                for (int m = 0; m < v->nfields; m++) {
                                    if (strcmp(v->fields[m].name, bind) == 0) {
                                        cg_type(g, v->fields[m].type);
                                        sb_appendf(&g->sb, " %s = __rk_match._u.%s.%s; ",
                                                   p->nfields[k].e->str, vname, bind);
                                        break;
                                    }
                                }
                            }
                        } else if (p && p->kind == E_CALL) {
                            for (int k = 0; k < p->nitems && k < v->nfields; k++) {
                                Expr* b = p->items[k];
                                if (b && b->kind == E_IDENT && strcmp(b->str, "_") != 0) {
                                    cg_type(g, v->fields[k].type);
                                    sb_appendf(&g->sb, " %s = __rk_match._u.%s.%s; ",
                                               b->str, vname, v->fields[k].name);
                                }
                            }
                        }
                    }
                }
                if (is_void) {
                    cg_expr(g, x->marms[i].body);
                    sb_append(&g->sb, "; ");
                } else {
                    sb_append(&g->sb, "__rk_mv = (");
                    cg_expr(g, x->marms[i].body);
                    sb_append(&g->sb, "); ");
                }
            }
            if (!saw_default) sb_append(&g->sb, "} else { } ");
            else              sb_append(&g->sb, "} ");
        } else {
            sb_append(&g->sb, "switch (");
            cg_expr(g, x->a);
            sb_append(&g->sb, ") { ");
            for (int i = 0; i < x->nmarms; i++) {
                Expr* p = x->marms[i].pattern;
                int is_wild = p && p->kind == E_IDENT && strcmp(p->str, "_") == 0;
                if (is_wild) sb_append(&g->sb, "default: ");
                else         { sb_append(&g->sb, "case "); cg_expr(g, p); sb_append(&g->sb, ": "); }
                if (is_void) { cg_expr(g, x->marms[i].body); sb_append(&g->sb, "; "); }
                else         { sb_append(&g->sb, "__rk_mv = ("); cg_expr(g, x->marms[i].body); sb_append(&g->sb, "); "); }
                sb_append(&g->sb, "break; ");
            }
            sb_append(&g->sb, "} ");
        }
        if (!is_void) sb_append(&g->sb, " __rk_mv; ");
        sb_append(&g->sb, "})");
        break;
    }
    case E_MEMBER: {
        /* Field access, possibly through the inheritance chain. The base
           struct is embedded as a *value* member `_base`, so inherited
           fields require `obj._base...field`. The first connector uses
           `->` only when the receiver itself is a pointer; every hop into a
           `_base` member uses `.` because those are embedded values. */
        int use_arrow = 0;
        if (x->a && x->a->kind == E_IDENT) {
            AstType* t = cg_lookup_local(g, x->a->str);
            if (t && (t->ptrs > 0 || strcmp(x->a->str, "self") == 0))
                use_arrow = 1;
        }
        AstType* ot = cg_resolve_type(g, x->a);
        int steps = 0;
        if (ot && ot->name) {
            StructDef* st = lookup_struct(g, ot->name);
            if (st) steps = field_owner_levels(g, st, x->str);
        }
        cg_expr(g, x->a);
        for (int i = 0; i <= steps; i++) {
            if (i == 0 && use_arrow) sb_append(&g->sb, "->");
            else sb_append(&g->sb, ".");
            if (i < steps) sb_append(&g->sb, "_base");
            else sb_append(&g->sb, x->str);
        }
        break;
    }
    case E_ARROW:
        cg_expr(g, x->a);
        sb_append(&g->sb, "->");
        sb_append(&g->sb, x->str);
        break;
    case E_INDEX:
        if (g->bounds_check) {
            sb_append(&g->sb, "(rk_bounds(");
            cg_expr(g, x->b);
            sb_append(&g->sb, ", sizeof(");
            cg_expr(g, x->a);
            sb_append(&g->sb, ")/sizeof(");
            cg_expr(g, x->a);
            sb_append(&g->sb, "[0])), ");
            cg_expr(g, x->a);
            sb_append(&g->sb, "[");
            cg_expr(g, x->b);
            sb_append(&g->sb, "])");
        } else {
            cg_expr(g, x->a);
            sb_append(&g->sb, "[");
            cg_expr(g, x->b);
            sb_append(&g->sb, "]");
        }
        break;
    case E_UNARY:
        sb_append(&g->sb, x->str);
        cg_expr(g, x->a);
        break;
    case E_POST:
        cg_expr(g, x->a);
        sb_append(&g->sb, x->str);
        break;
    case E_BINARY:
        cg_expr(g, x->a);
        sb_append(&g->sb, " ");
        sb_append(&g->sb, x->str);
        sb_append(&g->sb, " ");
        cg_expr(g, x->b);
        break;
    case E_TERNARY:
        cg_expr(g, x->a);
        sb_append(&g->sb, " ? ");
        cg_expr(g, x->b);
        sb_append(&g->sb, " : ");
        cg_expr(g, x->c);
        break;
    case E_ASSIGN: {
        cg_expr(g, x->a);
        sb_append(&g->sb, " ");
        sb_append(&g->sb, x->str);
        sb_append(&g->sb, " ");
        cg_expr(g, x->b);
        break;
    }
    case E_CAST:
        sb_append(&g->sb, "(");
        cg_type(g, x->type);
        sb_append(&g->sb, ")");
        cg_expr(g, x->a);
        break;
    case E_COMPOUND:
        sb_append(&g->sb, "{");
        for (int i = 0; i < x->ncitems; i++) {
            if (i) sb_append(&g->sb, ", ");
            if (x->citems[i].name) {
                sb_append(&g->sb, x->citems[i].name);
                sb_append(&g->sb, " = ");
            }
            cg_expr(g, x->citems[i].e);
        }
        sb_append(&g->sb, "}");
        break;
    case E_NAMED_INIT:
        /* sum variant constructor: `Circle { r: 2.0 }` -> canonical tagged init. */
        if (x->type && x->type->name) {
            const char* fn = x->type->name;
            const char* en = sema_lookup_variant(g->sema, fn);
            if (en) {
                EnumDef* ed = find_enum_def(g, en);
                int vi = ed ? variant_index(ed, fn) : -1;
                if (ed && vi >= 0) {
                    EnumVariant* v = &ed->variants[vi];
                    sb_appendf(&g->sb, "((%s){ ._tag = %s_%s", en, en, fn);
                    if (v->nfields > 0) {
                        sb_appendf(&g->sb, ", ._u.%s = (", fn);
                        sb_appendf(&g->sb, "%s_%s_payload){ ", en, fn);
                        for (int i = 0; i < x->nnfields; i++) {
                            if (i) sb_append(&g->sb, ", ");
                            sb_append(&g->sb, ".");
                            sb_append(&g->sb, x->nfields[i].name);
                            sb_append(&g->sb, " = ");
                            cg_expr(g, x->nfields[i].e);
                        }
                        sb_append(&g->sb, " }");
                    }
                    sb_append(&g->sb, " })");
                    break;
                }
            }
        }
        int has_type = (x->type && x->type->name && x->type->name[0]);
        if (has_type) {
            sb_appendf(&g->sb, "((%s){", x->type->name);
        } else {
            sb_append(&g->sb, "{");
        }
        StructDef* target_st = (x->type && x->type->name) ? lookup_struct(g, x->type->name) : NULL;
        for (int i = 0; i < x->nnfields; i++) {
            if (i) sb_append(&g->sb, ", ");
            /* C designated initializer: `._base.field = value` if inherited, else `.field = value`. */
            sb_append(&g->sb, ".");
            if (target_st) {
                int levels = field_owner_levels(g, target_st, x->nfields[i].name);
                for (int l = 0; l < levels; l++) {
                    sb_append(&g->sb, "_base.");
                }
            }
            sb_append(&g->sb, x->nfields[i].name);
            sb_append(&g->sb, " = ");
            cg_expr(g, x->nfields[i].e);
        }
        if (has_type) {
            sb_append(&g->sb, "})");
        } else {
            sb_append(&g->sb, "}");
        }
        break;
    case E_BRACE_INIT:
        sb_append(&g->sb, "{");
        for (int i = 0; i < x->nitems; i++) {
            if (i) sb_append(&g->sb, ", ");
            cg_expr(g, x->items[i]);
        }
        sb_append(&g->sb, "}");
        break;
    case E_PAREN:
        sb_append(&g->sb, "(");
        cg_expr(g, x->a);
        sb_append(&g->sb, ")");
        break;
    case E_SIZEOF_T:
        sb_append(&g->sb, "sizeof(");
        cg_type(g, x->type);
        sb_append(&g->sb, ")");
        break;
    case E_SIZEOF_E:
        sb_append(&g->sb, "sizeof(");
        cg_expr(g, x->a);
        sb_append(&g->sb, ")");
        break;
    case E_ARR_LIT:
        sb_append(&g->sb, "{");
        for (int i = 0; i < x->nitems; i++) {
            if (i) sb_append(&g->sb, ", ");
            cg_expr(g, x->items[i]);
        }
        sb_append(&g->sb, "}");
        break;
    case E_RANGE:
        cg_expr(g, x->a);
        sb_append(&g->sb, "..=");
        cg_expr(g, x->b);
        break;
    }
}

static void cg_param(CG* g, Param* p, AstType* receiver) {
    int is_self = (strcmp(p->name, "self") == 0);
    if (is_self && receiver) {
        sb_append(&g->sb, receiver->name);
        sb_append(&g->sb, "* ");
    } else if (p->type) {
        cg_type(g, p->type);
        sb_append(&g->sb, " ");
    }
    sb_append(&g->sb, p->name);
}

static void cg_call(CG* g, Expr* callee, Expr** args, int nargs) {
    if (callee->kind == E_MEMBER) {
        AstType* base_type = cg_resolve_type(g, callee->a);
        if (base_type) {
            int is_self = (callee->a->kind == E_IDENT &&
                           strcmp(callee->a->str, "self") == 0) ||
                          base_type->ptrs > 0;
            char* owner = NULL;
            int steps = 0;
            if (base_type && base_type->name) {
                owner = find_method_owner(g, base_type->name, callee->str, &steps);
            }
            size_t n = strlen(owner ? owner : base_type->name) + strlen(callee->str) + 2;
            char* func = malloc(n);
            if (!func) exit(1);
            snprintf(func, n, "%s_%s", owner ? owner : base_type->name, callee->str);
            sb_append(&g->sb, func);
            sb_append(&g->sb, "(");
            if (is_self) {
                if (steps == 0) {
                    cg_expr(g, callee->a);
                } else {
                    sb_append(&g->sb, "&");
                    cg_expr(g, callee->a);
                    for (int i = 0; i < steps; i++) sb_append(&g->sb, "->_base");
                }
            } else {
                sb_append(&g->sb, "&");
                cg_expr(g, callee->a);
                for (int i = 0; i < steps; i++) sb_append(&g->sb, "._base");
            }
            for (int i = 0; i < nargs; i++) {
                sb_append(&g->sb, ", ");
                cg_expr(g, args[i]);
            }
            sb_append(&g->sb, ")");
            free(func);
            free(owner);
            return;
        }
        /* E_MEMBER callee with unresolvable base type: emit as-is */
    }

    /* E_MEMBER callee with an unresolvable base type: fall back to a direct
       member call `self.method(args)`. */
    if (callee->kind == E_MEMBER && callee->a && callee->a->kind == E_IDENT &&
        strcmp(callee->a->str, "self") == 0) {
        AstType* st = cg_lookup_local(g, "self");
        if (st && st->name) {
            char* owner = NULL;
            int steps = 0;
            owner = find_method_owner(g, st->name, callee->str, &steps);
            if (owner) {
                SB tmp; sb_init(&tmp);
                sb_appendf(&tmp, "%s_%s", owner, callee->str);
                sb_append(&g->sb, tmp.data);
                sb_append(&g->sb, "(");
                cg_expr(g, callee->a);
                for (int i = 0; i < nargs; i++) {
                    sb_append(&g->sb, ", ");
                    cg_expr(g, args[i]);
                }
                sb_append(&g->sb, ")");
                sb_free(&tmp);
                free(owner);
                return;
            }
            free(owner);
        }
    }
    cg_expr(g, callee);
    sb_append(&g->sb, "(");
    for (int i = 0; i < nargs; i++) {
        if (i) sb_append(&g->sb, ", ");
        cg_expr(g, args[i]);
    }
    sb_append(&g->sb, ")");
}

static void cg_decl(CG* g, Decl* d) {
    if (!d) return;
    if (d->style == DECL_LET) {
        if (d->type) {
            cg_type(g, d->type);
            sb_append(&g->sb, " ");
        } else {
            AstType* inferred = infer_let_type(g, d->init);
        if (inferred) {
            cg_type(g, inferred);
            sb_append(&g->sb, " ");
        } else {
            sb_append(&g->sb, "auto ");
        }
        }
        sb_append(&g->sb, d->name);
    } else if (d->style == DECL_TYPED) {
        cg_type(g, d->type);
        sb_append(&g->sb, " ");
        sb_append(&g->sb, d->name);
    } else {
        cg_type(g, d->type);
        sb_append(&g->sb, " ");
        sb_append(&g->sb, d->name);
        if (d->dim) {
            sb_append(&g->sb, "[");
            cg_expr(g, d->dim);
            sb_append(&g->sb, "]");
        }
    }
    if (d->init) {
        sb_append(&g->sb, " = ");
        cg_expr(g, d->init);
    } else {
        sb_append(&g->sb, " = {0}");
    }
}

static void cg_fn_prototype(CG* g, FnDef* f, AstType* receiver) {
    if (f->is_extern) return;
    int has_self = (f->nparams > 0 && strcmp(f->params[0].name, "self") == 0);
    cg_indent(g);
    if (f->ret) {
        cg_type(g, f->ret);
        sb_append(&g->sb, " ");
    } else {
        sb_append(&g->sb, "void ");
    }
    sb_append(&g->sb, f->name);
    sb_append(&g->sb, "(");
    if (has_self) {
        cg_param(g, &f->params[0], receiver);
    }
    for (int i = has_self ? 1 : 0; i < f->nparams; i++) {
        if (i > (has_self ? 1 : 0) || has_self) sb_append(&g->sb, ", ");
        cg_param(g, &f->params[i], NULL);
    }
    sb_append(&g->sb, ");\n");
}

static void cg_fn(CG* g, FnDef* f, int ind, AstType* receiver) {
    /* `extern fn` declares a C function that is provided by an #include (or a
       [[raw]] region); trust it and emit nothing — not even a prototype. */
    if (f->is_extern) return;
    int has_self = (f->nparams > 0 && strcmp(f->params[0].name, "self") == 0);
    if (has_self && receiver) {
        AstType* self_t = calloc(1, sizeof *self_t);
        if (!self_t) exit(1);
        self_t->name = strdup(receiver->name);
        self_t->ptrs = 1;
        cg_add_local(g, "self", self_t);
    }
    cg_indent(g);
    if (f->ret) {
        cg_type(g, f->ret);
        sb_append(&g->sb, " ");
    } else {
        sb_append(&g->sb, "void ");
    }
    sb_append(&g->sb, f->name);
    sb_append(&g->sb, "(");
    if (has_self) {
        cg_param(g, &f->params[0], receiver);
    }
    for (int i = has_self ? 1 : 0; i < f->nparams; i++) {
        if (i > (has_self ? 1 : 0) || has_self) sb_append(&g->sb, ", ");
        cg_param(g, &f->params[i], NULL);
    }
    sb_append(&g->sb, ")");
    if (!f->body) {
        sb_append(&g->sb, ";\n");
        return;
    }
    sb_append(&g->sb, "\n");
    cg_indent(g);
    AstType* saved_ret = g->cur_ret;
    g->cur_ret = f->ret;
    cg_stmt(g, f->body);
    g->cur_ret = saved_ret;
    sb_append(&g->sb, "\n");
}

static void cg_struct(CG* g, StructDef* st) {
    sb_append(&g->sb, "typedef struct ");
    sb_append(&g->sb, st->name);
    if (st->nfields == 0) {
        sb_append(&g->sb, ";\n");
        return;
    }
    sb_append(&g->sb, " {\n");
    if (st->parent) {

        cg_indent(g);
        sb_append(&g->sb, "    struct ");
        sb_append(&g->sb, st->parent);
        sb_append(&g->sb, " _base;\n");
    }
    for (int i = 0; i < st->nfields; i++) {
        StructField* f = &st->fields[i];
        cg_indent(g);
        sb_append(&g->sb, "    ");
        cg_type(g, f->type);
        sb_append(&g->sb, " ");
        sb_append(&g->sb, f->name);
        if (f->dim) {
            sb_append(&g->sb, "[");
            cg_expr(g, f->dim);
            sb_append(&g->sb, "]");
        }
        sb_append(&g->sb, ";\n");
    }
    cg_indent(g);
    sb_append(&g->sb, "} ");
    sb_append(&g->sb, st->name);
    sb_append(&g->sb, ";\n");
}

static void cg_struct_ordered(CG* g, StructDef* st, const char** emitted, int* nemitted) {
    if (!st) return;
    for (int i = 0; i < *nemitted; i++) {
        if (strcmp(emitted[i], st->name) == 0) return; /* already emitted */
    }
    if (st->parent) {
        StructDef* pst = lookup_struct(g, st->parent);
        if (pst) cg_struct_ordered(g, pst, emitted, nemitted);
    }
    cg_struct(g, st);
    sb_append(&g->sb, "\n");
    emitted[(*nemitted)++] = st->name;
}

static EnumDef* find_enum_def(CG* g, const char* name) {
    if (!name) return NULL;
    if (g && g->sema && g->sema->prog) {
        Program* prog = g->sema->prog;
        for (int i = 0; i < prog->nitems; i++) {
            Item* it = prog->items[i];
            if (it->kind == TOP_ENUM && strcmp(it->ed->name, name) == 0)
                return it->ed;
        }
    }
    return NULL;
}

/* Index of a variant by name, or -1. Used by match/variant-constructor codegen. */
static int variant_index(EnumDef* ed, const char* name) {
    if (!ed || !name) return -1;
    for (int i = 0; i < ed->nvariants; i++)
        if (strcmp(ed->variants[i].name, name) == 0) return i;
    return -1;
}

/* C tag constant name for a variant: `<Enum>_<Variant>` (e.g. `Shape_Circle`). */
static void cg_emit_variant_const(CG* g, const char* enum_name, const char* variant) {
    sb_appendf(&g->sb, "%s_%s", enum_name, variant);
}

/* True if any variant of `ed` carries a payload. */
static int enum_has_payload(EnumDef* ed) {
    for (int i = 0; i < ed->nvariants; i++)
        if (ed->variants[i].nfields > 0) return 1;
    return 0;
}

/* Unit enum -> C `typedef enum { E_A=0, E_B=1 } E;`.
   Payload (algebraic) enum -> tagged union:
     typedef enum { E_A=0, E_B } E_tag;
     typedef struct E_B_payload { <T> f0; ... } E_B_payload;  (per payload variant)
     typedef struct E { E_tag _tag; union { E_B_payload B; ... } _u; } E; */
static void cg_enum(CG* g, EnumDef* ed) {
    if (ed->is_c_enum) {
        sb_append(&g->sb, "typedef enum { ");
        for (int i = 0; i < ed->nvariants; i++) {
            if (i) sb_append(&g->sb, ", ");
            sb_append(&g->sb, ed->variants[i].name);
        }
        sb_appendf(&g->sb, " } %s;\n", ed->name);
        return;
    }
    if (!enum_has_payload(ed)) {
        sb_append(&g->sb, "typedef enum { ");
        for (int i = 0; i < ed->nvariants; i++) {
            if (i) sb_append(&g->sb, ", ");
            cg_emit_variant_const(g, ed->name, ed->variants[i].name);
            sb_appendf(&g->sb, " = %d", i);
        }
        sb_append(&g->sb, " } ");
        sb_append(&g->sb, ed->name);
        sb_append(&g->sb, ";\n");
        return;
    }
    /* payload (algebraic) enum */
    char tagname[256];
    snprintf(tagname, sizeof tagname, "%s_tag", ed->name);
    sb_append(&g->sb, "typedef enum { ");
    for (int i = 0; i < ed->nvariants; i++) {
        if (i) sb_append(&g->sb, ", ");
        cg_emit_variant_const(g, ed->name, ed->variants[i].name);
        if (ed->variants[i].nfields == 0)
            sb_appendf(&g->sb, " = %d", i);
    }
    sb_appendf(&g->sb, " } %s;\n", tagname);
    for (int i = 0; i < ed->nvariants; i++) {
        if (ed->variants[i].nfields == 0) continue;
        sb_appendf(&g->sb, "typedef struct %s_%s_payload { ", ed->name, ed->variants[i].name);
        for (int j = 0; j < ed->variants[i].nfields; j++) {
            cg_type(g, ed->variants[i].fields[j].type);
            sb_appendf(&g->sb, " %s;", ed->variants[i].fields[j].name);
        }
        sb_append(&g->sb, " } ");
        sb_appendf(&g->sb, "%s_%s_payload;\n", ed->name, ed->variants[i].name);
    }
    sb_appendf(&g->sb, "typedef struct %s { %s _tag; union { ", ed->name, tagname);
    for (int i = 0; i < ed->nvariants; i++) {
        if (ed->variants[i].nfields == 0) continue;
        sb_appendf(&g->sb, "%s_%s_payload %s; ",
                   ed->name, ed->variants[i].name, ed->variants[i].name);
    }
    sb_append(&g->sb, "} _u; } ");
    sb_append(&g->sb, ed->name);
    sb_append(&g->sb, ";\n");
}

static void cg_impl(CG* g, ImplDef* im) {
    char* tyname = type_to_str(im->target);
    AstType receiver;
    memset(&receiver, 0, sizeof receiver);
    receiver.name = tyname;
    for (int i = 0; i < im->nmethods; i++) {
        FnDef* method = im->methods[i];
        char mangled[256];
        snprintf(mangled, sizeof(mangled), "%s_%s", tyname, method->name);
        FnDef copy = *method;
        copy.name = mangled;
        cg_fn(g, &copy, 1, &receiver);
        /* cg_fn registers `self` and params as locals; clear them so the
           next method starts fresh (and so the freed `tyname` is not reused). */
        cg_clear_locals(g);
    }
    free(tyname);
}

static void cg_stmt(CG* g, Stmt* s) {
    if (!s) return;
    switch (s->kind) {
    case S_BLOCK: {
        DeferFrame* f = &g->defer_stack[g->defer_depth++];
        f->count = 0; f->cap = 0; f->stmts = NULL; f->loop_depth = g->loop_depth;
        sb_append(&g->sb, "{\n");
        g->ind++;
        for (int i = 0; i < s->nstmts; i++) {
            if (s->stmts[i]->kind == S_DEFER)
                cg_add_defer(g, s->stmts[i]->defer);
            else
                cg_stmt(g, s->stmts[i]);
        }
        cg_emit_defers(g, f);
        g->ind--;
        cg_indent(g);
        sb_append(&g->sb, "}\n");
        g->defer_depth--;
        break;
    }
    case S_EXPR:
        cg_expr(g, s->e);
        sb_append(&g->sb, ";\n");
        break;
    case S_DECL:
        if (s->decl && s->decl->name) {
            AstType* t = s->decl->type ? cg_clone_type(s->decl->type) : NULL;
            cg_add_local(g, s->decl->name, t);
        }
        cg_decl(g, s->decl);
        sb_append(&g->sb, ";\n");
        break;
    case S_IF:
        sb_append(&g->sb, "if (");
        cg_expr(g, s->cond);
        sb_append(&g->sb, ") {\n");
        g->ind++;
        cg_stmt(g, s->then);
        g->ind--;
        cg_indent(g);
        sb_append(&g->sb, "}\n");
        if (s->els) {
            cg_indent(g);
            sb_append(&g->sb, "else {\n");
            g->ind++;
            cg_stmt(g, s->els);
            g->ind--;
            cg_indent(g);
            sb_append(&g->sb, "}\n");
        }
        break;
    case S_WHILE:
        sb_append(&g->sb, "while (");
        cg_expr(g, s->cond);
        sb_append(&g->sb, ") {\n");
        g->ind++;
        g->loop_depth++;
        cg_stmt(g, s->body);
        g->loop_depth--;
        g->ind--;
        cg_indent(g);
        sb_append(&g->sb, "}\n");
        break;
    case S_FOR:
        sb_append(&g->sb, "for (");
        if (s->init_decl) cg_decl(g, s->init_decl);
        else if (s->init_expr) cg_expr(g, s->init_expr);
        sb_append(&g->sb, "; ");
        if (s->cond) cg_expr(g, s->cond);
        sb_append(&g->sb, "; ");
        if (s->step) cg_expr(g, s->step);
        sb_append(&g->sb, ") {\n");
        g->ind++;
        g->loop_depth++;
        cg_stmt(g, s->body);
        g->loop_depth--;
        g->ind--;
        cg_indent(g);
        sb_append(&g->sb, "}\n");
        break;
    case S_FORIN:
        if (s->iter && s->iter->kind == E_ARR_LIT) {
            Expr* arr = s->iter;
            char* tmp = malloc(64);
            snprintf(tmp, 64, "__arr%d", g->result_count++);
            sb_append(&g->sb, "int ");
            sb_append(&g->sb, tmp);
            sb_append(&g->sb, "[] = ");
            cg_expr(g, arr);
            sb_append(&g->sb, ";\n");
            cg_indent(g);
            sb_append(&g->sb, "for (int __i = 0; __i < ");
            char cnt[16];
            snprintf(cnt, sizeof cnt, "%d", arr->nitems);
            sb_append(&g->sb, cnt);
            sb_append(&g->sb, "; __i++) {\n");
            g->ind++;
            cg_indent(g);
            sb_append(&g->sb, "auto ");
            sb_append(&g->sb, s->var);
            sb_append(&g->sb, " = ");
            sb_append(&g->sb, tmp);
            sb_append(&g->sb, "[__i];\n");
            g->loop_depth++;
            cg_stmt(g, s->body);
            g->loop_depth--;
            g->ind--;
            cg_indent(g);
            sb_append(&g->sb, "}\n");
            free(tmp);
        } else {
            sb_append(&g->sb, "for (");
            sb_append(&g->sb, s->var);
            sb_append(&g->sb, " in ");
            cg_expr(g, s->iter);
            sb_append(&g->sb, ") {\n");
            g->ind++;
            g->loop_depth++;
            cg_stmt(g, s->body);
            g->loop_depth--;
            g->ind--;
            cg_indent(g);
            sb_append(&g->sb, "}\n");
        }
        break;
    case S_SWITCH:
        sb_append(&g->sb, "switch (");
        cg_expr(g, s->e);
        sb_append(&g->sb, ") {\n");
        g->ind++;
        for (int i = 0; i < s->narms; i++) {
            SwitchArm* a = &s->arms[i];
            cg_indent(g);
            if (a->is_default) {
                sb_append(&g->sb, "default");
            } else {
                for (int j = 0; j < a->nlabels; j++) {
                    if (j) sb_append(&g->sb, " ");
                    sb_append(&g->sb, "case ");
                    cg_expr(g, a->labels[j]);
                }
            }
            sb_append(&g->sb, ":\n");
            if (a->arrow) {
                cg_stmt(g, a->body);
                cg_indent(g);
                sb_append(&g->sb, "break;\n");
            } else {
                cg_stmt(g, a->body);
            }
        }
        g->ind--;
        cg_indent(g);
        sb_append(&g->sb, "}\n");
        break;
    case S_MATCH: {
        AstType* mt = s->e ? s->e->type : NULL;
        EnumDef* ed = NULL;
        int payload = 0;
        if (mt && mt->name) {
            ed = find_enum_def(g, mt->name);
            payload = ed && enum_has_payload(ed);
        }
        if (!payload) {
            /* Unit enum / literal match: C switch/case. */
            sb_append(&g->sb, "switch (");
            cg_expr(g, s->e);
            sb_append(&g->sb, ") {\n");
            g->ind++;
            for (int i = 0; i < s->nmarms; i++) {
                MatchArm* a = &s->marms[i];
                cg_indent(g);
                if (a->pattern && a->pattern->kind == E_IDENT &&
                    strcmp(a->pattern->str, "_") == 0) {
                    sb_append(&g->sb, "default:\n");
                } else {
                    sb_append(&g->sb, "case ");
                    cg_expr(g, a->pattern);
                    sb_append(&g->sb, ":\n");
                }
                cg_indent(g);
                cg_expr(g, a->body);
                sb_append(&g->sb, ";\n");
                cg_indent(g);
                sb_append(&g->sb, "break;\n");
            }
            g->ind--;
            cg_indent(g);
            sb_append(&g->sb, "}\n");
            break;
        }
        /* Algebraic payload enum: lower to an if/else chain on the tag,
           binding payload fields by position. */
        sb_append(&g->sb, "do {\n");
        g->ind++;
        cg_indent(g);
        sb_appendf(&g->sb, "%s __rk_match = ", mt->name);
        cg_expr(g, s->e);
        sb_append(&g->sb, ";\n");
        int saw_default = 0;
        for (int i = 0; i < s->nmarms; i++) {
            MatchArm* a = &s->marms[i];
            Expr* p = a->pattern;
            int is_wild = p && p->kind == E_IDENT && strcmp(p->str, "_") == 0;
            if (is_wild) {
                saw_default = 1;
                if (i == 0) { sb_appendf(&g->sb, "if (1) {\n"); }
                else        { cg_indent(g); sb_append(&g->sb, "} else {\n"); }
                g->ind++;
                cg_indent(g);
                cg_expr(g, a->body);
                sb_append(&g->sb, ";\n");
                g->ind--;
                continue;
            }
            const char* vname = NULL;
            int vi = -1;
            if (p && p->kind == E_IDENT) vname = p->str;
            else if (p && p->kind == E_CALL && p->a && p->a->kind == E_IDENT)
                vname = p->a->str;
            else if (p && p->kind == E_NAMED_INIT && p->type && p->type->name)
                vname = p->type->name;
            if (vname && ed) vi = variant_index(ed, vname);
            if (i == 0) { cg_indent(g); sb_append(&g->sb, "if ("); }
            else        { cg_indent(g); sb_append(&g->sb, "} else if ("); }
            sb_append(&g->sb, "__rk_match._tag == ");
            cg_emit_variant_const(g, ed->name, vname);
            sb_append(&g->sb, ") {\n");
            g->ind++;
            if (vi >= 0) {
                EnumVariant* v = &ed->variants[vi];
                if (p && p->kind == E_NAMED_INIT) {
                    for (int k = 0; k < p->nnfields; k++) {
                        const char* bind = p->nfields[k].name;
                        for (int m = 0; m < v->nfields; m++) {
                            if (strcmp(v->fields[m].name, bind) == 0) {
                                cg_type(g, v->fields[m].type);
                                sb_appendf(&g->sb, " %s = __rk_match._u.%s.%s;\n",
                                           p->nfields[k].e->str, vname, bind);
                                break;
                            }
                        }
                    }
                } else {
                    int nargs = (p && p->kind == E_CALL) ? p->nitems : 0;
                    for (int k = 0; k < nargs && k < v->nfields; k++) {
                        Expr* b = p->items[k];
                        if (b && b->kind == E_IDENT && strcmp(b->str, "_") != 0) {
                            cg_type(g, v->fields[k].type);
                            sb_appendf(&g->sb, " %s = __rk_match._u.%s.%s;\n",
                                       b->str, vname, v->fields[k].name);
                        }
                    }
                }
            }
            cg_indent(g);
            cg_expr(g, a->body);
            sb_append(&g->sb, ";\n");
            g->ind--;
        }
        if (!saw_default) {
            cg_indent(g);
            sb_append(&g->sb, "} else { /* non-exhaustive */ }\n");
        } else {
            cg_indent(g);
            sb_append(&g->sb, "}\n");
        }
        g->ind--;
        cg_indent(g);
        sb_append(&g->sb, "} while (0);\n");
        break;
    }
    case S_RETURN:
        cg_emit_all_defers(g);
        sb_append(&g->sb, "return");
        if (s->e) {
            sb_append(&g->sb, " ");
            cg_expr(g, s->e);
        }
        sb_append(&g->sb, ";\n");
        break;
    case S_BREAK: {
        cg_emit_loop_defers(g);
        sb_append(&g->sb, "break;\n");
        break;
    }
    case S_CONTINUE: {
        cg_emit_loop_defers(g);
        sb_append(&g->sb, "continue;\n");
        break;
    }
    case S_EMPTY:
        sb_append(&g->sb, ";\n");
        break;
    }
}

static void cg_program(CG* g, Program* prog);

static void cg_program(CG* g, Program* prog) {
    sb_append(&g->sb, "#define _DEFAULT_SOURCE\n#define _POSIX_C_SOURCE 200809L\n\n");
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_RAW) {
            sb_appendn(&g->sb, it->raw, it->raw_len);
            if (it->raw_len > 0 && it->raw[it->raw_len - 1] != '\n') {
                sb_append(&g->sb, "\n");
            }
        }
    }
    sb_append(&g->sb, "\n");

    if (g->bounds_check) {
        sb_append(&g->sb,
                  "static _Noreturn void rk_bounds_fail(void){\n"
                  "    fprintf(stderr, \"Rook: index out of bounds\\n\");\n"
                  "    abort();\n"
                  "}\n"
                  "static long rk_bounds(long i, long n){\n"
                  "    if (i < 0 || i >= n) rk_bounds_fail();\n"
                  "    return i;\n"
                  "}\n\n");
    }

    /* Forward struct declarations */
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_STRUCT) {
            sb_appendf(&g->sb, "typedef struct %s %s;\n", it->st->name, it->st->name);
        }
    }
    sb_append(&g->sb, "\n");

    /* Plain struct definitions (ordered base first). */
    const char* emitted_structs[256];
    int nemitted_structs = 0;
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_STRUCT) {
            cg_struct_ordered(g, it->st, emitted_structs, &nemitted_structs);
        }
    }

    /* Plain enum definitions — emitted as C enums / tagged unions. */
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_ENUM) {
            cg_enum(g, it->ed);
        }
    }

    /* Function and method prototypes */
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_FN && !it->fn->is_extern) {
            cg_fn_prototype(g, it->fn, NULL);
        } else if (it->kind == TOP_IMPL) {
            char* tyname = type_to_str(it->im->target);
            AstType receiver;
            memset(&receiver, 0, sizeof receiver);
            receiver.name = tyname;
            for (int j = 0; j < it->im->nmethods; j++) {
                FnDef* m = it->im->methods[j];
                char mangled[256];
                snprintf(mangled, sizeof(mangled), "%s_%s", tyname, m->name);
                FnDef copy = *m;
                copy.name = mangled;
                cg_fn_prototype(g, &copy, &receiver);
            }
            free(tyname);
        }
    }
    sb_append(&g->sb, "\n");

    /* Top-level functions / impls. */
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        switch (it->kind) {
        case TOP_RAW: break;
        case TOP_FN: cg_fn(g, it->fn, 0, NULL); cg_clear_locals(g); break;
        case TOP_STRUCT: break; /* already emitted above */
        case TOP_IMPL: cg_impl(g, it->im); break;
        default: break;
        }
    }
}

char* codegen_header(Sema* sema, Program* prog, int* out_len, const char* mod_name) {
    CG g;
    memset(&g, 0, sizeof g);
    sb_init(&g.sb);
    g.sema = sema;
    g.ind = 0;

    char guard[256];
    snprintf(guard, sizeof(guard), "ROKADE_HEADER_%s_H", mod_name ? mod_name : "MODULE");
    for (char* p = guard; *p; p++) {
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
        else if (!((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))) *p = '_';
    }

    sb_appendf(&g.sb, "#ifndef %s\n#define %s\n\n#define _DEFAULT_SOURCE\n#define _POSIX_C_SOURCE 200809L\n\n", guard, guard);

    /* Emit raw includes from TOP_RAW (e.g. #include <...>) */
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_RAW) {
            const char* p = it->raw;
            const char* end = it->raw + it->raw_len;
            while (p < end) {
                const char* nl = memchr(p, '\n', (size_t)(end - p));
                int line_len = nl ? (int)(nl - p + 1) : (int)(end - p);
                const char* s = p;
                while (s < p + line_len && (*s == ' ' || *s == '\t')) s++;
                if (s < p + line_len && *s == '#') {
                    sb_appendn(&g.sb, p, line_len);
                }
                p = nl ? nl + 1 : end;
            }
        }
    }
    sb_append(&g.sb, "\n");

    /* Forward struct declarations */
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_STRUCT) {
            sb_appendf(&g.sb, "typedef struct %s %s;\n", it->st->name, it->st->name);
        }
    }
    sb_append(&g.sb, "\n");

    /* Struct definitions */
    const char* emitted_structs[256];
    int nemitted_structs = 0;
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_STRUCT) {
            cg_struct_ordered(&g, it->st, emitted_structs, &nemitted_structs);
        }
    }

    /* Enum definitions */
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_ENUM) {
            cg_enum(&g, it->ed);
        }
    }

    /* Function and method prototypes */
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_FN && !it->fn->is_extern) {
            cg_fn_prototype(&g, it->fn, NULL);
        } else if (it->kind == TOP_IMPL) {
            char* tyname = type_to_str(it->im->target);
            AstType receiver;
            memset(&receiver, 0, sizeof receiver);
            receiver.name = tyname;
            for (int j = 0; j < it->im->nmethods; j++) {
                FnDef* m = it->im->methods[j];
                char mangled[256];
                snprintf(mangled, sizeof(mangled), "%s_%s", tyname, m->name);
                FnDef copy = *m;
                copy.name = mangled;
                cg_fn_prototype(&g, &copy, &receiver);
            }
            free(tyname);
        }
    }

    sb_appendf(&g.sb, "\n#endif /* %s */\n", guard);

    if (out_len) *out_len = g.sb.len;
    char* out = sb_strdup(&g.sb);
    sb_free(&g.sb);
    return out;
}
char* codegen_program(Sema* sema, Program* prog, int* out_len, int bounds_check) {
    CG g;
    memset(&g, 0, sizeof g);
    sb_init(&g.sb);
    g.sema = sema;
    g.ind = 0;
    g.bounds_check = bounds_check;
    cg_program(&g, prog);
    if (out_len) *out_len = g.sb.len;
    char* out = sb_strdup(&g.sb);
    sb_free(&g.sb);
    for (int i = 0; i < g.nlocals; i++) free(g.local_names[i]);
    free(g.local_names);
    free(g.local_types);
    for (int i = 0; i < 64; i++)
        if (g.defer_stack[i].stmts) free(g.defer_stack[i].stmts);
    return out;
}