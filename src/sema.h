#ifndef RK_SEMA_H
#define RK_SEMA_H

#include "ast.h"
#include <stddef.h>

typedef enum {
    SYM_FN,
    SYM_STRUCT,
    SYM_IMPL,
    SYM_VAR,
    SYM_TYPE,
    SYM_ENUM,
    SYM_ENUMVARIANT,
} SymKind;

typedef struct Sym {
    char* name;
    SymKind kind;
    AstType* ret_type;
    FnDef* fn;
    StructDef* st;
    ImplDef* im;
    EnumDef* ed;          /* SYM_ENUMVARIANT: owning enum definition        */
    int variant_idx;      /* SYM_ENUMVARIANT: index of the variant          */
    Decl* decl;
    AstType* type;
    char* source_file;
    int line;
    int col;
} Sym;

typedef struct Scope {
    struct Scope* parent;
    Sym** syms;
    int nsyms;
    int cap;
} Scope;

typedef struct Sema {
    Scope* scope;
    Program* prog;
    char* err;
    char errbuf[1024];
    const char* src;
    int srclen;
    const char** include_dirs;
    size_t n_include_dirs;
    /* Structured multi-diagnostic store for analyzer/LSP tooling.
       `err` stays as the first-error string for CLI compat. */
    struct SemaDiag* diags;
    int ndiags;
    int capdiags;
} Sema;

typedef struct SemaDiag {
    int line;          /* 1-based, original file coordinates */
    int col;           /* 1-based */
    int end_col;       /* 1-based exclusive-ish; >= col */
    int severity;      /* 1: error, 2: warning, 3: info, 4: hint */
    char code[16];     /* e.g. "E1001" */
    char file[512];    /* original file via @rk:src markers (may be "") */
    char message[512]; /* short message without location prefix */
} SemaDiag;

void sema_diag_add(Sema* s, int line, int col, int end_col, int severity,
                   const char* code, const char* msg);
void sema_clear_diags(Sema* s);
int sema_diag_count(Sema* s);
SemaDiag* sema_diag_get(Sema* s, int idx);

Sema* sema_new(void);
void sema_free(Sema* s);

int sema_collect(Sema* s, Program* prog);
void sema_set_source(Sema* s, const char* src, int len);
void sema_set_include_dirs(Sema* s, const char** dirs, size_t n_dirs);
Sym* sema_lookup(Sema* s, const char* name);
Sym* sema_lookup_method(Sema* s, const char* type_name, const char* method_name);

/* For a unit-enum variant name, return the owning enum's name (first match),
   or NULL if `name` is not a registered enum variant. */
const char* sema_lookup_variant(Sema* s, const char* name);
StructDef* sema_lookup_struct(Sema* s, const char* name);
EnumDef* sema_lookup_enum(Sema* s, const char* name);
int sema_is_module(Sema* s, const char* name);

/* Run the full type-checking pass. On the first error, sets `s->err` to a
   malloc'd source-accurate diagnostic and returns 1; otherwise returns 0. */
int sema_check(Sema* s, Program* prog);

/* Analyzer entrypoint: checks each top-level item independently so one
   function's error doesn't suppress diagnostics for the rest of the file.
   Populates s->diags (structured) and sets s->err to the first error for
   CLI compat. Returns the number of error diagnostics. */
int sema_check_all(Sema* s, Program* prog);
int sema_check_all_ex(Sema* s, Program* prog, int max_items);

typedef void (*SemaRawHook)(void);
void sema_set_raw_hook(SemaRawHook hook);
void sema_add_raw_name(const char* name);

const char* sema_type_cname(Sema* s, AstType* t);
const char* sema_mangle_method(Sema* s, const char* type_name, const char* method_name);

const char* sema_lookup_cfunc(const char* name);
const char* sema_cfunc_ret(const char* name);
const char* sema_lookup_cfunc_param(const char* name, int pidx);
int sema_cfunc_nparams(const char* name);
int sema_cfunc_is_variadic(const char* name);

/* Load C API signatures (name/ret/params, plus variadic markers) from
   commandlist.json. Idempotent: the first call wins. `basedir` is the source
   directory (project-local fallback); `override` is an explicit path from
   config/flag (may be NULL). If no file is found, rokade falls back to its
   built-in C name table. */
void sema_load_commandlist(const char* basedir, const char* override);

/* Resolve the static type of an expression (mirrors the internal checker
   type resolver). Returns an owned AstType* that the caller must free, or
   NULL if the type is unresolvable. Used by the codegen for call/index
   receivers (e.g. v.items()[0]). */
AstType* sema_resolve_type(Sema* s, Expr* e);
AstType* sema_clone_type(AstType* t);
AstType* sema_mk_type(const char* qual, const char* name, int ptrs);
int sema_is_cfunc(const char* name);
int sema_register_cfunc(const char* name, const char* ret, const char* param_types, int nparams, int is_variadic);
int sema_register_cfunc_loc(const char* name, const char* ret, const char* param_types, int nparams, int is_variadic, const char* header, int line, int col);
int sema_register_cstruct(Sema* s, const char* name, StructField* fields, int nfields);
int sema_register_cstruct_loc(Sema* s, const char* name, StructField* fields, int nfields, const char* header, int line, int col);
int sema_register_ctypedef(Sema* s, const char* name, AstType* type);
int sema_register_ctypedef_loc(Sema* s, const char* name, AstType* type, const char* header, int line, int col);
int sema_register_cvar(Sema* s, const char* name, AstType* type);
int sema_register_cvar_loc(Sema* s, const char* name, AstType* type, const char* header, int line, int col);
int sema_register_cconst(Sema* s, const char* name, AstType* type, const char* val_str);
int sema_register_cconst_loc(Sema* s, const char* name, AstType* type, const char* val_str, const char* header, int line, int col);

size_t sema_cfunc_count(void);
const char* sema_cfunc_name(size_t idx);
const char* sema_cfunc_ret_at(size_t idx);
const char* sema_cfunc_params_at(size_t idx);
int sema_cfunc_nparams_at(size_t idx);
int sema_cfunc_is_variadic_at(size_t idx);
const char* sema_cfunc_header_at(size_t idx);
int sema_cfunc_line_at(size_t idx);
int sema_cfunc_col_at(size_t idx);
int sema_lookup_cfunc_loc(const char* name, const char** out_header, int* out_line, int* out_col);

int sema_scope_sym_count(Sema* s);
Sym* sema_scope_sym_get(Sema* s, int idx);

#endif