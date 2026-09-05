#include "sema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <unistd.h>
#include <limits.h>
#endif

#include "diag.h"
#include "util.h"

/* ─── Commandlist (C API info from commandlist.json) ────── */

typedef struct {
    char name[128];
    char ret[128];
    char param_types[256];  /* \x1f-separated param types (e.g. "int\x1fconst char*") */
    int nparams;
    int is_variadic;        /* 1 if the last "param" was "..." (not counted in nparams) */
} ClFunc;

static ClFunc* cl_funcs = NULL;
static size_t cl_count = 0;
static size_t cl_cap = 0;
static int cl_loaded = 0;

static int file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

#ifdef __linux__
/* Directory holding the running rokade executable, resolved via /proc/self/exe.
   Returns 0 on success, -1 if it cannot be determined. Callers own `buf`. */
static int cl_exe_dir(char* buf, size_t cap) {
    ssize_t n = readlink("/proc/self/exe", buf, cap - 1);
    if (n <= 0) return -1;
    buf[n] = '\0';
    char* slash = strrchr(buf, '/');
    if (!slash) return -1;
    *slash = '\0';
    return 0;
}
#endif

static void cl_load(const char* basedir, const char* override) {
    if (cl_loaded) return;
    cl_loaded = 1;

    char path[8192];
    path[0] = '\0';

    /* 1) explicit override from config (or the caller) */
    if (override && override[0]) {
        snprintf(path, sizeof(path), "%s", override);
        if (!file_exists(path)) path[0] = '\0';
    }
    /* 2) ROKADE_DATA_DIR (installed/share or repo src/libc) */
    if (path[0] == '\0') {
        const char* datadir = getenv("ROKADE_DATA_DIR");
        if (datadir && datadir[0]) {
            snprintf(path, sizeof(path), "%s/commandlist.json", datadir);
            if (!file_exists(path)) path[0] = '\0';
        }
    }
    /* 3) relative to the source file's directory (project-local) */
    if (path[0] == '\0' && basedir && basedir[0]) {
        snprintf(path, sizeof(path), "%s/../commandlist.json", basedir);
        if (!file_exists(path)) {
            snprintf(path, sizeof(path), "%s/commandlist.json", basedir);
            if (!file_exists(path)) path[0] = '\0';
        }
    }
#ifdef __linux__
    /* 4) alongside the executable: build tree ("build/commandlist.json") or an
       install layout ("<exe>/../share/rokade/commandlist.json"). This makes a
       build-tree or installed rokade find its commandlist from any cwd. */
    if (path[0] == '\0') {
        char ed[4096];
        if (cl_exe_dir(ed, sizeof(ed)) == 0) {
            snprintf(path, sizeof(path), "%s/commandlist.json", ed);
            if (!file_exists(path)) {
                snprintf(path, sizeof(path), "%s/../share/rokade/commandlist.json", ed);
                if (!file_exists(path)) path[0] = '\0';
            }
        }
    }
#endif
    /* 5) cwd fallback */
    if (path[0] == '\0') {
        snprintf(path, sizeof(path), "commandlist.json");
    }
    if (!file_exists(path)) return;

    FILE* f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char* buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);

    /* Minimal scan: function objects have "name", "ret", "params" keys.
       A "name" value only counts as a function name if followed by "ret".
       "params" is an array of {"name":"...","type":"..."} objects. */
    char name_cand[128] = "";
    char last_key[16] = "";
    size_t i = 0;
    while (buf[i]) {
        if (buf[i] == '"') {
            size_t s = i + 1;
            while (buf[s] && buf[s] != '"') s++;
            size_t tl = s - (i + 1);
            size_t k = s + 1;
            while (buf[k] == ' ' || buf[k] == '\t' || buf[k] == '\n' || buf[k] == '\r') k++;
            if (buf[k] == ':') {
                if (tl == 4 && strncmp(buf + i + 1, "name", 4) == 0)
                    snprintf(last_key, sizeof(last_key), "name");
                else if (tl == 3 && strncmp(buf + i + 1, "ret", 3) == 0)
                    snprintf(last_key, sizeof(last_key), "ret");
                else if (tl == 6 && strncmp(buf + i + 1, "params", 6) == 0)
                    snprintf(last_key, sizeof(last_key), "params");
                else if (tl == 4 && strncmp(buf + i + 1, "type", 4) == 0)
                    snprintf(last_key, sizeof(last_key), "type");
                else
                    last_key[0] = '\0';
            } else {
                if (strcmp(last_key, "name") == 0) {
                    snprintf(name_cand, sizeof(name_cand), "%.*s", (int)(tl < 127 ? tl : 127), buf + i + 1);
                } else if (strcmp(last_key, "ret") == 0 && name_cand[0]) {
                    if (cl_count >= cl_cap) {
                        cl_cap = cl_cap ? cl_cap * 2 : 64;
                        cl_funcs = realloc(cl_funcs, cl_cap * sizeof(ClFunc));
                    }
                    snprintf(cl_funcs[cl_count].name, sizeof(cl_funcs[cl_count].name), "%s", name_cand);
                    snprintf(cl_funcs[cl_count].ret, sizeof(cl_funcs[cl_count].ret),
                             "%.*s", (int)(tl < 127 ? tl : 127), buf + i + 1);
                    cl_funcs[cl_count].param_types[0] = '\0';
                    cl_funcs[cl_count].nparams = 0;
                    cl_count++;
                } else if (strcmp(last_key, "type") == 0 && name_cand[0]) {
                    /* Add param type to the current (last) function */
                    if (cl_count > 0) {
                        int idx = (int)cl_count - 1;
                        /* A "..." param is a variadic marker, not a real arg:
                           flag the function and do not count it. */
                        if (tl == 3 && strncmp(buf + i + 1, "...", 3) == 0) {
                            cl_funcs[idx].is_variadic = 1;
                        } else {
                            if (cl_funcs[idx].nparams > 0)
                                strncat(cl_funcs[idx].param_types, "\x1f",
                                        sizeof(cl_funcs[idx].param_types) -
                                        strlen(cl_funcs[idx].param_types) - 1);
                            strncat(cl_funcs[idx].param_types, buf + i + 1,
                                    (int)(tl < 200 ? tl : 200));
                            cl_funcs[idx].nparams++;
                        }
                    }
                }
                last_key[0] = '\0';
            }
            i = s;
        }
        i++;
    }
    free(buf);
}

const char* sema_lookup_cfunc(const char* name) {
    for (size_t i = 0; i < cl_count; i++)
        if (strcmp(cl_funcs[i].name, name) == 0 && strcmp(cl_funcs[i].ret, "void") != 0)
            return cl_funcs[i].ret;
    return NULL;
}

const char* sema_lookup_cfunc_param(const char* name, int pidx) {
    for (size_t i = 0; i < cl_count; i++) {
        if (strcmp(cl_funcs[i].name, name) == 0 && pidx < cl_funcs[i].nparams) {
            /* Tokenize param_types to get the pidx-th param type */
            const char* p = cl_funcs[i].param_types;
            int pi = 0;
            while (*p && pi < pidx) {
                if (*p == '\x1f') { pi++; p++; }
                else p++;
            }
            /* Find end of token */
            const char* start = p;
            while (*p && *p != '\x1f') p++;
            static char param_type[256];
            snprintf(param_type, sizeof(param_type), "%.*s", (int)(p - start), start);
            return param_type;
        }
    }
    return NULL;
}

int sema_cfunc_nparams(const char* name) {
    for (size_t i = 0; i < cl_count; i++)
        if (strcmp(cl_funcs[i].name, name) == 0)
            return cl_funcs[i].nparams;
    return -1;
}

int sema_is_cfunc(const char* name) {
    for (size_t i = 0; i < cl_count; i++)
        if (strcmp(cl_funcs[i].name, name) == 0)
            return 1;
    return 0;
}

int sema_cfunc_is_variadic(const char* name) {
    for (size_t i = 0; i < cl_count; i++)
        if (strcmp(cl_funcs[i].name, name) == 0)
            return cl_funcs[i].is_variadic;
    return 0;
}

void sema_load_commandlist(const char* basedir, const char* override) {
    cl_load(basedir, override);
}

int sema_register_cfunc(const char* name, const char* ret, const char* param_types, int nparams, int is_variadic) {
    if (!name || !name[0]) return 0;
    for (size_t i = 0; i < cl_count; i++) {
        if (strcmp(cl_funcs[i].name, name) == 0) {
            if (ret && ret[0]) snprintf(cl_funcs[i].ret, sizeof(cl_funcs[i].ret), "%s", ret);
            if (param_types) snprintf(cl_funcs[i].param_types, sizeof(cl_funcs[i].param_types), "%s", param_types);
            cl_funcs[i].nparams = nparams;
            cl_funcs[i].is_variadic = is_variadic;
            return 1;
        }
    }
    if (cl_count >= cl_cap) {
        cl_cap = cl_cap ? cl_cap * 2 : 64;
        cl_funcs = realloc(cl_funcs, cl_cap * sizeof(ClFunc));
    }
    strncpy(cl_funcs[cl_count].name, name, 127);
    cl_funcs[cl_count].name[127] = '\0';
    snprintf(cl_funcs[cl_count].ret, sizeof(cl_funcs[cl_count].ret), "%s", ret ? ret : "void");
    if (param_types) {
        strncpy(cl_funcs[cl_count].param_types, param_types, sizeof(cl_funcs[cl_count].param_types) - 1);
        cl_funcs[cl_count].param_types[sizeof(cl_funcs[cl_count].param_types) - 1] = '\0';
    } else {
        cl_funcs[cl_count].param_types[0] = '\0';
    }
    cl_funcs[cl_count].nparams = nparams;
    cl_funcs[cl_count].is_variadic = is_variadic;
    cl_count++;
    return 1;
}

static Scope* scope_new(Scope* parent) {
    Scope* s = calloc(1, sizeof *s);
    if (!s) exit(1);
    s->parent = parent;
    return s;
}

static void scope_free(Scope* s) {
    if (!s) return;
    for (int i = 0; i < s->nsyms; i++) {
        free(s->syms[i]->name);
        free(s->syms[i]);
    }
    free(s->syms);
    scope_free(s->parent);
    free(s);
}

static Sym* scope_lookup(Scope* s, const char* name) {
    if (!s || !name) return NULL;
    for (int i = s->nsyms - 1; i >= 0; i--) {
        if (strcmp(s->syms[i]->name, name) == 0) return s->syms[i];
    }
    if (s->parent) return scope_lookup(s->parent, name);
    return NULL;
}

static void scope_add(Scope* s, Sym* sym) {
    if (s->nsyms == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->syms = realloc(s->syms, s->cap * sizeof *s->syms);
        if (!s->syms) exit(1);
    }
    s->syms[s->nsyms++] = sym;
}

