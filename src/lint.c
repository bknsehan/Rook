#include "lint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"

/* Optional expanded-source for marker remapping (CLI mode). NULL = raw. */
static const char* g_lint_src = NULL;

static void lint_emit(Sema* sema, int line, int col, int end_col,
                      int severity, const char* code, const char* msg) {
    int rline = line, rcol = col;
    char rfile[512] = "";
    if (g_lint_src && line >= 1) {
        int ol = 0, oc = 0;
        if (diag_resolve_linecol(g_lint_src, line, col,
                                 rfile, sizeof rfile, &ol, &oc)) {
            if (ol >= 1) rline = ol;
            if (oc >= 1) rcol = oc;
            /* Keep end_col width relative. */
            int w = end_col > col ? end_col - col : 1;
            end_col = rcol + w;
        }
    }
    int before = sema_diag_count(sema);
    sema_diag_add(sema, rline, rcol, end_col, severity, code, msg);
    if (rfile[0] && sema_diag_count(sema) > before) {
        SemaDiag* d = sema_diag_get(sema, sema_diag_count(sema) - 1);
        if (d) snprintf(d->file, sizeof d->file, "%s", rfile);
    }
}

/* ─── ident-use collection ─────────────────────────────────────────── */

typedef struct {
    const char** names;
    int n;
    int cap;
} NameSet;

static void names_add(NameSet* s, const char* name) {
    if (!s || !name || !name[0]) return;
    if (s->n >= s->cap) {
        int ncap = s->cap ? s->cap * 2 : 32;
        const char** nn = realloc(s->names, (size_t)ncap * sizeof *nn);
        if (!nn) exit(1);
        s->names = nn;
        s->cap = ncap;
    }
    s->names[s->n++] = name;
}

static int names_has(NameSet* s, const char* name) {
    if (!s || !name) return 0;
    for (int i = 0; i < s->n; i++) {
        if (strcmp(s->names[i], name) == 0) return 1;
    }
    return 0;
}

static void collect_expr_uses(Expr* e, NameSet* out) {
    if (!e || !out) return;
    if (e->kind == E_IDENT && e->str) names_add(out, e->str);
    /* E_MEMBER `a.b`: `a` may itself be an ident use; member name is not a var. */
    collect_expr_uses(e->a, out);
    collect_expr_uses(e->b, out);
    collect_expr_uses(e->c, out);
    for (int i = 0; i < e->nitems; i++) collect_expr_uses(e->items[i], out);
    for (int i = 0; i < e->ncitems; i++)
        if (e->citems[i].e) collect_expr_uses(e->citems[i].e, out);
    for (int i = 0; i < e->nnfields; i++)
        if (e->nfields[i].e) collect_expr_uses(e->nfields[i].e, out);
    for (int i = 0; i < e->nmarms; i++) {
        /* match arm pattern may bind names; body uses count. Keep it simple:
           patterns that are plain idents are treated as bindings elsewhere. */
        collect_expr_uses(e->marms[i].pattern, out);
        collect_expr_uses(e->marms[i].body, out);
    }
}

static void collect_stmt_uses(Stmt* s, NameSet* out) {
    if (!s || !out) return;
    if (s->decl && s->decl->init) collect_expr_uses(s->decl->init, out);
    if (s->decl && s->decl->dim) collect_expr_uses(s->decl->dim, out);
    collect_expr_uses(s->e, out);
    collect_expr_uses(s->cond, out);
    collect_expr_uses(s->init_expr, out);
    collect_expr_uses(s->step, out);
    collect_expr_uses(s->iter, out);
    if (s->init_decl && s->init_decl->init) collect_expr_uses(s->init_decl->init, out);
    collect_stmt_uses(s->then, out);
    collect_stmt_uses(s->els, out);
    collect_stmt_uses(s->body, out);
    collect_stmt_uses(s->defer, out);
    for (int i = 0; i < s->nstmts; i++) collect_stmt_uses(s->stmts[i], out);
    for (int i = 0; i < s->narms; i++) {
        for (int j = 0; j < s->arms[i].nlabels; j++)
            collect_expr_uses(s->arms[i].labels[j], out);
        collect_stmt_uses(s->arms[i].body, out);
    }
    for (int i = 0; i < s->nmarms; i++) {
        collect_expr_uses(s->marms[i].pattern, out);
        collect_expr_uses(s->marms[i].body, out);
    }
}

/* ─── decl collection ──────────────────────────────────────────────── */

typedef struct {
    Decl** decls;
    int* is_param;
    int n;
    int cap;
} DeclSet;

static void decls_add(DeclSet* s, Decl* d, int is_param) {
    if (!s || !d || !d->name) return;
    if (s->n >= s->cap) {
        int ncap = s->cap ? s->cap * 2 : 32;
        Decl** nd = realloc(s->decls, (size_t)ncap * sizeof *nd);
        int* np = realloc(s->is_param, (size_t)ncap * sizeof *np);
        if (!nd || !np) exit(1);
        s->decls = nd;
        s->is_param = np;
        s->cap = ncap;
    }
    s->decls[s->n] = d;
    s->is_param[s->n] = is_param;
    s->n++;
}