Sema* sema_new(void) {
    Sema* s = calloc(1, sizeof *s);
    if (!s) exit(1);
    s->scope = scope_new(NULL);
    return s;
}

void sema_free(Sema* s) {
    if (!s) return;
    scope_free(s->scope);
    free(s->err);
    free(s);
}

void sema_set_source(Sema* s, const char* src, int len) {
    if (!s) return;
    s->src = src;
    s->srclen = len;
}

void sema_set_include_dirs(Sema* s, const char** dirs, size_t n_dirs) {
    if (!s) return;
    s->include_dirs = dirs;
    s->n_include_dirs = n_dirs;
}

static Sym* sym_new_fn(const char* name, FnDef* fn) {
    Sym* s = calloc(1, sizeof *s);
    if (!s) exit(1);
    s->name = strdup(name);
    s->kind = SYM_FN;
    s->fn = fn;
    s->ret_type = fn->ret;
    return s;
}

static Sym* sym_new_struct(const char* name, StructDef* st) {
    Sym* s = calloc(1, sizeof *s);
    if (!s) exit(1);
    s->name = strdup(name);
    s->kind = SYM_STRUCT;
    s->st = st;
    return s;
}

static Sym* sym_new_impl(const char* name, ImplDef* im) {
    Sym* s = calloc(1, sizeof *s);
    if (!s) exit(1);
    s->name = strdup(name);
    s->kind = SYM_IMPL;
    s->im = im;
    return s;
}

static Sym* sym_new_var(const char* name, Decl* decl) {
    Sym* s = calloc(1, sizeof *s);
    if (!s) exit(1);
    s->name = strdup(name);
    s->kind = SYM_VAR;
    s->decl = decl;
    return s;
}

static Sym* sym_new_type(const char* name, AstType* type) {
    Sym* s = calloc(1, sizeof *s);
    if (!s) exit(1);
    s->name = strdup(name);
    s->kind = SYM_TYPE;
    s->type = type;
    return s;
}

static Sym* sym_new_variant(const char* name, EnumDef* ed, int idx) {
    Sym* s = calloc(1, sizeof *s);
    if (!s) exit(1);
    s->name = strdup(name);
    s->kind = SYM_ENUMVARIANT;
    s->ed = ed;
    s->variant_idx = idx;
    return s;
}

AstType* sema_mk_type(const char* qual, const char* name, int ptrs) {
    AstType* t = calloc(1, sizeof *t);
    t->qual = qual ? strdup(qual) : strdup("");
    t->name = name ? strdup(name) : strdup("void");
    t->ptrs = ptrs;
    return t;
}

int sema_register_cstruct(Sema* s, const char* name, StructField* fields, int nfields) {
    if (!s || !s->scope || !name || !name[0]) return 0;
    Sym* existing = sema_lookup(s, name);
    if (existing && (existing->kind == SYM_STRUCT || existing->kind == SYM_IMPL)) {
        return 0;
    }
    StructDef* st = calloc(1, sizeof *st);
    st->name = strdup(name);
    st->fields = fields;
    st->nfields = nfields;
    st->is_object = 0;
    Sym* sym = sym_new_struct(name, st);
    scope_add(s->scope, sym);
    return 1;
}

int sema_register_ctypedef(Sema* s, const char* name, AstType* type) {
    if (!s || !s->scope || !name || !name[0] || !type) return 0;
    Sym* existing = sema_lookup(s, name);
    if (existing) return 0;
    Sym* sym = sym_new_type(name, type);
    scope_add(s->scope, sym);
    return 1;
}

int sema_register_cvar(Sema* s, const char* name, AstType* type) {
    if (!s || !s->scope || !name || !name[0] || !type) return 0;
    Sym* existing = sema_lookup(s, name);
    if (existing) return 0;
    Decl* d = calloc(1, sizeof *d);
    d->name = strdup(name);
    d->type = type;
    Sym* sym = sym_new_var(name, d);
    scope_add(s->scope, sym);
    return 1;
}

static void collect_program(Sema* sema, Program* prog) {
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        switch (it->kind) {
        case TOP_RAW:
        case TOP_MODULE:
        case TOP_IMPORT:
            break;
        case TOP_FN: {
            Sym* sym = sym_new_fn(it->fn->name, it->fn);
            scope_add(sema->scope, sym);
            break;
        }
        case TOP_STRUCT: {
            Sym* sym = sym_new_struct(it->st->name, it->st);
            scope_add(sema->scope, sym);
            break;
        }
        case TOP_IMPL: {
            Sym* sym = sym_new_impl(it->im->target->name, it->im);
            scope_add(sema->scope, sym);
            break;
        }
        case TOP_ENUM: {
            Sym* sym = sym_new_type(it->ed->name, NULL);
            sym->kind = SYM_ENUM;
            sym->ed = it->ed;
            scope_add(sema->scope, sym);
            for (int i = 0; i < it->ed->nvariants; i++) {
                Sym* v = sym_new_variant(it->ed->variants[i].name, it->ed, i);
                scope_add(sema->scope, v);
            }
            break;
        }
        }
    }
}

int sema_collect(Sema* sema, Program* prog) {
    sema->prog = prog;
    collect_program(sema, prog);
    return sema->err ? 1 : 0;
}

Sym* sema_lookup(Sema* sema, const char* name) {
    return scope_lookup(sema->scope, name);
}

/* Return the owning enum's name for a unit-enum variant (first match), or NULL
   if `name` is not a registered enum variant. Used by the C backend to emit
   `Enum_Variant` constants. */
const char* sema_lookup_variant(Sema* sema, const char* name) {
    if (!sema || !sema->prog || !name) return NULL;
    for (int i = 0; i < sema->prog->nitems; i++) {
        Item* it = sema->prog->items[i];
        if (it->kind != TOP_ENUM) continue;
        for (int j = 0; j < it->ed->nvariants; j++) {
            if (strcmp(it->ed->variants[j].name, name) == 0)
                return it->ed->name;
        }
    }
    return NULL;
}

/* Look up a struct definition by name (walks the symbol table). */
StructDef* sema_lookup_struct(Sema* sema, const char* name) {
    if (!sema || !name) return NULL;
    Sym* sym = scope_lookup(sema->scope, name);
    if (sym && sym->kind == SYM_STRUCT) return sym->st;
    if (sym && sym->kind == SYM_IMPL) {
        /* impl syms store the target struct; find the struct def */
        for (int i = 0; i < sema->prog->nitems; i++) {
            Item* it = sema->prog->items[i];
            if (it->kind == TOP_STRUCT && strcmp(it->st->name, name) == 0) return it->st;
        }
    }
    return NULL;
}

/* Look up an enum definition by name (walks symbol table, falls back to items). */
EnumDef* sema_lookup_enum(Sema* sema, const char* name) {
    if (!sema || !name) return NULL;
    Sym* sym = scope_lookup(sema->scope, name);
    if (sym && sym->kind == SYM_ENUM && sym->ed) return sym->ed;
    if (sema->prog) {
        for (int i = 0; i < sema->prog->nitems; i++) {
            Item* it = sema->prog->items[i];
            if (it->kind == TOP_ENUM && it->ed && strcmp(it->ed->name, name) == 0)
                return it->ed;
        }
    }
    return NULL;
}

/* Find the impl for `struct_name` that defines `method`, walking the
   inheritance chain. Returns the ImplDef (or NULL) and sets *owner to the
   struct whose impl owns the method (malloc'd; caller frees). */
static ImplDef* find_method_impl(Sema* s, const char* struct_name, const char* method, char** owner) {
    const char* cur = struct_name;
    while (cur && s->prog) {
        for (int i = 0; i < s->prog->nitems; i++) {
            Item* it = s->prog->items[i];
            if (it->kind != TOP_IMPL) continue;
            if (strcmp(it->im->target->name, cur) != 0) continue;
            for (int j = 0; j < it->im->nmethods; j++) {
                if (strcmp(it->im->methods[j]->name, method) == 0) {
                    if (owner) {
                        *owner = malloc(strlen(cur) + 1);
                        if (!*owner) exit(1);
                        strcpy(*owner, cur);
                    }
                    return it->im;
                }
            }
        }
        Sym* sym = sema_lookup(s, cur);
        StructDef* st = (sym && (sym->kind == SYM_STRUCT || sym->kind == SYM_IMPL))
                            ? sema_lookup_struct(s, cur)
                            : NULL;
        if (!st || !st->parent) break;
        cur = st->parent;
    }
    return NULL;
}

Sym* sema_lookup_method(Sema* sema, const char* type_name, const char* method_name) {
    char* owner = NULL;
    ImplDef* im = find_method_impl(sema, type_name, method_name, &owner);
    free(owner);
    if (!im) return NULL;
    for (int i = 0; i < im->nmethods; i++)
        if (strcmp(im->methods[i]->name, method_name) == 0) {
            Sym* sym = calloc(1, sizeof *sym);
            if (!sym) exit(1);
            sym->name = strdup(method_name);
            sym->kind = SYM_FN;
            sym->fn = im->methods[i];
            sym->ret_type = im->methods[i]->ret;
            return sym;
        }
    return NULL;
}

static void append_type_qual(SB* sb, AstType* t) {
    if (t->qual) sb_append(sb, t->qual);
}

static void append_type_name(SB* sb, AstType* t) {
    sb_append(sb, t->name);
}

const char* sema_type_cname(Sema* sema, AstType* t) {
    if (!t) return NULL;
    (void)sema;
    SB sb;
    sb_init(&sb);
    append_type_qual(&sb, t);
    append_type_name(&sb, t);
    for (int i = 0; i < t->ptrs; i++) sb_append(&sb, "*");
    return sb_strdup(&sb);
}

const char* sema_mangle_method(Sema* sema, const char* type_name, const char* method_name) {
    (void)sema;
    SB sb;
    sb_init(&sb);
    sb_append(&sb, type_name);
    sb_append(&sb, "_");
    sb_append(&sb, method_name);
    return sb_strdup(&sb);
}

/* ═══════════════════════════════════════════════════════════════════════
   Type checker
   ═══════════════════════════════════════════════════════════════════════ */

typedef struct Checker {
    Sema* s;
    Scope* scope;             /* top-level symbol scope */
    Scope* locals;            /* current block scope */
    const char* self_type;    /* enclosing struct name (methods) */
    AstType* self_t;          /* self type (possibly generic instance) */
    FnDef* cur_fn;            /* for `?` / return checks */
    int defer_depth;          /* >0 while inside a defer body */
    int loop_depth;
    int switch_depth;         /* >0 while inside a switch/match body */
    int in_question;          /* nested `?` evaluation */
    char* err;                /* malloc'd diagnostic buffer */
    int is_err;
    int check_ret;            /* function is checked (has a body) */
} Checker;

static char* ck_type_str(AstType* t);
static AstType* ck_resolve_type(Checker* ck, Expr* e);
static AstType* ck_clone_type(AstType* src);
static void ck_expr(Checker* ck, Expr* x);
static void ck_stmt(Checker* ck, Stmt* s);
static void ck_decl(Checker* ck, Decl* d);

static void ck_err_at(Checker* ck, int offset, int width, const char* msg) {
    if (ck->is_err) return;
    ck->is_err = 1;
    if (!ck->s || !ck->s->src) return;
    char* buf = malloc(2048);
    if (!buf) exit(1);
    diag_render(ck->s->src, offset, width, "error", msg, buf, 2048);
    ck->err = buf;
}

static void ck_err_expr(Checker* ck, Expr* x, const char* msg) {
    ck_err_at(ck, x ? x->start : 0, x && x->len >= 1 ? x->len : 1, msg);
}

/* ── local scope helpers ──────────────────────────────────────────── */

static void ck_push_scope(Checker* ck) {
    ck->locals = scope_new(ck->locals);
}

static void ck_pop_scope(Checker* ck) {
    Scope* top = ck->locals;
    if (!top) return;
    ck->locals = top->parent;
    /* free this scope's symbols (but keep the parent chain intact) */
    for (int i = 0; i < top->nsyms; i++) {
        free(top->syms[i]->name);
        free(top->syms[i]);
    }
    free(top->syms);
    free(top);
}

static void ck_add_local(Checker* ck, const char* name, AstType* type, Decl* decl) {
    if (!ck->locals) ck->locals = scope_new(NULL);
    Sym* sym = calloc(1, sizeof *sym);
    if (!sym) exit(1);
    sym->name = strdup(name);
    sym->kind = SYM_VAR;
    sym->type = type;
    sym->decl = decl;
    scope_add(ck->locals, sym);
}

static Sym* ck_lookup_local(Checker* ck, const char* name) {
    return scope_lookup(ck->locals, name);
}

/* For a sum-variant pattern like `Circle { r: x }` or `Rect { w: w, h: h }`,
   register the bound field names as locals (typed from the variant's payload
   struct fields) so the arm body can type-check against them. Unit-variant,
   wildcard, and integer/literal patterns bind nothing. */
static void ck_bind_match_pattern(Checker* ck, Expr* p, AstType* scrut) {
    (void)scrut;
    if (!p) return;
    if (p->kind == E_IDENT) return;                 /* wildcard "_" or unit variant */
    const char* vname = NULL;
    EnumDef* ed = NULL;
    EnumVariant* v = NULL;
    if (p->kind == E_NAMED_INIT && p->type && p->type->name) {  /* Circle { r: x } */
        vname = p->type->name;
        Sym* vsym = sema_lookup(ck->s, vname);
        if (!vsym || vsym->kind != SYM_ENUMVARIANT || !vsym->ed) return;
        ed = vsym->ed;
        int vi = vsym->variant_idx;
        if (vi < 0 || vi >= ed->nvariants) return;
        v = &ed->variants[vi];
        for (int i = 0; i < p->nnfields && i < v->nfields; i++) {
            NamedInitField* sf = &p->nfields[i];
            if (!sf->e || sf->e->kind != E_IDENT || strcmp(sf->e->str, "_") == 0) continue;
            AstType* field_ty = NULL;
            for (int m = 0; m < v->nfields; m++) {
                if (strcmp(v->fields[m].name, sf->name) == 0) {
                    field_ty = v->fields[m].type;
                    break;
                }
            }
            if (!field_ty) continue;
            ck_add_local(ck, sf->e->str, ck_clone_type(field_ty), NULL);
        }
    } else if (p->kind == E_CALL && p->a && p->a->kind == E_IDENT
               && sema_lookup_variant(ck->s, p->a->str)) {
        /* A positional variant pattern `Circle(x)` is not allowed;
           patterns carry named fields: `Circle { r: x }` or `Circle { r }`. */
        ck_err_expr(ck, p,
            "variant pattern uses named fields: write `Circle { r: x }` (or `Circle { r }`)");
        return;
    }
}

/* ── builtin / macro knowledge ─────────────────────────────────────── */

static const char* const BUILTIN_NAMES[] = {
    "int", "float", "double", "char", "long", "short", "void",
    "size_t", "ssize_t", "bool", "auto", "const", "unsigned", "signed",
    "true", "false", "NULL", "self",
    "FILE", "FILE*", "va_list", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int8_t", "int16_t", "int32_t", "int64_t", "uintptr_t", "intptr_t",
    "stdin", "stdout", "stderr",
    "printf", "fprintf", "sprintf", "snprintf", "scanf", "sscanf",
    "puts", "putchar", "getchar", "strlen", "strcmp", "strcpy", "strcat",
    "strdup", "strchr", "strrchr", "strstr", "strcspn", "strtok", "memcpy", "memset", "malloc", "calloc", "realloc", "free",
    "abs", "labs", "rand", "srand", "exit", "atoi", "atol", "atof",
    "pow", "sqrt", "sin", "cos", "tan", "fopen", "fclose", "fread",
    "fwrite", "fgets", "fgetc", "fputc", "getline",
    NULL
};

static int is_builtin_name(const char* name) {
    if (!name) return 0;
    for (int i = 0; BUILTIN_NAMES[i]; i++)
        if (strcmp(BUILTIN_NAMES[i], name) == 0) return 1;
    return 0;
}

/* Common C type-words / typedefs that may appear in raw-C decls. */
static const char* const C_TYPE_WORDS[] = {
    "int", "char", "float", "double", "long", "short", "void", "size_t",
    "ssize_t", "FILE", "va_list", "uint8_t", "uint16_t", "uint32_t",
    "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t", "uintptr_t",
    "intptr_t", "CPoint", "bool",
    NULL
};

static int is_c_type_word(const char* name) {
    if (!name) return 0;
    for (int i = 0; C_TYPE_WORDS[i]; i++)
        if (strcmp(C_TYPE_WORDS[i], name) == 0) return 1;
    return 0;
}

/* ── type helpers ──────────────────────────────────────────────────── */

static AstType* ck_mk_type(const char* name, int ptrs) {
    AstType* t = ast_type_new();
    const char* p = name ? strchr(name, '*') : NULL;
    if (p) {
        size_t nlen = (size_t)(p - name);
        char base[256];
        if (nlen >= sizeof(base)) nlen = sizeof(base) - 1;
        memcpy(base, name, nlen);
        base[nlen] = '\0';
        while (nlen > 0 && base[nlen - 1] == ' ') base[--nlen] = '\0';
        t->name = strdup(base);
        int extra = 0;
        for (const char* q = p; *q; q++) { if (*q == '*') extra++; }
        t->ptrs = ptrs + extra;
    } else {
        t->name = name ? strdup(name) : NULL;
        t->ptrs = ptrs;
    }
    return t;
}

static AstType* ck_clone_type(AstType* src) {
    if (!src) return NULL;
    AstType* r = ast_type_new();
    r->name = src->name ? strdup(src->name) : NULL;
    r->qual = src->qual ? strdup(src->qual) : NULL;
    r->ptrs = src->ptrs;
    return r;
}

static int ck_type_is_numeric(const char* name) {
    if (!name) return 0;
    return strcmp(name, "int") == 0 || strcmp(name, "float") == 0 ||
           strcmp(name, "double") == 0 || strcmp(name, "char") == 0 ||
           strcmp(name, "long") == 0 || strcmp(name, "short") == 0 ||
           strcmp(name, "size_t") == 0 || strcmp(name, "ssize_t") == 0 ||
           strcmp(name, "bool") == 0 || strcmp(name, "unsigned") == 0 ||
           strcmp(name, "signed") == 0 || strcmp(name, "uint8_t") == 0 ||
           strcmp(name, "uint16_t") == 0 || strcmp(name, "uint32_t") == 0 ||
           strcmp(name, "uint64_t") == 0 || strcmp(name, "int8_t") == 0 ||
           strcmp(name, "int16_t") == 0 || strcmp(name, "int32_t") == 0 ||
           strcmp(name, "int64_t") == 0 || strcmp(name, "uintptr_t") == 0 ||
           strcmp(name, "intptr_t") == 0;
}

/* Equivalence: pointer/value distinction, const-tolerant. */
static int ck_type_eq(AstType* a, AstType* b) {
    if (!a || !b) return 0;
    if (a->ptrs != b->ptrs) return 0;
    if (strcmp(a->name, b->name) != 0) return 0;
    return 1;
}

static int ck_types_compatible(AstType* want, AstType* got) {
    if (!want || !got) return 0;
    if (ck_type_is_numeric(want->name) && ck_type_is_numeric(got->name) &&
        want->ptrs == 0 && got->ptrs == 0) return 1;
    if (ck_type_eq(want, got)) return 1;
    /* void* is compatible with any pointer type (standard C: NULL and malloc) */
    if (want->ptrs > 0 && got->ptrs > 0) {
        if (strcmp(want->name, "void") == 0 || strcmp(got->name, "void") == 0) return 1;
    }
    return 0;
}

static char* ck_type_str(AstType* t) {
    if (!t) return strdup("?");
    SB sb;
    sb_init(&sb);
    if (t->qual) sb_append(&sb, t->qual);
    sb_append(&sb, t->name);
    for (int i = 0; i < t->ptrs; i++) sb_append(&sb, "*");
    return sb_strdup(&sb);
}

/* ── raw-C knowledge (typedefs / C globals / C fns) ────────────────── */

typedef struct RawName {
    char* name;
    struct RawName* next;
} RawName;

static RawName* raw_names = NULL;

static void raw_add(const char* name) {
    if (!name || !*name) return;
    RawName* r = malloc(sizeof *r);
    if (!r) exit(1);
    r->name = strdup(name);
    r->next = raw_names;
    raw_names = r;
}

static int raw_has(const char* name) {
    if (!name) return 0;
    for (RawName* r = raw_names; r; r = r->next)
        if (strcmp(r->name, name) == 0) return 1;
    return 0;
}

static void raw_names_free(void) {
    RawName* r = raw_names;
    while (r) {
        RawName* n = r->next;
        free(r->name);
        free(r);
        r = n;
    }
    raw_names = NULL;
}

/* ── "did you mean?" suggestions ─────────────────────────────────────── */

/* Bounded Levenshtein edit distance. */
static int lev_dist(const char* a, const char* b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la == 0) return lb;
    if (lb == 0) return la;
    int prev[256], cur[256];
    for (int j = 0; j <= lb && j < 256; j++) prev[j] = j;
    for (int i = 1; i <= la; i++) {
        cur[0] = i;
        char ca = a[i - 1];
        for (int j = 1; j <= lb && j < 256; j++) {
            int cost = (ca == b[j - 1]) ? 0 : 1;
            int v = prev[j] + 1;
            int t = cur[j - 1] + 1; if (t < v) v = t;
            int s = prev[j - 1] + cost; if (s < v) v = s;
            cur[j] = v;
        }
        for (int j = 0; j <= lb && j < 256; j++) prev[j] = cur[j];
    }
    return prev[lb];
}