static void collect_stmt_decls(Stmt* s, DeclSet* out) {
    if (!s || !out) return;
    if (s->decl) decls_add(out, s->decl, 0);
    if (s->init_decl) decls_add(out, s->init_decl, 0);
    collect_stmt_decls(s->then, out);
    collect_stmt_decls(s->els, out);
    collect_stmt_decls(s->body, out);
    collect_stmt_decls(s->defer, out);
    for (int i = 0; i < s->nstmts; i++) collect_stmt_decls(s->stmts[i], out);
    for (int i = 0; i < s->narms; i++) collect_stmt_decls(s->arms[i].body, out);
    /* S_MATCH arm bodies are expressions (MatchArm.body is Expr*), so they
       cannot declare locals — uses are already covered by collect_stmt_uses. */
}

/* ─── unreachable-code detection ───────────────────────────────────── */

static int stmt_always_exits(const Stmt* s) {
    if (!s) return 0;
    return s->kind == S_RETURN || s->kind == S_BREAK || s->kind == S_CONTINUE;
}

static void check_unreachable_block(Stmt* block, Sema* sema) {
    if (!block || block->kind != S_BLOCK) return;
    for (int i = 0; i < block->nstmts; i++) {
        Stmt* s = block->nstmts > 0 ? block->stmts[i] : NULL;
        if (!s) continue;
        if (i > 0 && stmt_always_exits(block->stmts[i - 1])) {
            if (s->kind != S_EMPTY) {
                char msg[256];
                snprintf(msg, sizeof msg, "unreachable code after '%s'",
                         block->stmts[i - 1]->kind == S_RETURN ? "return" :
                         block->stmts[i - 1]->kind == S_BREAK ? "break" : "continue");
                int ec = s->col + (s->len >= 1 ? s->len : 1);
                lint_emit(sema, s->line >= 1 ? s->line : 1,
                          s->col >= 1 ? s->col : 1, ec, 2, "W2002", msg);
                /* Only warn once per block to avoid noise. */
                return;
            }
        }
        /* Recurse into nested blocks. */
        check_unreachable_block(s->then, sema);
        check_unreachable_block(s->els, sema);
        check_unreachable_block(s->body, sema);
        check_unreachable_block(s->defer, sema);
        for (int k = 0; k < s->nstmts; k++)
            check_unreachable_block(s->stmts[k], sema);
        for (int k = 0; k < s->narms; k++)
            check_unreachable_block(s->arms[k].body, sema);
    }
}

/* ─── per-function driver ──────────────────────────────────────────── */

static void lint_fn(FnDef* fn, Sema* sema, int is_main) {
    if (!fn || !fn->body || !sema) return;

    NameSet uses = {0};
    collect_stmt_uses(fn->body, &uses);

    DeclSet decls = {0};
    collect_stmt_decls(fn->body, &decls);

    for (int i = 0; i < decls.n; i++) {
        Decl* d = decls.decls[i];
        if (!d->name || d->name[0] == '_') continue;
        if (!names_has(&uses, d->name)) {
            char msg[256];
            snprintf(msg, sizeof msg, "unused variable '%s'", d->name);
            int ec = d->col + (d->len >= 1 ? d->len : (int)strlen(d->name));
            lint_emit(sema, d->line >= 1 ? d->line : 1,
                      d->col >= 1 ? d->col : 1, ec, 2, "W2001", msg);
        }
    }

    /* Unused parameters: hint-level (not warning) to avoid hello-world noise.
       Skipped for `main` entirely. */
    if (!is_main) {
        for (int i = 0; i < fn->nparams; i++) {
            const char* pn = fn->params[i].name;
            if (!pn || pn[0] == '_' || pn[0] == '\0') continue;
            if (!names_has(&uses, pn)) {
                char msg[256];
                snprintf(msg, sizeof msg, "unused parameter '%s'", pn);
                int fl = fn->line >= 1 ? fn->line : 1;
                int fc = fn->col >= 1 ? fn->col : 1;
                lint_emit(sema, fl, fc, fc + 1, 4, "W2003", msg);
                break; /* one hint per function is enough */
            }
        }
    }

    check_unreachable_block(fn->body, sema);

    free(uses.names);
    free(decls.decls);
    free(decls.is_param);
}

void lint_run(Program* prog, int n_local_items, Sema* sema) {
    lint_run_with_src(prog, n_local_items, sema, NULL);
}

void lint_run_with_src(Program* prog, int n_local_items, Sema* sema,
                       const char* src) {
    if (!prog || !sema) return;
    g_lint_src = src;
    int limit = (n_local_items > 0 && n_local_items < prog->nitems)
                    ? n_local_items
                    : prog->nitems;
    for (int i = 0; i < limit; i++) {
        Item* it = prog->items[i];
        if (!it) continue;
        if (it->kind == TOP_FN && it->fn) {
            if (it->fn->mod_prefix) continue; /* comprised import */
            int is_main = it->fn->name && strcmp(it->fn->name, "main") == 0;
            lint_fn(it->fn, sema, is_main);
        } else if (it->kind == TOP_IMPL && it->im) {
            if (it->im->mod_prefix) continue;
            for (int m = 0; m < it->im->nmethods; m++) {
                if (it->im->methods[m] && !it->im->methods[m]->mod_prefix)
                    lint_fn(it->im->methods[m], sema, 0);
            }
        }
    }
    g_lint_src = NULL;
}