/* Closest candidate within a reasonable distance, or NULL. */
static const char* lev_nearest(const char* target, const char** cands, int n) {
    if (!target) return NULL;
    int best_d = 1 << 20, best = -1;
    int maxd = (int)strlen(target) / 3 + 1;
    if (maxd < 1) maxd = 1;
    for (int i = 0; i < n; i++) {
        if (!cands[i] || strcmp(cands[i], target) == 0) continue;
        int d = lev_dist(target, cands[i]);
        if (d <= maxd && d < best_d) { best_d = d; best = i; }
    }
    return best >= 0 ? cands[best] : NULL;
}

/* Returns a malloc'd "<base> (did you mean '<cand>'?)" or a strdup of base. */
static char* with_suggestion(const char* base, const char* cand) {
    if (!cand) return strdup(base);
    size_t need = strlen(base) + strlen(cand) + 32;
    char* out = malloc(need);
    if (!out) exit(1);
    snprintf(out, need, "%s (did you mean '%s'?)", base, cand);
    return out;
}

/* Collect all raw-known names (C fns/globals + user fns) into a NULL-terminated
   array (malloc'd). */
static const char** collect_raw_names(int* out_n) {
    int n = 0;
    for (RawName* r = raw_names; r; r = r->next) n++;
    const char** arr = malloc((size_t)(n + 1) * sizeof(char*));
    if (!arr) exit(1);
    int i = 0;
    for (RawName* r = raw_names; r; r = r->next) arr[i++] = r->name;
    arr[i] = NULL;
    *out_n = n;
    return arr;
}

/* Collect visible local-scope symbol names (walking the scope chain). */
static const char** collect_scope_names(Scope* top, int* out_n) {
    int n = 0;
    for (Scope* s = top; s; s = s->parent) n += s->nsyms;
    const char** arr = malloc((size_t)(n + 1) * sizeof(char*));
    if (!arr) exit(1);
    int i = 0;
    for (Scope* s = top; s; s = s->parent)
        for (int k = 0; k < s->nsyms; k++) arr[i++] = s->syms[k]->name;
    arr[i] = NULL;
    *out_n = n;
    return arr;
}

/* Collect known type names (program struct names + builtin scalar types). */
static const char** collect_type_names(Program* prog, int* out_n) {
    static const char* builtins[] = {
        "int", "float", "double", "char", "bool",
        "void", NULL
    };
    int nb = 0; while (builtins[nb]) nb++;
    int ns = 0;
    if (prog) for (int i = 0; i < prog->nitems; i++)
        if (prog->items[i]->kind == TOP_STRUCT) ns++;
    const char** arr = malloc((size_t)(nb + ns + 1) * sizeof(char*));
    if (!arr) exit(1);
    int i = 0;
    if (prog) for (int j = 0; j < prog->nitems; j++)
        if (prog->items[j]->kind == TOP_STRUCT)
            arr[i++] = prog->items[j]->st->name;
    for (int j = 0; j < nb; j++) arr[i++] = builtins[j];
    arr[i] = NULL;
    *out_n = i;
    return arr;
}


static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static void scan_raw_region(Checker* ck, const char* raw, int len);

static void scan_c_header_file(Checker* ck, const char* header_name) {
    if (!header_name || !*header_name) return;

    static char scanned[256][256];
    static size_t n_scanned = 0;
    for (size_t i = 0; i < n_scanned; i++) {
        if (strcmp(scanned[i], header_name) == 0) return;
    }
    if (n_scanned < 256) {
        snprintf(scanned[n_scanned++], 256, "%s", header_name);
    }

    const char* search_dirs[] = {
        "/usr/include",
        "/usr/local/include",
        "/usr/include/x86_64-linux-gnu",
        "/usr/include/aarch64-linux-gnu",
        "src",
        "include",
        "."
    };
    char full_path[4096];
    int found = 0;
    for (size_t d = 0; d < sizeof(search_dirs)/sizeof(search_dirs[0]); d++) {
        snprintf(full_path, sizeof full_path, "%s/%s", search_dirs[d], header_name);
        if (access(full_path, R_OK) == 0) {
            found = 1;
            break;
        }
    }
    if (!found) return;

    FILE* f = fopen(full_path, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz > 0 && sz < 2 * 1024 * 1024) { /* Up to 2MB header */
        char* content = malloc((size_t)sz + 1);
        if (content) {
            size_t rd = fread(content, 1, (size_t)sz, f);
            content[rd] = '\0';
            scan_raw_region(ck, content, (int)rd);
            free(content);
        }
    }
    fclose(f);
}

/* Scan a TOP_RAW slice for typedef aliases, C function names, #define macro
   names, and top-level C global variable names, so the checker does not flag
   them. Conservative: every bare identifier that is not a C type-word and not
   a keyword is treated as a "raw-known" name. */
static void scan_raw_region(Checker* ck, const char* raw, int len) {
    if (!raw || len <= 0) return;
    char* buf = malloc(len + 1);
    if (!buf) exit(1);
    memcpy(buf, raw, len);
    buf[len] = '\0';

    /* Discover any included C headers and scan their symbols */
    const char* inc = strstr(buf, "#include");
    while (inc) {
        const char* q1 = strchr(inc, '<');
        const char* q2 = strchr(inc, '"');
        char end_char = 0;
        const char* hstart = NULL;
        if (q1 && (!q2 || q1 < q2)) {
            hstart = q1 + 1;
            end_char = '>';
        } else if (q2) {
            hstart = q2 + 1;
            end_char = '"';
        }
        if (hstart) {
            const char* hend = strchr(hstart, end_char);
            if (hend && (hend - hstart < 256)) {
                char hname[256];
                size_t hlen = (size_t)(hend - hstart);
                memcpy(hname, hstart, hlen);
                hname[hlen] = '\0';
                /* Only scan C headers (.h or without extension), not .rook */
                if (hlen < 5 || strcmp(hname + hlen - 5, ".rook") != 0) {
                    scan_c_header_file(ck, hname);
                }
            }
        }
        inc = strstr(inc + 8, "#include");
    }

    /* Blank out comments, string/char literals, and preprocessor directives
       so words inside them are not mistakenly collected as identifiers. */
    char* cur = buf;
    while (*cur) {
        if (cur[0] == '/' && cur[1] == '/') {
            while (*cur && *cur != '\n') { *cur = ' '; cur++; }
        } else if (cur[0] == '/' && cur[1] == '*') {
            *cur = ' '; cur++;
            if (*cur) { *cur = ' '; cur++; }
            while (*cur && !(cur[0] == '*' && cur[1] == '/')) {
                *cur = ' ';
                cur++;
            }
            if (*cur) { *cur = ' '; cur++; }
            if (*cur) { *cur = ' '; cur++; }
        } else if (*cur == '"') {
            *cur = ' '; cur++;
            while (*cur && *cur != '"') {
                if (*cur == '\\' && *(cur + 1)) { *cur = ' '; cur++; }
                *cur = ' '; cur++;
            }
            if (*cur == '"') { *cur = ' '; cur++; }
        } else if (*cur == '\'') {
            *cur = ' '; cur++;
            while (*cur && *cur != '\'') {
                if (*cur == '\\' && *(cur + 1)) { *cur = ' '; cur++; }
                *cur = ' '; cur++;
            }
            if (*cur == '\'') { *cur = ' '; cur++; }
        } else if (*cur == '#') {
            const char* line = cur;
            cur++;
            while (*cur == ' ' || *cur == '\t') cur++;
            if (strncmp(cur, "define", 6) == 0 && (*(cur + 6) == ' ' || *(cur + 6) == '\t')) {
                cur += 6;
                while (*cur == ' ' || *cur == '\t') cur++;
                const char* id_start = cur;
                while (is_ident_char(*cur)) cur++;
                int id_len = (int)(cur - id_start);
                if (id_len > 0) {
                    char* w = malloc(id_len + 1);
                    if (w) {
                        memcpy(w, id_start, id_len);
                        w[id_len] = '\0';
                        raw_add(w);
                    }
                }
            }
            while (*cur && *cur != '\n') {
                if (*cur == '\\' && *(cur + 1) == '\n') cur += 2;
                else cur++;
            }
            for (char* k = (char*)line; k < cur; k++) *k = ' ';
        } else {
            cur++;
        }
    }

    const char* p = buf;
    while (*p) {
        while (*p && !is_ident_char(*p)) p++;
        if (!*p) break;
        const char* start = p;
        while (is_ident_char(*p)) p++;
        int n = (int)(p - start);
        if (n > 0) {
            char* w = malloc(n + 1);
            if (!w) exit(1);
            memcpy(w, start, n);
            w[n] = '\0';
            if (!is_c_type_word(w) &&
                strcmp(w, "struct") != 0 && strcmp(w, "union") != 0 &&
                strcmp(w, "enum") != 0 && strcmp(w, "typedef") != 0 &&
                strcmp(w, "include") != 0 && strcmp(w, "define") != 0 &&
                strcmp(w, "ifndef") != 0 && strcmp(w, "ifdef") != 0 &&
                strcmp(w, "endif") != 0 && strcmp(w, "return") != 0 &&
                strcmp(w, "sizeof") != 0 && strcmp(w, "static") != 0 &&
                strcmp(w, "extern") != 0 && strcmp(w, "const") != 0 &&
                strcmp(w, "void") != 0 && strcmp(w, "int") != 0 &&
                strcmp(w, "char") != 0 && strcmp(w, "float") != 0 &&
                strcmp(w, "double") != 0 && strcmp(w, "long") != 0 &&
                strcmp(w, "short") != 0 && strcmp(w, "unsigned") != 0 &&
                strcmp(w, "signed") != 0 && strcmp(w, "size_t") != 0 &&
                strcmp(w, "ssize_t") != 0 && strcmp(w, "FILE") != 0 &&
                strcmp(w, "bool") != 0) {
                raw_add(w);
            } else {
                free(w);
            }
        }
    }
    free(buf);
}

/* ── expression type inference (best effort) ───────────────────────── */

static int ck_is_self(Checker* ck, Expr* e) {
    (void)ck;
    return e && e->kind == E_IDENT && strcmp(e->str, "self") == 0;
}

static AstType* ck_resolve_type(Checker* ck, Expr* e) {
    if (!e) return NULL;
    switch (e->kind) {
    case E_IDENT: {
        if (ck_is_self(ck, e)) return ck->self_t ? ck_clone_type(ck->self_t) : NULL;
        if (strcmp(e->str, "true") == 0 || strcmp(e->str, "false") == 0) return ck_mk_type("bool", 0);
        if (strcmp(e->str, "NULL") == 0 || strcmp(e->str, "null") == 0) return ck_mk_type("void", 1);
        Sym* sym = ck_lookup_local(ck, e->str);
        if (sym) {
            AstType* r = sym->type ? ck_clone_type(sym->type) : NULL;
            if (r && sym->decl && sym->decl->dim) r->ptrs++;
            return r;
        }
        Sym* g = sema_lookup(ck->s, e->str);
        if (g && g->kind == SYM_ENUMVARIANT && g->ed) {
            return ck_mk_type(g->ed->name, 0);   /* the owning enum type */
        }
        if (g && g->kind == SYM_VAR) {
            AstType* r = g->type ? ck_clone_type(g->type) : NULL;
            if (r && g->decl && g->decl->dim) r->ptrs++;
            return r;
        }
        return NULL;
    }
    case E_LITERAL:
        if (e->str[0] == '"') return ck_mk_type("char", 1);
        if (e->str[0] == '\'') return ck_mk_type("char", 0);
        return ck_mk_type("int", 0);   /* numeric literal defaults to int */
    case E_CALL:
        if (e->a && e->a->kind == E_IDENT) {
            const char* fn = e->a->str;
            const char* en = sema_lookup_variant(ck->s, fn);
            if (en) {
                ck_err_expr(ck, e,
                    "use named-field construction 'X { field: value }' instead of "
                    "positional 'X(...)'");
                return ck_mk_type(en, 0);
            }
            Sym* sym = sema_lookup(ck->s, fn);
            if (sym && sym->kind == SYM_FN && sym->fn && sym->fn->ret) {
                return ck_clone_type(sym->fn->ret);
            }
            const char* c_ret = sema_lookup_cfunc(fn);
            if (c_ret) return ck_mk_type(c_ret, 0);
        }
        if (e->a && e->a->kind == E_MEMBER) {
            Expr* m = e->a;
            AstType* base = ck_resolve_type(ck, m->a);
            if (base) {
                /* struct field access */
                Sym* sym = sema_lookup(ck->s, base->name);
                if (sym && (sym->kind == SYM_STRUCT || sym->kind == SYM_IMPL)) {
                    StructDef* st = sema_lookup_struct(ck->s, base->name);
                    while (st) {
                        for (int i = 0; i < st->nfields; i++) {
                            if (strcmp(st->fields[i].name, m->str) == 0) {
                                AstType* r = ck_clone_type(st->fields[i].type);
                                free(base);
                                return r;
                            }
                        }
                        st = st->parent ? sema_lookup_struct(ck->s, st->parent) : NULL;
                    }
                }
            }
            free(base);
        }
        return NULL;
    case E_INDEX: {
        AstType* base = ck_resolve_type(ck, e->a);
        if (!base) return NULL;
        if (base->ptrs > 0) {
            AstType* r = ck_clone_type(base);
            r->ptrs--;
            free(base);
            return r;
        }
        free(base);
        return NULL;
    }
    case E_PAREN:
        return ck_resolve_type(ck, e->a);
    case E_CAST:
        return e->type ? ck_clone_type(e->type) : NULL;
    case E_NAMED_INIT:
    case E_COMPOUND: {
        if (!e->type || !e->type->name) return NULL;
        const char* owning_enum = sema_lookup_variant(ck->s, e->type->name);
        if (owning_enum) {
            return ck_mk_type(owning_enum, e->type->ptrs);
        }
        return ck_clone_type(e->type);
    }
    case E_BINARY: {
        AstType* lt = ck_resolve_type(ck, e->a);
        AstType* rt = ck_resolve_type(ck, e->b);
        if (!lt || !rt) { free(lt); free(rt); return NULL; }
        const char* op = e->str;
        if (op && (strcmp(op, "+") == 0 || strcmp(op, "-") == 0)) {
            if (lt->ptrs > 0 && rt->ptrs == 0 && ck_type_is_numeric(rt->name)) {
                AstType* r = ck_clone_type(lt);
                free(lt); free(rt);
                return r;
            }
            if (strcmp(op, "+") == 0 && lt->ptrs == 0 && rt->ptrs > 0 && ck_type_is_numeric(lt->name)) {
                AstType* r = ck_clone_type(rt);
                free(lt); free(rt);
                return r;
            }
            if (strcmp(op, "-") == 0 && lt->ptrs > 0 && rt->ptrs > 0) {
                AstType* r = ck_mk_type("int", 0);
                free(lt); free(rt);
                return r;
            }
        }
        if (lt->ptrs == 0 && rt->ptrs == 0) {
            int lf = (lt->name && (strcmp(lt->name,"float")==0 || strcmp(lt->name,"double")==0));
            int rf = (rt->name && (strcmp(rt->name,"float")==0 || strcmp(rt->name,"double")==0));
            if (lf || rf) { AstType* r = ck_mk_type("float", 0); free(lt); free(rt); return r; }
            if (ck_type_is_numeric(lt->name) && ck_type_is_numeric(rt->name)) {
                AstType* r = ck_mk_type("int", 0); free(lt); free(rt); return r;
            }
        }
        free(lt); free(rt);
        return NULL;
    }
    case E_UNARY: {
        AstType* inner = ck_resolve_type(ck, e->a);
        if (!inner) return NULL;
        if (e->str && strcmp(e->str, "*") == 0 && inner->ptrs > 0) {
            AstType* r = ck_clone_type(inner);
            r->ptrs--;
            free(inner);
            return r;
        }
        if (e->str && strcmp(e->str, "&") == 0) {
            inner->ptrs++;
            return inner;
        }
        return inner;
    }
    case E_POST:
        return ck_resolve_type(ck, e->a);
    case E_TERNARY:
        return ck_resolve_type(ck, e->b);
    case E_MATCH:
        return e->type ? ck_clone_type(e->type) : NULL;
    case E_QUESTION: {
        /* Without built-in Result/Option there is no type-level `?`; the
           inner expression's type is preserved. */
        return ck_resolve_type(ck, e->a);
    }
    default:
        return NULL;
    }
}

static AstType* ck_fn_ret_instantiated(Checker* ck, FnDef* f) {
    (void)ck;
    if (!f || !f->ret) return NULL;
    return ck_clone_type(f->ret);
}

static int ck_is_result_type(AstType* t) {
    (void)t;
    return 0;
}

/* ── expression checking ───────────────────────────────────────────── */

static void ck_check_call(Checker* ck, Expr* x) {
    Expr* callee = x->a;
    if (!callee) return;

    if (callee->kind == E_IDENT) {
        const char* fn = callee->str;
        const char* en = sema_lookup_variant(ck->s, fn);
        if (en) {
            ck_err_expr(ck, x,
                "use named-field construction 'X { field: value }' instead of "
                "positional 'X(...)'");
            return;
        }
    }

    /* Variant constructor / `Some`/`None`/`Ok`/`Err` constructors are checked above. */
    if (callee->kind == E_IDENT) {
        const char* fn = callee->str;
        Sym* sym = sema_lookup(ck->s, fn);
        if (sym && sym->kind == SYM_FN && sym->fn) {
            FnDef* f = sym->fn;
            int nparams = f->nparams;
            int skip = (nparams > 0 && strcmp(f->params[0].name, "self") == 0) ? 1 : 0;
            if (x->nitems != nparams - skip) {
                char msg[256];
                snprintf(msg, sizeof msg, "function '%s' expects %d argument%s, got %d",
                         fn, nparams - skip, (nparams - skip) == 1 ? "" : "s", x->nitems);
                ck_err_expr(ck, x, msg);
                return;
            }
            for (int i = 0; i < x->nitems; i++) {
                int pi = i + skip;
                AstType* want = ck_clone_type(f->params[pi].type);
                AstType* got = ck_resolve_type(ck, x->items[i]);
                if (!want || !got) { free(want); free(got); continue; }
                int ok = ck_types_compatible(want, got);
                if (!ok) {
                    char* ws = ck_type_str(want);
                    char* gs = ck_type_str(got);
                    char msg[256];
                    snprintf(msg, sizeof msg, "argument %d of '%s': expected %s, got %s",
                             i + 1, fn, ws, gs);
                    ck_err_expr(ck, x->items[i], msg);
                    free(ws); free(gs);
                }
                free(want); free(got);
            }
            return;
        }
        if (sema_is_cfunc(fn)) {
            int np = sema_cfunc_nparams(fn);
            if (!sema_cfunc_is_variadic(fn) && np >= 0 && x->nitems > np) {
                char msg[256];
                snprintf(msg, sizeof msg, "function '%s' expects at most %d argument%s, got %d",
                         fn, np, np == 1 ? "" : "s", x->nitems);
                ck_err_expr(ck, x, msg);
            }
            return;
        }
        if (!is_builtin_name(fn) && !raw_has(fn) && !sema_is_cfunc(fn)) {
            char msg[256];
            snprintf(msg, sizeof msg, "call to unknown function '%s'", fn);
            int rn = 0;
            const char** cands = collect_raw_names(&rn);
            const char* near = lev_nearest(fn, cands, rn);
            char* full = with_suggestion(msg, near);
            free(cands);
            ck_err_expr(ck, callee, full);
            free(full);
            return;
        }
        return;
    }

    if (callee->kind == E_MEMBER) {
        Expr* m = callee;
        AstType* base = ck_resolve_type(ck, m->a);
        if (!base) return;
        const char* method = m->str;

        /* user-defined impl methods */
        Sym* base_sym = sema_lookup(ck->s, base->name);
        char* owner = NULL;
        ImplDef* im = (base_sym && (base_sym->kind == SYM_STRUCT || base_sym->kind == SYM_IMPL))
                          ? find_method_impl(ck->s, base->name, method, &owner)
                          : NULL;
        if (im) {
            free(owner);
            FnDef* mdef = NULL;
            for (int i = 0; i < im->nmethods; i++)
                if (strcmp(im->methods[i]->name, method) == 0) { mdef = im->methods[i]; break; }
            if (mdef) {
                int nparams = mdef->nparams;
                int skip = (nparams > 0 && strcmp(mdef->params[0].name, "self") == 0) ? 1 : 0;
                if (x->nitems != nparams - skip) {
                    char msg[256];
                    snprintf(msg, sizeof msg, "method '%s' expects %d argument%s, got %d",
                             method, nparams - skip, (nparams - skip) == 1 ? "" : "s", x->nitems);
                    ck_err_expr(ck, x, msg);
                    free(base);
                    return;
                }
                for (int j = 0; j < x->nitems; j++) {
                    int pi = j + skip;
                    AstType* want = ck_clone_type(mdef->params[pi].type);
                    AstType* got = ck_resolve_type(ck, x->items[j]);
                    if (!want || !got) { free(want); free(got); continue; }
                    int ok = ck_types_compatible(want, got);
                    if (!ok) {
                        char* ws = ck_type_str(want);
                        char* gs = ck_type_str(got);
                        char msg[256];
                        snprintf(msg, sizeof msg, "argument %d of '%s': expected %s, got %s",
                                 j + 1, method, ws, gs);
                        ck_err_expr(ck, x->items[j], msg);
                        free(ws); free(gs);
                    }
                    free(want); free(got);
                }
            }
            free(base);
            return;
        }

        /* unknown method on a known type */
        if (!raw_has(method) && is_builtin_name(base->name)) {
            char msg[256];
            snprintf(msg, sizeof msg, "unknown method '%s' for type '%s'", method, base->name);
            ck_err_expr(ck, callee, msg);
        }
        free(base);
        return;
    }

    if (callee->kind == E_ARROW) {
        /* C-style direct function-pointer / arrow call; treat leniently. */
        return;
    }
}

static void ck_member_field(Checker* ck, Expr* x) {
    Expr* obj = x->a;
    AstType* t = ck_resolve_type(ck, obj);
    if (!t) return;
    const char* field = x->str;

    Sym* sym = sema_lookup(ck->s, t->name);
    if (sym && (sym->kind == SYM_STRUCT || sym->kind == SYM_IMPL)) {
        StructDef* st = sema_lookup_struct(ck->s, t->name);
        int found = 0;
        while (st) {
            for (int i = 0; i < st->nfields; i++) {
                if (strcmp(st->fields[i].name, field) == 0) { found = 1; break; }
            }
            if (found) break;
            st = st->parent ? sema_lookup_struct(ck->s, st->parent) : NULL;
        }
        /* a method on the type (or an inherited one) is not a field — allowed */
        if (!found) {
            char* owner = NULL;
            if (find_method_impl(ck->s, t->name, field, &owner)) {
                found = 1;
            }
            free(owner);
        }
        if (!found) {
            char msg[256];
            snprintf(msg, sizeof msg, "type '%s' has no field '%s'", t->name, field);
            ck_err_expr(ck, x, msg);
        }
    }
    free(t);
}

static Expr* strip_parens_and_casts(Expr* e) {
    while (e) {
        if (e->kind == E_PAREN) e = e->a;
        else if (e->kind == E_CAST) e = e->a;
        else break;
    }
    return e;
}

/* Evaluate simple constant expressions at compile-time (constants, casts, parens, unary +/-, binary + - * /) */
static int sema_eval_const(Expr* e, double* out_val) {
    if (!e) return 0;
    while (e && (e->kind == E_PAREN || e->kind == E_CAST)) e = e->a;
    if (!e) return 0;

    if (e->kind == E_LITERAL && e->str) {
        const char* s = e->str;
        while (*s == ' ' || *s == '\t') s++;
        char* endp = NULL;
        double d = strtod(s, &endp);
        if (endp && endp != s) {
            while (*endp == 'u' || *endp == 'U' || *endp == 'l' || *endp == 'L' || *endp == 'f' || *endp == 'F') endp++;
            while (*endp == ' ' || *endp == '\t') endp++;
            if (*endp == '\0') {
                *out_val = d;
                return 1;
            }
        }
        return 0;
    }
    if (e->kind == E_UNARY && e->str) {
        double v;
        if (!sema_eval_const(e->a, &v)) return 0;
        if (strcmp(e->str, "-") == 0) { *out_val = -v; return 1; }
        if (strcmp(e->str, "+") == 0) { *out_val = v; return 1; }
        return 0;
    }
    if (e->kind == E_BINARY && e->str) {
        double a, b;
        if (!sema_eval_const(e->a, &a) || !sema_eval_const(e->b, &b)) return 0;
        if (strcmp(e->str, "+") == 0) { *out_val = a + b; return 1; }
        if (strcmp(e->str, "-") == 0) { *out_val = a - b; return 1; }
        if (strcmp(e->str, "*") == 0) { *out_val = a * b; return 1; }
        if (strcmp(e->str, "/") == 0) {
            if (b == 0.0) return 0;
            *out_val = a / b;
            return 1;
        }
        return 0;
    }
    return 0;
}

static int is_literal_zero(Expr* e) {
    double v = 0.0;
    if (sema_eval_const(e, &v)) {
        return (v == 0.0);
    }
    return 0;
}

static void ck_expr(Checker* ck, Expr* x) {
    if (!x || ck->is_err) return;
    switch (x->kind) {
    case E_LITERAL:
        break;
    case E_IDENT: {
        if (ck_is_self(ck, x)) break;
        if (strcmp(x->str, "_") == 0) break;
        Sym* local = ck_lookup_local(ck, x->str);
        if (local) {
            if (local->decl) { x->def_kind = DEF_VAR; x->def = local->decl; }
            if (local->type) x->type = ck_clone_type(local->type);
            break;
        }
        Sym* g = sema_lookup(ck->s, x->str);
        if (g) {
            if (g->kind == SYM_ENUMVARIANT && g->ed)
                x->type = ck_mk_type(g->ed->name, 0);
            if (g->fn)        { x->def_kind = DEF_FN;     x->def = g->fn; }
            else if (g->st)   { x->def_kind = DEF_STRUCT; x->def = g->st; }
            else if (g->ed) {
                if (g->kind == SYM_ENUMVARIANT && g->variant_idx >= 0
                    && g->variant_idx < g->ed->nvariants) {
                    x->def_kind = DEF_VARIANT;
                    x->def = &g->ed->variants[g->variant_idx];
                } else {
                    x->def_kind = DEF_ENUM;
                    x->def = g->ed;
                }
            }
            break;
        }
        if (is_builtin_name(x->str)) break;
        if (raw_has(x->str)) break;
        if (sema_is_cfunc(x->str)) break;
        if (x->str[0] >= '0' && x->str[0] <= '9') break;
        char msg[256];
        snprintf(msg, sizeof msg, "use of undeclared identifier '%s'", x->str);
        int sn = 0;
        const char** sc = collect_scope_names(ck->locals, &sn);
        const char* near = lev_nearest(x->str, sc, sn);
        char* full = with_suggestion(msg, near);
        free(sc);
        ck_err_expr(ck, x, full);
        free(full);
        break;
    }
    case E_CALL:
        ck_expr(ck, x->a);
        for (int i = 0; i < x->nitems; i++) ck_expr(ck, x->items[i]);
        ck_check_call(ck, x);
        if (!x->type) x->type = ck_resolve_type(ck, x);
        break;
    case E_MEMBER:
        ck_expr(ck, x->a);
        ck_member_field(ck, x);
        break;
    case E_ARROW:
        ck_expr(ck, x->a);
        break;
    case E_INDEX:
        ck_expr(ck, x->a);
        ck_expr(ck, x->b);
        if (!x->type) x->type = ck_resolve_type(ck, x);
        break;
    case E_UNARY:
        ck_expr(ck, x->a);
        if (x->str && (strcmp(x->str, "++") == 0 || strcmp(x->str, "--") == 0)) {
            AstType* t = ck_resolve_type(ck, x->a);
            if (t) {
                if (t->ptrs > 0 && t->name && strcmp(t->name, "void") == 0) {
                    ck_err_expr(ck, x, "pointer arithmetic on 'void*' is invalid; cast to 'char*' or 'uint8_t*'");
                }
                free(t);
            }
        }
        if (!x->type) x->type = ck_resolve_type(ck, x);
        break;
    case E_POST:
        ck_expr(ck, x->a);
        if (x->str && (strcmp(x->str, "++") == 0 || strcmp(x->str, "--") == 0)) {
            AstType* t = ck_resolve_type(ck, x->a);
            if (t) {
                if (t->ptrs > 0 && t->name && strcmp(t->name, "void") == 0) {
                    ck_err_expr(ck, x, "pointer arithmetic on 'void*' is invalid; cast to 'char*' or 'uint8_t*'");
                }
                free(t);
            }
        }
        break;
    case E_BINARY: {
        ck_expr(ck, x->a);
        ck_expr(ck, x->b);

        /* Ban division or modulo by literal zero */
        if (x->str && (strcmp(x->str, "/") == 0 || strcmp(x->str, "%") == 0)) {
            if (is_literal_zero(x->b)) {
                ck_err_expr(ck, x, "division or modulo by zero");
            }
        }

        /* Pointer arithmetic safety checks */
        AstType* lt = ck_resolve_type(ck, x->a);
        AstType* rt = ck_resolve_type(ck, x->b);
        if (lt && rt && x->str) {
            int lp = lt->ptrs > 0;
            int rp = rt->ptrs > 0;
            int lvoid = lp && lt->name && strcmp(lt->name, "void") == 0;
            int rvoid = rp && rt->name && strcmp(rt->name, "void") == 0;
            if (strcmp(x->str, "+") == 0) {
                if (lp && rp) {
                    ck_err_expr(ck, x, "cannot add two pointers");
                } else if (lvoid || rvoid) {
                    ck_err_expr(ck, x, "pointer arithmetic on 'void*' is invalid; cast to 'char*' or 'uint8_t*'");
                }
            } else if (strcmp(x->str, "-") == 0) {
                if (!lp && rp) {
                    ck_err_expr(ck, x, "cannot subtract pointer from integer");
                } else if (lvoid || rvoid) {
                    ck_err_expr(ck, x, "pointer arithmetic on 'void*' is invalid; cast to 'char*' or 'uint8_t*'");
                }
            } else if (strcmp(x->str, "*") == 0 || strcmp(x->str, "/") == 0 || strcmp(x->str, "%") == 0 ||
                       strcmp(x->str, "&") == 0 || strcmp(x->str, "|") == 0 || strcmp(x->str, "^") == 0 ||
                       strcmp(x->str, "<<") == 0 || strcmp(x->str, ">>") == 0) {
                if (lp || rp) {
                    ck_err_expr(ck, x, "invalid operand of pointer type for binary operator");
                }
            }
        }
        free(lt);
        free(rt);
        break;
    }
    case E_TERNARY: {
        Expr* c = strip_parens_and_casts(x->a);
        if (c && c->kind == E_ASSIGN) {
            ck_err_expr(ck, c, "assignment used as condition; did you mean '=='?");
        }
        ck_expr(ck, x->a);
        ck_expr(ck, x->b);
        ck_expr(ck, x->c);
        break;
    }
    case E_ASSIGN: {
        ck_expr(ck, x->a);
        ck_expr(ck, x->b);
        const char* op = x->str ? x->str : "=";
        if (strcmp(op, "/=") == 0 || strcmp(op, "%=") == 0) {
            if (is_literal_zero(x->b)) {
                ck_err_expr(ck, x, "division or modulo by zero");
            }
        }
        AstType* lt = ck_resolve_type(ck, x->a);
        AstType* rt = ck_resolve_type(ck, x->b);
        if (lt && rt) {
            int lp = lt->ptrs > 0;
            int rp = rt->ptrs > 0;
            int lvoid = lp && lt->name && strcmp(lt->name, "void") == 0;
            int rvoid = rp && rt->name && strcmp(rt->name, "void") == 0;
            if (strcmp(op, "+=") == 0) {
                if (lp && rp) {
                    ck_err_expr(ck, x, "cannot add two pointers");
                } else if (lvoid || rvoid) {
                    ck_err_expr(ck, x, "pointer arithmetic on 'void*' is invalid; cast to 'char*' or 'uint8_t*'");
                }
            } else if (strcmp(op, "-=") == 0) {
                if (!lp && rp) {
                    ck_err_expr(ck, x, "cannot subtract pointer from integer");
                } else if (lvoid || rvoid) {
                    ck_err_expr(ck, x, "pointer arithmetic on 'void*' is invalid; cast to 'char*' or 'uint8_t*'");
                }
            } else if (strcmp(op, "*=") == 0 || strcmp(op, "/=") == 0 || strcmp(op, "%=") == 0 ||
                       strcmp(op, "&=") == 0 || strcmp(op, "|=") == 0 || strcmp(op, "^=") == 0 ||
                       strcmp(op, "<<=") == 0 || strcmp(op, ">>=") == 0) {
                if (lp || rp) {
                    ck_err_expr(ck, x, "invalid operand of pointer type for binary operator");
                }
            }
            int ok = ck_types_compatible(lt, rt);
            if (!ok) {
                char* ls = ck_type_str(lt);
                char* rs = ck_type_str(rt);
                char msg[256];
                snprintf(msg, sizeof msg, "cannot assign %s to %s", rs, ls);
                ck_err_expr(ck, x, msg);
                free(ls); free(rs);
            }
        }
        free(lt); free(rt);
        break;
    }
    case E_CAST:
        ck_expr(ck, x->a);
        break;
    case E_COMPOUND:
        for (int i = 0; i < x->ncitems; i++) ck_expr(ck, x->citems[i].e);
        break;
    case E_NAMED_INIT:
        for (int i = 0; i < x->nnfields; i++) ck_expr(ck, x->nfields[i].e);
        break;
    case E_BRACE_INIT:
        for (int i = 0; i < x->nitems; i++) ck_expr(ck, x->items[i]);
        break;
    case E_PAREN:
        ck_expr(ck, x->a);
        break;
    case E_SIZEOF_T:
        break;
    case E_SIZEOF_E:
        ck_expr(ck, x->a);
        break;
    case E_ARR_LIT:
        for (int i = 0; i < x->nitems; i++) ck_expr(ck, x->items[i]);
        break;
    case E_RANGE:
        ck_expr(ck, x->a);
        ck_expr(ck, x->b);
        break;
    case E_QUESTION: {
        ck_expr(ck, x->a);
        if (ck->defer_depth > 0) {
            ck_err_expr(ck, x, "'?' is not allowed inside a defer body");
        } else {
            AstType* t = ck_resolve_type(ck, x->a);
            if (!t) break;
            if (!ck_is_result_type(t)) {
                char* ts = ck_type_str(t);
                char msg[256];
                snprintf(msg, sizeof msg, "'?' requires the expression to be a Result/Option, got %s", ts);
                ck_err_expr(ck, x, msg);
                free(ts);
                free(t);
                break;
            }
            if (!ck->cur_fn || !ck->cur_fn->ret) {
                ck_err_expr(ck, x, "'?' requires the enclosing function to return a Result");
                free(t);
                break;
            }
            AstType* fret = ck_fn_ret_instantiated(ck, ck->cur_fn);
            if (fret && !ck_is_result_type(fret)) {
                char* fs = ck_type_str(fret);
                char msg[256];
                snprintf(msg, sizeof msg, "'?' requires the enclosing function to return a Result, not %s", fs);
                ck_err_expr(ck, x, msg);
                free(fs);
            }
            free(fret);
            free(t);
        }
        break;
    }
    case E_MATCH: {
        ck_expr(ck, x->a);
        AstType* result = NULL;
        for (int i = 0; i < x->nmarms; i++) {
            ck_push_scope(ck);
            ck_bind_match_pattern(ck, x->marms[i].pattern, x->a->type);
            ck_expr(ck, x->marms[i].pattern);
            ck_expr(ck, x->marms[i].body);
            AstType* bt = ck_resolve_type(ck, x->marms[i].body);
            if (bt && bt->name && (strcmp(bt->name, "void") != 0 || bt->ptrs > 0)) {
                if (!result) result = bt;
                else free(bt);
            } else free(bt);
            ck_pop_scope(ck);
        }
        x->type = result ? result : ck_mk_type("void", 0);
        break;
    }
    }
}

/* ── statement checking ────────────────────────────────────────────── */

static int is_enum_type(Sema* s, const char* name) {
    if (!s || !s->prog || !name) return 0;
    for (int i = 0; i < s->prog->nitems; i++) {
        Item* it = s->prog->items[i];
        if (it->kind == TOP_ENUM && strcmp(it->ed->name, name) == 0) return 1;
    }
    return 0;
}

static int ck_decl_type_valid(Checker* ck, AstType* t) {
    if (!t) return 1;
    if (is_c_type_word(t->name)) return 1;
    if (raw_has(t->name)) return 1;
    Sym* sym = sema_lookup(ck->s, t->name);
    if (sym && (sym->kind == SYM_STRUCT || sym->kind == SYM_IMPL)) {
        StructDef* st = sema_lookup_struct(ck->s, t->name);
        if (st) return 1;
    }
    /* A `sum` (enum) is a valid type. `scope_lookup` may return a shadowing
       `impl` symbol registered under the same name, so verify the enum exists
       directly against the program items. */
    if (sym && sym->kind == SYM_ENUM) return 1;
    if (sym && sym->kind == SYM_IMPL && is_enum_type(ck->s, t->name)) return 1;
    if (ck->cur_fn && ck->cur_fn->nparams > 0) {
        for (int i = 0; i < ck->cur_fn->nparams; i++) {
            AstType* pt = ck->cur_fn->params[i].type;
            if (pt && pt->name && strcmp(pt->name, t->name) == 0)
                return 1;
        }
    }
    return 0;
}

static void ck_decl(Checker* ck, Decl* d) {
    if (!d || !d->name) return;
    if (d->init) ck_expr(ck, d->init);

    /* validate declared type */
    if (d->type && d->type->name && strcmp(d->type->name, "void") == 0 && d->type->ptrs == 0) {
        char msg[256];
        snprintf(msg, sizeof msg, "variable '%s' cannot have 'void' type", d->name);
        ck_err_at(ck, d->start, d->len >= 1 ? d->len : 1, msg);
    } else if (d->type && !ck_decl_type_valid(ck, d->type)) {
        char* ts = ck_type_str(d->type);
        char msg[256];
        snprintf(msg, sizeof msg, "unknown type '%s'", ts);
        int tn = 0;
        const char** tc = collect_type_names(ck->s->prog, &tn);
        const char* near = lev_nearest(ts, tc, tn);
        char* full = with_suggestion(msg, near);
        free(tc);
        ck_err_at(ck, d->start, d->len >= 1 ? d->len : 1, full);
        free(full);
        free(ts);
    }

    if (!d->type && !d->init) {
        char msg[256];
        snprintf(msg, sizeof msg, "cannot infer type for variable '%s' without initializer", d->name);
        ck_err_at(ck, d->start, d->len >= 1 ? d->len : 1, msg);
    }

    /* infer and register the local */
    AstType* inferred = NULL;
    if (d->type) inferred = ck_clone_type(d->type);
    else if (d->init) inferred = ck_resolve_type(ck, d->init);
    ck_add_local(ck, d->name, inferred, d);

    /* initializer / declared-type mismatch */
    if (d->type && d->init) {
        AstType* got = ck_resolve_type(ck, d->init);
        if (got) {
            int ok = ck_types_compatible(d->type, got);
            if (!ok && raw_has(d->type->name)) ok = 1;
            if (!ok) {
                char* ws = ck_type_str(d->type);
                char* gs = ck_type_str(got);
                char msg[256];
                snprintf(msg, sizeof msg, "cannot initialize %s with %s", ws, gs);
                ck_err_at(ck, d->start, d->len >= 1 ? d->len : 1, msg);
                free(ws); free(gs);
            }
            free(got);
        }
    }

    if (d->dim) ck_expr(ck, d->dim);
}

/* Returns 1 if `s` (a block) ends in a `return <expr>` (or expression-bodied
   tail). Used for the "all paths return" check. */
static int ck_block_returns(Checker* ck, Stmt* s) {
    if (!s || s->kind != S_BLOCK) return 0;
    for (int i = s->nstmts - 1; i >= 0; i--) {
        Stmt* st = s->stmts[i];
        if (st->kind == S_RETURN) return st->e != NULL;
        if (st->kind == S_EXPR && st->e && st->e->kind == E_CALL &&
            st->e->a && st->e->a->kind == E_IDENT &&
            (strcmp(st->e->a->str, "exit") == 0 || strcmp(st->e->a->str, "abort") == 0))
            return 1;
        if (st->kind == S_IF && st->then && st->els &&
            ck_block_returns(ck, st->then) && ck_block_returns(ck, st->els))
            return 1;
        if (st->kind == S_BLOCK && ck_block_returns(ck, st)) return 1;
        if (st->kind == S_SWITCH && st->narms > 0) {
            int all = 1;
            for (int j = 0; j < st->narms; j++) {
                if (!ck_block_returns(ck, st->arms[j].body)) { all = 0; break; }
            }
            if (all) return 1;
        }
        if (st->kind == S_DECL || st->kind == S_EXPR || st->kind == S_EMPTY ||
            st->kind == S_DEFER) {
            continue;
        }
        break;  /* while / for / match / forin: conservatively "not sure" */
    }
    return 0;
}

static void ck_check_returns(Checker* ck, FnDef* f) {
    if (!f || !f->body || !f->ret) return;
    if (strcmp(f->ret->name, "void") == 0) return;
    if (f->nparams > 0 && strcmp(f->params[0].name, "self") == 0 && !f->body) return;
    if (ck_block_returns(ck, f->body)) return;
    char msg[256];
    snprintf(msg, sizeof msg, "function '%s' does not return a value on all paths", f->name);
    ck_err_at(ck, f->start ? f->start : (f->body ? f->body->start : 0), f->len ? f->len : 1, msg);
}

static void ck_scan_defer_body(Checker* ck, Stmt* s) {
    if (!s || ck->is_err) return;
    switch (s->kind) {
    case S_RETURN:
        ck_err_at(ck, s->start, s->len >= 1 ? s->len : 1, "'return' is not allowed inside a defer body");
        return;
    case S_BREAK:
        ck_err_at(ck, s->start, s->len >= 1 ? s->len : 1, "'break' is not allowed inside a defer body");
        return;
    case S_CONTINUE:
        ck_err_at(ck, s->start, s->len >= 1 ? s->len : 1, "'continue' is not allowed inside a defer body");
        return;
    case S_BLOCK:
        for (int i = 0; i < s->nstmts; i++) ck_scan_defer_body(ck, s->stmts[i]);
        return;
    case S_IF:
        ck_scan_defer_body(ck, s->then);
        ck_scan_defer_body(ck, s->els);
        return;
    case S_WHILE:
    case S_FOR:
    case S_FORIN:
        ck_scan_defer_body(ck, s->body);
        return;
    case S_SWITCH:
        for (int i = 0; i < s->narms; i++) ck_scan_defer_body(ck, s->arms[i].body);
        return;
    default:
        return;
    }
}

static void ck_stmt(Checker* ck, Stmt* s) {
    if (!s || ck->is_err) return;
    switch (s->kind) {
    case S_BLOCK:
        ck_push_scope(ck);
        for (int i = 0; i < s->nstmts; i++) ck_stmt(ck, s->stmts[i]);
        ck_pop_scope(ck);
        break;
    case S_EXPR:
        ck_expr(ck, s->e);
        break;
    case S_DECL:
        ck_decl(ck, s->decl);
        break;
    case S_IF: {
        Expr* c = strip_parens_and_casts(s->cond);
        if (c && c->kind == E_ASSIGN) {
            ck_err_expr(ck, c, "assignment used as condition; did you mean '=='?");
        }
        ck_expr(ck, s->cond);
        ck_stmt(ck, s->then);
        ck_stmt(ck, s->els);
        break;
    }
    case S_WHILE: {
        Expr* c = strip_parens_and_casts(s->cond);
        if (c && c->kind == E_ASSIGN) {
            ck_err_expr(ck, c, "assignment used as condition; did you mean '=='?");
        }
        ck_expr(ck, s->cond);
        ck->loop_depth++;
        ck_stmt(ck, s->body);
        ck->loop_depth--;
        break;
    }
    case S_FOR: {
        if (s->init_decl) ck_decl(ck, s->init_decl);
        if (s->init_expr) ck_expr(ck, s->init_expr);
        Expr* c = strip_parens_and_casts(s->cond);
        if (c && c->kind == E_ASSIGN) {
            ck_err_expr(ck, c, "assignment used as condition; did you mean '=='?");
        }
        ck_expr(ck, s->cond);
        ck_expr(ck, s->step);
        ck->loop_depth++;
        ck_stmt(ck, s->body);
        ck->loop_depth--;
        break;
    }
    case S_FORIN:
        if (!s->iter || s->iter->kind != E_ARR_LIT) {
            ck_err_expr(ck, s->iter, "for-in loop currently only supports array literal collections (e.g. 'for x in [1, 2, 3]')");
        }
        ck_expr(ck, s->iter);
        /* the loop variable is `auto` from the array literal element */
        ck_add_local(ck, s->var, NULL, NULL);
        ck->loop_depth++;
        ck_stmt(ck, s->body);
        ck->loop_depth--;
        break;
    case S_SWITCH: {
        Expr* c = strip_parens_and_casts(s->e);
        if (c && c->kind == E_ASSIGN) {
            ck_err_expr(ck, c, "assignment used as switch condition; did you mean '=='?");
        }
        ck_expr(ck, s->e);
        ck->switch_depth++;
        for (int i = 0; i < s->narms; i++) {
            for (int j = 0; j < s->arms[i].nlabels; j++) ck_expr(ck, s->arms[i].labels[j]);
            ck_stmt(ck, s->arms[i].body);
        }
        ck->switch_depth--;
        break;
    }
    case S_MATCH: {
        ck_expr(ck, s->e);
        ck->switch_depth++;
        for (int i = 0; i < s->nmarms; i++) {
            ck_push_scope(ck);
            ck_bind_match_pattern(ck, s->marms[i].pattern, s->e->type);
            ck_expr(ck, s->marms[i].pattern);
            ck_expr(ck, s->marms[i].body);
            ck_pop_scope(ck);
        }
        ck->switch_depth--;
        break;
    }
    case S_RETURN: {
        if (ck->defer_depth > 0) {
            ck_err_at(ck, s->start, s->len >= 1 ? s->len : 1, "'return' is not allowed inside a defer body");
            break;
        }
        if (s->e) ck_expr(ck, s->e);
        if (ck->cur_fn) {
            AstType* want = ck_fn_ret_instantiated(ck, ck->cur_fn);
            if (want && strcmp(want->name, "void") != 0) {
                if (!s->e) {
                    char msg[256];
                    snprintf(msg, sizeof msg, "function '%s' returns %s but 'return' has no value",
                             ck->cur_fn->name, want->name);
                    ck_err_at(ck, s->start, s->len >= 1 ? s->len : 1, msg);
                }
            } else if (want && s->e) {
                /* returning a value from a void function */
                AstType* got = ck_resolve_type(ck, s->e);
                if (got && strcmp(got->name, "void") != 0) {
                    char msg[256];
                    snprintf(msg, sizeof msg, "function '%s' returns void but 'return' has a value",
                             ck->cur_fn->name);
                    ck_err_at(ck, s->start, s->len >= 1 ? s->len : 1, msg);
                }
                free(got);
            }
            if (want && s->e) {
                AstType* got = ck_resolve_type(ck, s->e);
                if (want && got && strcmp(want->name, "void") != 0) {
                    int ok = ck_types_compatible(want, got);
                    if (!ok) {
                        char* ws = ck_type_str(want);
                        char* gs = ck_type_str(got);
                        char msg[256];
                        snprintf(msg, sizeof msg, "function '%s' returns %s but expression has type %s",
                                 ck->cur_fn->name, ws, gs);
                        ck_err_expr(ck, s->e, msg);
                        free(ws); free(gs);
                    }
                }
                free(got);
            }
            free(want);
        }
        break;
    }
    case S_BREAK:
        if (ck->defer_depth > 0) {
            ck_err_at(ck, s->start, s->len >= 1 ? s->len : 1, "'break' is not allowed inside a defer body");
        } else if (ck->loop_depth == 0 && ck->switch_depth == 0) {
            ck_err_at(ck, s->start, s->len >= 1 ? s->len : 1, "'break' used outside of a loop");
        }
        break;
    case S_CONTINUE:
        if (ck->defer_depth > 0) {
            ck_err_at(ck, s->start, s->len >= 1 ? s->len : 1, "'continue' is not allowed inside a defer body");
        } else if (ck->loop_depth == 0) {
            ck_err_at(ck, s->start, s->len >= 1 ? s->len : 1, "'continue' used outside of a loop");
        }
        break;
    case S_EMPTY:
        break;
    case S_DEFER: {
        ck->defer_depth++;
        ck_scan_defer_body(ck, s->defer);
        ck_stmt(ck, s->defer);
        ck->defer_depth--;
        break;
    }
    }
}

/* ── top-level checking ────────────────────────────────────────────── */

static void ck_check_impl(Checker* ck, ImplDef* im) {
    /* receiver type: target name */
    AstType* recv = im->target;
    /* `impl` is allowed on `object` and on `sum` (tagged unions); plain C
       `struct` cannot carry methods (use `object` instead). */
    if (recv && recv->name) {
        StructDef* st = sema_lookup_struct(ck->s, recv->name);
        if (st && !st->is_object) {
            ck_err_at(ck, im->start, 4,
                "plain C 'struct' cannot have methods; use 'object' (or 'sum') for impl");
        }
    }

    for (int i = 0; i < im->nmethods; i++) {
        FnDef* m = im->methods[i];
        int has_self = m->nparams > 0 && strcmp(m->params[0].name, "self") == 0;

        AstType* self_t = NULL;
        if (has_self) {
            self_t = ck_clone_type(recv);
            self_t->ptrs = 1;
        }

        ck->self_type = recv->name;
        ck->self_t = self_t;
        ck->cur_fn = m;
        ck->loop_depth = 0;

        /* method params */
        for (int j = has_self ? 1 : 0; j < m->nparams; j++)
            ck_add_local(ck, m->params[j].name, ck_clone_type(m->params[j].type), NULL);

        if (m->body) {
            ck_stmt(ck, m->body);
            ck_check_returns(ck, m);
        }

        /* clean up locals */
        while (ck->locals && ck->locals->parent) ck_pop_scope(ck);
        while (ck->locals) ck_pop_scope(ck);
        ck->locals = NULL;

        ck->self_t = NULL;
        ck->self_type = NULL;
        ck->cur_fn = NULL;
        free(self_t);
    }
}

static void ck_check_fn(Checker* ck, FnDef* f) {
    ck->cur_fn = f;
    ck->loop_depth = 0;
    ck->self_type = NULL;
    ck->self_t = NULL;

    for (int i = 0; i < f->nparams; i++)
        ck_add_local(ck, f->params[i].name, ck_clone_type(f->params[i].type), NULL);

    if (f->body) {
        ck_stmt(ck, f->body);
        ck_check_returns(ck, f);
    }

    while (ck->locals) ck_pop_scope(ck);
    ck->locals = NULL;
    ck->cur_fn = NULL;
}

/* ── entry point ───────────────────────────────────────────────────── */

static int ck_check_program(Checker* ck, Program* prog) {
    ck->s->prog = prog;
    ck->scope = ck->s->scope;
    ck->locals = NULL;

    /* scan raw regions for C names */
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_RAW) scan_raw_region(ck, it->raw, it->raw_len);
    }

    /* pass 1: register structs/impls as type names (also for self) */
    for (int i = 0; i < prog->nitems && !ck->is_err; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_STRUCT) {
            Sym* sym = sema_lookup(ck->s, it->st->name);
            if (!sym) {
                Sym* n = sym_new_struct(it->st->name, it->st);
                scope_add(ck->scope, n);
            }
        }
    }

    /* pass 2: check bodies */
    for (int i = 0; i < prog->nitems && !ck->is_err; i++) {
        Item* it = prog->items[i];
        switch (it->kind) {
        case TOP_FN:
            ck_check_fn(ck, it->fn);
            break;
        case TOP_IMPL:
            ck_check_impl(ck, it->im);
            break;
        default:
            break;
        }
    }
    return ck->is_err;
}

/* Run the full checker. On error, sets sema->err to a malloc'd diagnostic
   and returns 1; otherwise returns 0. */
int sema_check(Sema* sema, Program* prog) {
    if (!sema) return 0;
    raw_names_free();
    sema->err = NULL;

    Checker ck;
    memset(&ck, 0, sizeof ck);
    ck.s = sema;
    ck.err = NULL;

    int err = ck_check_program(&ck, sema->prog ? sema->prog : prog);
    if (err) sema->err = ck.err ? ck.err : strdup("error");

    raw_names_free();
    return err;
}

/* Public wrappers used by the codegen to resolve receiver types for
   call/index expressions (e.g. v.items()[0].method()). */
AstType* sema_resolve_type(Sema* s, Expr* e) {
    Checker ck;
    memset(&ck, 0, sizeof(ck));
    ck.s = s;
    ck.scope = s->scope;
    return ck_resolve_type(&ck, e);
}
AstType* sema_clone_type(AstType* t) {
    return ck_clone_type(t);
}
