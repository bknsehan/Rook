#include "lsp_features.h"
#include "lsp_json.h"
#include "lsp_workspace.h"
#include "../ast.h"
#include "../sema.h"
#include "../emit.h"
#include "../resolve.h"
#include "../rk_dirent.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#ifdef _WIN32
#include <io.h>
#define access _access
#define R_OK 4
#else
#include <unistd.h>
#endif

/* ─── AST Ident Searching Helpers ───────────────────────────────────── */

static void consider_ident(Expr* e, int L, int C, Expr** out) {
    if (*out || !e || e->line != L) return;
    int elen = e->len > 0 ? e->len : (e->str ? (int)strlen(e->str) : 1);
    if (e->kind == E_IDENT && C >= e->col && C <= e->col + elen) {
        *out = e;
    } else if (e->kind == E_MEMBER && e->str) {
        int m_col = e->col > 0 ? e->col + 1 : 1;
        int m_len = (int)strlen(e->str);
        if (C >= m_col && C <= m_col + m_len) {
            *out = e;
        }
    }
}

static void walk_expr_ident(Expr* e, int L, int C, Expr** out) {
    if (!e || *out) return;
    consider_ident(e, L, C, out);
    walk_expr_ident(e->a, L, C, out);
    walk_expr_ident(e->b, L, C, out);
    walk_expr_ident(e->c, L, C, out);
    for (int i = 0; i < e->nitems && !*out; i++) walk_expr_ident(e->items[i], L, C, out);
    for (int i = 0; i < e->ncitems && !*out; i++)
        if (e->citems[i].e) walk_expr_ident(e->citems[i].e, L, C, out);
    for (int i = 0; i < e->nnfields && !*out; i++)
        if (e->nfields[i].e) walk_expr_ident(e->nfields[i].e, L, C, out);
    for (int i = 0; i < e->nmarms && !*out; i++) {
        walk_expr_ident(e->marms[i].pattern, L, C, out);
        walk_expr_ident(e->marms[i].body, L, C, out);
    }
}

static void walk_decl_ident(Decl* d, int L, int C, Expr** out) {
    if (!d || *out) return;
    if (d->line == L && d->name) {
        int nlen = (int)strlen(d->name);
        if (C >= d->col && C <= d->col + nlen) {
            static Expr decl_e;
            memset(&decl_e, 0, sizeof(decl_e));
            decl_e.kind = E_IDENT;
            decl_e.str = d->name;
            decl_e.line = d->line;
            decl_e.col = d->col;
            decl_e.len = nlen;
            decl_e.def_kind = DEF_VAR;
            decl_e.def = d;
            decl_e.type = d->type;
            *out = &decl_e;
            return;
        }
    }
    walk_expr_ident(d->dim, L, C, out);
    walk_expr_ident(d->init, L, C, out);
}

static void walk_stmt_ident(Stmt* s, int L, int C, Expr** out) {
    if (!s || *out) return;
    if (s->decl) walk_decl_ident(s->decl, L, C, out);
    walk_expr_ident(s->e, L, C, out);
    walk_expr_ident(s->cond, L, C, out);
    walk_stmt_ident(s->then, L, C, out);
    walk_stmt_ident(s->els, L, C, out);
    walk_stmt_ident(s->body, L, C, out);
    if (s->init_decl) walk_decl_ident(s->init_decl, L, C, out);
    walk_expr_ident(s->init_expr, L, C, out);
    walk_expr_ident(s->step, L, C, out);
    walk_expr_ident(s->iter, L, C, out);
    for (int i = 0; i < s->nstmts && !*out; i++) walk_stmt_ident(s->stmts[i], L, C, out);
    for (int i = 0; i < s->narms && !*out; i++) {
        for (int j = 0; j < s->arms[i].nlabels && !*out; j++)
            walk_expr_ident(s->arms[i].labels[j], L, C, out);
        walk_stmt_ident(s->arms[i].body, L, C, out);
    }
    for (int i = 0; i < s->nmarms && !*out; i++) {
        walk_expr_ident(s->marms[i].pattern, L, C, out);
        walk_expr_ident(s->marms[i].body, L, C, out);
    }
    if (s->defer) walk_stmt_ident(s->defer, L, C, out);
}

static void walk_item_ident(Item* it, int L, int C, Expr** out) {
    if (!it || *out) return;
    switch (it->kind) {
    case TOP_FN:
        if (it->fn) {
            if (it->fn->line == L && it->fn->name) {
                int nlen = (int)strlen(it->fn->name);
                if (C >= it->fn->col && C <= it->fn->col + nlen) {
                    static Expr fn_e;
                    memset(&fn_e, 0, sizeof(fn_e));
                    fn_e.kind = E_IDENT;
                    fn_e.str = it->fn->name;
                    fn_e.line = it->fn->line;
                    fn_e.col = it->fn->col;
                    fn_e.len = nlen;
                    fn_e.def_kind = DEF_FN;
                    fn_e.def = it->fn;
                    *out = &fn_e;
                    return;
                }
            }
            if (it->fn->body) walk_stmt_ident(it->fn->body, L, C, out);
        }
        break;
    case TOP_STRUCT:
        if (it->st) {
            if (it->st->line == L && it->st->name) {
                int nlen = (int)strlen(it->st->name);
                if (C >= it->st->col && C <= it->st->col + nlen) {
                    static Expr st_e;
                    memset(&st_e, 0, sizeof(st_e));
                    st_e.kind = E_IDENT;
                    st_e.str = it->st->name;
                    st_e.line = it->st->line;
                    st_e.col = it->st->col;
                    st_e.len = nlen;
                    st_e.def_kind = DEF_STRUCT;
                    st_e.def = it->st;
                    *out = &st_e;
                    return;
                }
            }
            for (int i = 0; i < it->st->nfields && !*out; i++)
                walk_expr_ident(it->st->fields[i].dim, L, C, out);
        }
        break;
    case TOP_ENUM:
        if (it->ed) {
            if (it->ed->line == L && it->ed->name) {
                int nlen = (int)strlen(it->ed->name);
                if (C >= it->ed->col && C <= it->ed->col + nlen) {
                    static Expr ed_e;
                    memset(&ed_e, 0, sizeof(ed_e));
                    ed_e.kind = E_IDENT;
                    ed_e.str = it->ed->name;
                    ed_e.line = it->ed->line;
                    ed_e.col = it->ed->col;
                    ed_e.len = nlen;
                    ed_e.def_kind = DEF_ENUM;
                    ed_e.def = it->ed;
                    *out = &ed_e;
                    return;
                }
            }
            for (int i = 0; i < it->ed->nvariants && !*out; i++) {
                EnumVariant* v = &it->ed->variants[i];
                if (v->line == L && v->name) {
                    int nlen = (int)strlen(v->name);
                    if (C >= v->col && C <= v->col + nlen) {
                        static Expr var_e;
                        memset(&var_e, 0, sizeof(var_e));
                        var_e.kind = E_IDENT;
                        var_e.str = v->name;
                        var_e.line = v->line;
                        var_e.col = v->col;
                        var_e.len = nlen;
                        var_e.def_kind = DEF_VARIANT;
                        var_e.def = v;
                        *out = &var_e;
                        return;
                    }
                }
            }
        }
        break;
    case TOP_IMPL:
        if (it->im) {
            for (int i = 0; i < it->im->nmethods && !*out; i++) {
                FnDef* m = it->im->methods[i];
                if (m) {
                    if (m->line == L && m->name) {
                        int nlen = (int)strlen(m->name);
                        if (C >= m->col && C <= m->col + nlen) {
                            static Expr me_e;
                            memset(&me_e, 0, sizeof(me_e));
                            me_e.kind = E_IDENT;
                            me_e.str = m->name;
                            me_e.line = m->line;
                            me_e.col = m->col;
                            me_e.len = nlen;
                            me_e.def_kind = DEF_FN;
                            me_e.def = m;
                            *out = &me_e;
                            return;
                        }
                    }
                    if (m->body) walk_stmt_ident(m->body, L, C, out);
                }
            }
        }
        break;
    default: break;
    }
}

static Expr* find_ident_at(Program* p, int L1, int C1) {
    if (!p) return NULL;
    Expr* out = NULL;
    for (int i = 0; i < p->nitems && !out; i++) walk_item_ident(p->items[i], L1, C1, &out);
    return out;
}

static int def_location(DefKind kind, void* def, int* line, int* col, const char** out_file) {
    if (!def) return 1;
    if (out_file) *out_file = NULL;
    switch (kind) {
    case DEF_FN:      { FnDef* f = (FnDef*)def; *line = f->line; *col = f->col; if (out_file) *out_file = f->source_file; return 0; }
    case DEF_STRUCT:  { StructDef* s = (StructDef*)def; *line = s->line; *col = s->col; if (out_file) *out_file = s->source_file; return 0; }
    case DEF_ENUM:    { EnumDef* e = (EnumDef*)def; *line = e->line; *col = e->col; if (out_file) *out_file = e->source_file; return 0; }
    case DEF_VARIANT: { EnumVariant* v = (EnumVariant*)def; *line = v->line; *col = v->col; return 0; }
    case DEF_VAR:     { Decl* d = (Decl*)def; *line = d->line; *col = d->col; return 0; }
    default: break;
    }
    return 1;
}

static char* format_ast_type(AstType* t) {
    if (!t) return strdup("void");
    SB sb;
    sb_init(&sb);
    if (t->qual && t->qual[0]) {
        sb_append(&sb, t->qual);
        sb_append(&sb, " ");
    }
    sb_append(&sb, t->name ? t->name : "void");
    for (int i = 0; i < t->ptrs; i++) sb_append(&sb, "*");
    char* s = sb_strdup(&sb);
    sb_free(&sb);
    return s;
}

static char* format_fn_sig(FnDef* fn) {
    if (!fn) return strdup("");
    SB sb;
    sb_init(&sb);
    char* r = format_ast_type(fn->ret);
    sb_appendf(&sb, "%s %s(", r, fn->name);
    free(r);
    for (int i = 0; i < fn->nparams; i++) {
        if (i > 0) sb_append(&sb, ", ");
        char* pt = format_ast_type(fn->params[i].type);
        if (fn->params[i].name) {
            sb_appendf(&sb, "%s %s", pt, fn->params[i].name);
        } else {
            sb_append(&sb, pt);
        }
        free(pt);
    }
    sb_append(&sb, ")");
    char* res = sb_strdup(&sb);
    sb_free(&sb);
    return res;
}

static void get_word_at_pos(const char* text, int len, int line, int character, char* out, size_t cap) {
    if (!text || len <= 0 || !out || cap == 0) return;
    out[0] = '\0';
    int offset = lsp_pos_to_offset(text, len, line, character);
    if (offset < 0 || offset > len) return;
    int cur = offset;
    if (cur >= len || (!isalnum((unsigned char)text[cur]) && text[cur] != '_' && text[cur] != '#')) {
        if (cur > 0 && (isalnum((unsigned char)text[cur - 1]) || text[cur - 1] == '_' || text[cur - 1] == '#')) {
            cur--;
        }
    }
    if (cur < 0 || cur >= len || (!isalnum((unsigned char)text[cur]) && text[cur] != '_' && text[cur] != '#')) {
        return;
    }
    int start = cur;
    while (start > 0 && (isalnum((unsigned char)text[start - 1]) || text[start - 1] == '_')) {
        start--;
    }
    if (start > 0 && text[start - 1] == '#') {
        start--;
    }
    int end = cur;
    if (text[end] == '#') end++;
    while (end < len && (isalnum((unsigned char)text[end]) || text[end] == '_')) {
        end++;
    }
    size_t wlen = (size_t)(end - start);
    if (wlen >= cap) wlen = cap - 1;
    memcpy(out, text + start, wlen);
    out[wlen] = '\0';
}

static void find_decl_in_stmt(Stmt* s, const char* var_name, AstType** out_type) {
    if (!s || *out_type) return;
    if (s->decl && s->decl->name && strcmp(s->decl->name, var_name) == 0) {
        if (s->decl->type) { *out_type = s->decl->type; return; }
    }
    if (s->init_decl && s->init_decl->name && strcmp(s->init_decl->name, var_name) == 0) {
        if (s->init_decl->type) { *out_type = s->init_decl->type; return; }
    }
    for (int i = 0; i < s->nstmts && !*out_type; i++) {
        find_decl_in_stmt(s->stmts[i], var_name, out_type);
    }
    if (s->then) find_decl_in_stmt(s->then, var_name, out_type);
    if (s->els) find_decl_in_stmt(s->els, var_name, out_type);
    if (s->body) find_decl_in_stmt(s->body, var_name, out_type);
}

static const char* find_var_type(LspDoc* doc, const char* var_name, int cursor_line) {
    if (!doc || !var_name || !var_name[0]) return NULL;
    if (doc->prog) {
        for (int i = 0; i < doc->prog->nitems; i++) {
            Item* it = doc->prog->items[i];
            if (it->kind == TOP_FN && it->fn) {
                FnDef* fn = it->fn;
                for (int p = 0; p < fn->nparams; p++) {
                    if (fn->params[p].name && strcmp(fn->params[p].name, var_name) == 0) {
                        if (fn->params[p].type && fn->params[p].type->name)
                            return fn->params[p].type->name;
                    }
                }
                AstType* local_t = NULL;
                if (fn->body) find_decl_in_stmt(fn->body, var_name, &local_t);
                if (local_t && local_t->name) return local_t->name;
            } else if (it->kind == TOP_IMPL && it->im) {
                for (int m = 0; m < it->im->nmethods; m++) {
                    FnDef* fn = it->im->methods[m];
                    if (fn) {
                        for (int p = 0; p < fn->nparams; p++) {
                            if (fn->params[p].name && strcmp(fn->params[p].name, var_name) == 0) {
                                if (fn->params[p].type && fn->params[p].type->name)
                                    return fn->params[p].type->name;
                            }
                        }
                        AstType* local_t = NULL;
                        if (fn->body) find_decl_in_stmt(fn->body, var_name, &local_t);
                        if (local_t && local_t->name) return local_t->name;
                    }
                }
            }
        }
    }
    for (int l = cursor_line; l >= 0; l--) {
        int prefix_len = 0;
        const char* ltext = lsp_get_line_prefix(doc->text, doc->len, l, 1000, &prefix_len);
        if (prefix_len <= 0) continue;
        char linebuf[512];
        if (prefix_len >= (int)sizeof(linebuf)) prefix_len = sizeof(linebuf) - 1;
        memcpy(linebuf, ltext, prefix_len);
        linebuf[prefix_len] = '\0';
        char* vpos = strstr(linebuf, var_name);
        if (vpos) {
            const char* before = vpos - 1;
            while (before >= linebuf && (*before == ' ' || *before == '\t' || *before == '*')) before--;
            if (before >= linebuf) {
                const char* t_end = before + 1;
                const char* t_start = before;
                while (t_start > linebuf && (isalnum((unsigned char)t_start[-1]) || t_start[-1] == '_')) t_start--;
                int tlen = (int)(t_end - t_start);
                static char found_type[128];
                if (tlen > 0 && tlen < (int)sizeof(found_type)) {
                    memcpy(found_type, t_start, tlen);
                    found_type[tlen] = '\0';
                    if (strcmp(found_type, "let") != 0 && strcmp(found_type, "return") != 0 && strcmp(found_type, "mut") != 0) {
                        return found_type;
                    }
                }
            }
            const char* after = vpos + strlen(var_name);
            while (*after == ' ' || *after == '\t') after++;
            if (*after == ':') {
                after++;
                while (*after == ' ' || *after == '\t') after++;
                const char* t_start = after;
                while (*after && (isalnum((unsigned char)*after) || *after == '_')) after++;
                int tlen = (int)(after - t_start);
                static char found_type[128];
                if (tlen > 0 && tlen < (int)sizeof(found_type)) {
                    memcpy(found_type, t_start, tlen);
                    found_type[tlen] = '\0';
                    return found_type;
                }
            }
        }
    }
    return NULL;
}

static void append_completion_item_ext(SB* res, int* count, const char* label, int kind,
                                       const char* detail, const char* insert_text,
                                       int insert_format, const char* sort_text,
                                       const char* doc_str) {
    if (*count > 0) sb_append(res, ",");
    sb_append(res, "{\"label\":");
    json_emit_escaped_str(res, label);
    sb_appendf(res, ",\"kind\":%d", kind);
    if (detail && detail[0]) {
        sb_append(res, ",\"detail\":");
        json_emit_escaped_str(res, detail);
    }
    if (insert_text && insert_text[0]) {
        sb_append(res, ",\"insertText\":");
        json_emit_escaped_str(res, insert_text);
    }
    if (insert_format > 1) {
        sb_appendf(res, ",\"insertTextFormat\":%d", insert_format);
    }
    if (sort_text && sort_text[0]) {
        sb_append(res, ",\"sortText\":");
        json_emit_escaped_str(res, sort_text);
    }
    if (doc_str && doc_str[0]) {
        sb_append(res, ",\"documentation\":{\"kind\":\"markdown\",\"value\":");
        json_emit_escaped_str(res, doc_str);
        sb_append(res, "}");
    }
    sb_append(res, "}");
    (*count)++;
}


static void collect_stmt_locals(Stmt* s, int cur_line, SB* res, int* count) {
    if (!s) return;
    if (s->decl && s->decl->name && s->decl->line <= cur_line) {
        char* tstr = format_ast_type(s->decl->type);
        append_completion_item_ext(res, count, s->decl->name, 6 /* Variable */, tstr, NULL, 1, "0_var", "Local variable");
        free(tstr);
    }
    if (s->init_decl && s->init_decl->name && s->init_decl->line <= cur_line) {
        char* tstr = format_ast_type(s->init_decl->type);
        append_completion_item_ext(res, count, s->init_decl->name, 6 /* Variable */, tstr, NULL, 1, "0_var", "Loop variable");
        free(tstr);
    }
    if (s->kind == S_FORIN && s->var) {
        append_completion_item_ext(res, count, s->var, 6 /* Variable */, "Iterator variable", NULL, 1, "0_var", "For-in iterator variable");
    }
    for (int i = 0; i < s->nstmts; i++) {
        collect_stmt_locals(s->stmts[i], cur_line, res, count);
    }
    if (s->then) collect_stmt_locals(s->then, cur_line, res, count);
    if (s->els) collect_stmt_locals(s->els, cur_line, res, count);
    if (s->body) collect_stmt_locals(s->body, cur_line, res, count);
}

static void collect_fn_locals(FnDef* fn, int cur_line, SB* res, int* count) {
    if (!fn) return;
    for (int p = 0; p < fn->nparams; p++) {
        if (fn->params[p].name) {
            char* tstr = format_ast_type(fn->params[p].type);
            append_completion_item_ext(res, count, fn->params[p].name, 6 /* Variable */, tstr, NULL, 1, "0_param", "Function parameter");
            free(tstr);
        }
    }
    if (fn->body) {
        collect_stmt_locals(fn->body, cur_line, res, count);
    }
}

static void collect_scope_locals(Program* prog, int cur_line, SB* res, int* count) {
    if (!prog) return;
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_FN && it->fn) {
            int next_line = 1000000;
            if (i + 1 < prog->nitems && prog->items[i + 1]->fn) {
                next_line = prog->items[i + 1]->fn->line;
            }
            if (cur_line >= it->fn->line && cur_line < next_line) {
                collect_fn_locals(it->fn, cur_line, res, count);
                break;
            }
        } else if (it->kind == TOP_IMPL && it->im) {
            for (int m = 0; m < it->im->nmethods; m++) {
                FnDef* fn = it->im->methods[m];
                if (fn && cur_line >= fn->line) {
                    int next_m_line = 1000000;
                    if (m + 1 < it->im->nmethods && it->im->methods[m + 1]) {
                        next_m_line = it->im->methods[m + 1]->line;
                    }
                    if (cur_line < next_m_line) {
                        collect_fn_locals(fn, cur_line, res, count);
                        break;
                    }
                }
            }
        }
    }
}

static const char* lookup_libc_header_for_symbol(const char* name) {
    if (!name || !name[0]) return NULL;
    static const struct { const char* func; const char* header; } map[] = {
        {"printf", "stdio.h"}, {"scanf", "stdio.h"}, {"fopen", "stdio.h"}, {"fclose", "stdio.h"},
        {"puts", "stdio.h"}, {"fputs", "stdio.h"}, {"fgets", "stdio.h"}, {"fprintf", "stdio.h"},
        {"sprintf", "stdio.h"}, {"snprintf", "stdio.h"}, {"fread", "stdio.h"}, {"fwrite", "stdio.h"},
        {"getchar", "stdio.h"}, {"putchar", "stdio.h"}, {"feof", "stdio.h"}, {"ferror", "stdio.h"},
        {"fflush", "stdio.h"}, {"fseek", "stdio.h"}, {"ftell", "stdio.h"}, {"perror", "stdio.h"},
        {"malloc", "stdlib.h"}, {"calloc", "stdlib.h"}, {"realloc", "stdlib.h"}, {"free", "stdlib.h"},
        {"exit", "stdlib.h"}, {"abort", "stdlib.h"}, {"atoi", "stdlib.h"}, {"atol", "stdlib.h"},
        {"strtol", "stdlib.h"}, {"rand", "stdlib.h"}, {"srand", "stdlib.h"}, {"qsort", "stdlib.h"},
        {"abs", "stdlib.h"}, {"system", "stdlib.h"}, {"getenv", "stdlib.h"},
        {"strlen", "string.h"}, {"strcmp", "string.h"}, {"strncmp", "string.h"}, {"strcpy", "string.h"},
        {"strncpy", "string.h"}, {"strcat", "string.h"}, {"strncat", "string.h"}, {"strchr", "string.h"},
        {"strrchr", "string.h"}, {"strstr", "string.h"}, {"strdup", "string.h"}, {"memcpy", "string.h"},
        {"memset", "string.h"}, {"memmove", "string.h"}, {"memcmp", "string.h"}, {"memchr", "string.h"},
        {"sin", "math.h"}, {"cos", "math.h"}, {"tan", "math.h"}, {"sqrt", "math.h"},
        {"pow", "math.h"}, {"floor", "math.h"}, {"ceil", "math.h"}, {"fabs", "math.h"},
        {"time", "time.h"}, {"clock", "time.h"}, {"difftime", "time.h"},
        {NULL, NULL}
    };
    for (int i = 0; map[i].func; i++) {
        if (strcmp(map[i].func, name) == 0) return map[i].header;
    }
    return NULL;
}

/* ─── Language Keywords & Builtin Types Documentation ───────────────── */

typedef struct KeywordHover {
    const char* name;
    const char* syntax;
    const char* detail;
    const char* desc;
    const char* example;
} KeywordHover;

static const KeywordHover g_builtin_hovers[] = {
    /* Language Constructs & Keywords */
    {
        "struct",
        "struct Name {\n    field: Type;\n}",
        "composite record type",
        "Defines a composite record type grouping named fields in contiguous memory. Supports single inheritance (`struct Child : Base`).",
        "struct Point {\n    x: float;\n    y: float;\n}"
    },
    {
        "impl",
        "impl TargetType {\n    ReturnType method(TargetType* self, ...) { ... }\n}",
        "method implementation",
        "Attaches methods and associated functions to a struct or sum type.",
        "impl Point {\n    void reset(Point* self) {\n        self->x = 0.0f;\n        self->y = 0.0f;\n    }\n}"
    },
    {
        "sum",
        "sum Name {\n    Variant1,\n    Variant2(PayloadType)\n}",
        "algebraic data type",
        "Defines an algebraic data type (tagged union). Variants can hold typed payloads, checked exhaustively with match.",
        "sum Shape {\n    Circle(float),\n    Rect(float, float)\n}"
    },
    {
        "enum",
        "enum Name {\n    Variant1,\n    Variant2\n}",
        "enumeration type",
        "Defines an enumeration of distinct named integer constants.",
        "enum Direction {\n    Up,\n    Down,\n    Left,\n    Right\n}"
    },
    {
        "defer",
        "defer <statement>;",
        "deferred execution",
        "Defers execution of a cleanup statement until the enclosing scope exits.",
        "file = fopen(\"data.txt\", \"r\");\ndefer fclose(file);"
    },
    {
        "match",
        "match (expr) {\n    Pattern => statement,\n}",
        "pattern matching",
        "Exhaustive pattern matching construct for destructuring sum types or branching on values.",
        "match (shape) {\n    Shape::Circle(r) => print(\"circle\"),\n    Shape::Rect(w, h) => print(\"rect\"),\n}"
    },
    {
        "for",
        "for (init; condition; step) { ... }\nfor (item in iterable) { ... }",
        "iteration loop",
        "Iteration loop construct supporting C-style 3-clause loops and iterator loops.",
        "for (int i = 0; i < 10; i++) { ... }\nfor (item in items) { ... }"
    },
    {
        "in",
        "for (item in iterable)",
        "iterator keyword",
        "Iterator keyword used in for-in loops to traverse elements in an array or collection.",
        NULL
    },
    {
        "while",
        "while (condition) { ... }",
        "conditional loop",
        "Loop construct that repeatedly executes its body as long as the condition evaluates to true.",
        "while (running) {\n    update();\n}"
    },
    {
        "if",
        "if (condition) { ... } else { ... }",
        "conditional branch",
        "Conditional branching construct based on a boolean expression.",
        "if (hp <= 0) {\n    die();\n} else {\n    fight();\n}"
    },
    {
        "else",
        "else { ... }",
        "fallback branch",
        "Fallback branch of an if statement, executed when the condition evaluates to false.",
        NULL
    },
    {
        "return",
        "return expr;",
        "return from function",
        "Terminates execution of the current function and passes the return value back to the caller.",
        "return 0;"
    },
    {
        "break",
        "break;",
        "break loop/switch",
        "Immediately terminates the innermost enclosing for, while, or switch construct.",
        NULL
    },
    {
        "continue",
        "continue;",
        "continue loop",
        "Skips the remainder of the current loop iteration and advances to the next.",
        NULL
    },
    {
        "switch",
        "switch (expr) {\n    case val: ... break;\n    default: ... break;\n}",
        "multi-way branch",
        "Multi-way branching construct based on integer or character expressions.",
        NULL
    },
    {
        "case",
        "case value:",
        "switch case arm",
        "Arm within a switch statement matched against the switch expression.",
        NULL
    },
    {
        "default",
        "default:",
        "switch default arm",
        "Fallback arm in a switch statement executed when no other case matches.",
        NULL
    },
    {
        "extern",
        "extern ReturnType func_name(ParamType param);",
        "external C declaration",
        "Declares an external function or variable defined in a C library or foreign object file.",
        "extern void printf(char* fmt, ...);"
    },
    {
        "auto",
        "auto name = expr;",
        "type inference",
        "Infers the variable type automatically from its initializing expression.",
        "auto pos = get_player_position();"
    },
    {
        "const",
        "const Type name = value;",
        "constant modifier",
        "Immutable variable modifier. Prevents reassignment after initialization.",
        "const float PI = 3.14159f;"
    },
    {
        "mut",
        "mut Type name = value;",
        "mutable modifier",
        "Explicit mutable variable modifier.",
        NULL
    },
    {
        "alias",
        "alias NewName = TargetType;",
        "type alias",
        "Defines a type alias, creating an alternate name for an existing type.",
        "alias EntityId = uint32;"
    },
    {
        "sizeof",
        "sizeof(Type)",
        "size in bytes",
        "Compile-time operator yielding the memory size of a type or expression in bytes.",
        "int size = sizeof(Entity);"
    },
    {
        "cast",
        "cast(TargetType, expr)",
        "type conversion",
        "Explicitly converts an expression from one type to another.",
        "float f = cast(float, int_val);"
    },
    {
        "#comprise",
        "#comprise <std/module>\n#comprise path.to.module",
        "module import directive",
        "Imports definitions and symbols from a Rook standard library or project module.",
        "#comprise <std/io>\n#comprise core.math"
    },
    {
        "comprise",
        "#comprise <std/module>\n#comprise path.to.module",
        "module import directive",
        "Imports definitions and symbols from a Rook standard library or project module.",
        "#comprise <std/io>\n#comprise core.math"
    },
    {
        "#include",
        "#include <header.h>",
        "C header include",
        "Includes a C standard or third-party header file directly for native C interop.",
        "#include <math.h>\n#include <stdio.h>"
    },
    {
        "include",
        "#include <header.h>",
        "C header include",
        "Includes a C standard or third-party header file directly for native C interop.",
        "#include <math.h>\n#include <stdio.h>"
    },
    {
        "#define",
        "#define NAME <value>",
        "macro definition",
        "Defines a preprocessor macro or compile-time constant.",
        "#define MAX_BUFFER 1024"
    },
    {
        "define",
        "#define NAME <value>",
        "macro definition",
        "Defines a preprocessor macro or compile-time constant.",
        "#define MAX_BUFFER 1024"
    },
    {
        "null",
        "null",
        "null literal",
        "Null pointer literal value (equivalent to `(void*)0`).",
        NULL
    },
    {
        "true",
        "true",
        "boolean literal",
        "Boolean literal value representing truth.",
        NULL
    },
    {
        "false",
        "false",
        "boolean literal",
        "Boolean literal value representing falsehood.",
        NULL
    },

    /* Primitive & Built-in Types */
    {
        "int",
        "int",
        "signed 32-bit integer",
        "Signed 32-bit integer (-2,147,483,648 to 2,147,483,647). Standard signed integer type.",
        "count: int = 10;"
    },
    {
        "int32",
        "int32",
        "signed 32-bit integer",
        "Signed 32-bit integer (-2,147,483,648 to 2,147,483,647).",
        "x: int32 = 0;"
    },
    {
        "i32",
        "i32",
        "signed 32-bit integer",
        "Signed 32-bit integer (-2,147,483,648 to 2,147,483,647).",
        NULL
    },
    {
        "int8",
        "int8",
        "signed 8-bit integer",
        "Signed 8-bit integer (-128 to 127).",
        "delta: int8 = -1;"
    },
    {
        "i8",
        "i8",
        "signed 8-bit integer",
        "Signed 8-bit integer (-128 to 127).",
        NULL
    },
    {
        "int16",
        "int16",
        "signed 16-bit integer",
        "Signed 16-bit integer (-32,768 to 32,767).",
        "val: int16 = 1000;"
    },
    {
        "i16",
        "i16",
        "signed 16-bit integer",
        "Signed 16-bit integer (-32,768 to 32,767).",
        NULL
    },
    {
        "int64",
        "int64",
        "signed 64-bit integer",
        "Signed 64-bit integer (-9,223,372,036,854,775,808 to 9,223,372,036,854,775,807).",
        "large: int64 = 10000000000L;"
    },
    {
        "i64",
        "i64",
        "signed 64-bit integer",
        "Signed 64-bit integer.",
        NULL
    },
    {
        "uint",
        "uint",
        "unsigned 32-bit integer",
        "Unsigned 32-bit integer (0 to 4,294,967,295).",
        "idx: uint = 0;"
    },
    {
        "uint32",
        "uint32",
        "unsigned 32-bit integer",
        "Unsigned 32-bit integer (0 to 4,294,967,295).",
        NULL
    },
    {
        "u32",
        "u32",
        "unsigned 32-bit integer",
        "Unsigned 32-bit integer (0 to 4,294,967,295).",
        NULL
    },
    {
        "uint8",
        "uint8",
        "unsigned 8-bit integer",
        "Unsigned 8-bit integer (0 to 255). Commonly used for raw bytes, buffers, and color values.",
        "byte: uint8 = 255;"
    },
    {
        "u8",
        "u8",
        "unsigned 8-bit integer",
        "Unsigned 8-bit integer (0 to 255).",
        NULL
    },
    {
        "uint16",
        "uint16",
        "unsigned 16-bit integer",
        "Unsigned 16-bit integer (0 to 65,535).",
        "port: uint16 = 8080;"
    },
    {
        "u16",
        "u16",
        "unsigned 16-bit integer",
        "Unsigned 16-bit integer (0 to 65,535).",
        NULL
    },
    {
        "uint64",
        "uint64",
        "unsigned 64-bit integer",
        "Unsigned 64-bit integer (0 to 18,446,744,073,709,551,615).",
        "total: uint64 = 50000000000UL;"
    },
    {
        "u64",
        "u64",
        "unsigned 64-bit integer",
        "Unsigned 64-bit integer.",
        NULL
    },
    {
        "float",
        "float",
        "32-bit floating point",
        "Single-precision 32-bit IEEE 754 floating-point number.",
        "speed: float = 3.14f;"
    },
    {
        "float32",
        "float32",
        "32-bit floating point",
        "Single-precision 32-bit IEEE 754 floating-point number.",
        NULL
    },
    {
        "double",
        "double",
        "64-bit floating point",
        "Double-precision 64-bit IEEE 754 floating-point number.",
        "precision: double = 0.000001;"
    },
    {
        "float64",
        "float64",
        "64-bit floating point",
        "Double-precision 64-bit IEEE 754 floating-point number.",
        NULL
    },
    {
        "bool",
        "bool",
        "boolean type",
        "Boolean truth value (`true` or `false`).",
        "is_active: bool = true;"
    },
    {
        "char",
        "char",
        "character byte",
        "Single byte / ASCII character literal.",
        "ch: char = 'A';"
    },
    {
        "void",
        "void",
        "unit type",
        "Unit type representing absence of value or non-returning functions.",
        NULL
    },
    {
        "size_t",
        "size_t",
        "pointer-sized unsigned int",
        "Unsigned pointer-sized integer used for array sizes, indices, and memory allocations.",
        "len: size_t = sizeof(int);"
    },
    {
        "isize",
        "isize",
        "signed pointer-sized int",
        "Signed pointer-sized integer matching target architecture word size.",
        NULL
    },
    {
        "intptr_t",
        "intptr_t",
        "signed pointer-sized int",
        "Signed pointer-sized integer matching target architecture word size.",
        NULL
    },
    {
        "usize",
        "usize",
        "unsigned pointer-sized int",
        "Unsigned pointer-sized integer matching target architecture word size.",
        NULL
    },
    {
        "uintptr_t",
        "uintptr_t",
        "unsigned pointer-sized int",
        "Unsigned pointer-sized integer matching target architecture word size.",
        NULL
    },
    {
        "Str",
        "Str",
        "UTF-8 string type",
        "Rook standard string type with UTF-8 character buffer and length.",
        "msg: Str = \"Hello, world!\";"
    },
    {
        "string",
        "string",
        "UTF-8 string type",
        "Rook string type with UTF-8 character buffer and length.",
        "msg: string = \"Hello, world!\";"
    },
    {
        "Vec",
        "Vec[T]",
        "dynamic vector array",
        "Dynamic resizable array type providing heap-allocated linear storage.",
        NULL
    },
    {
        "Result",
        "sum Result[T, E] {\n    Ok(T),\n    Err(E)\n}",
        "result sum type",
        "Sum type for error handling representing either success (`Ok(T)`) or failure (`Err(E)`).",
        NULL
    },
    {
        "Option",
        "sum Option[T] {\n    Some(T),\n    None\n}",
        "optional sum type",
        "Sum type representing an optional value that may be present (`Some(T)`) or absent (`None`).",
        NULL
    },
    {
        "StringBuilder",
        "StringBuilder",
        "mutable string builder",
        "Efficient mutable string builder for dynamic string concatenation.",
        NULL
    },
    {
        "File",
        "File",
        "file handle abstraction",
        "Standard file handle abstraction for I/O operations.",
        NULL
    },
    {NULL, NULL, NULL, NULL, NULL}
};

static const KeywordHover* find_builtin_hover(const char* name) {
    if (!name || !name[0]) return NULL;
    const char* lookup = name;
    if (lookup[0] == '#') lookup++;

    for (int i = 0; g_builtin_hovers[i].name; i++) {
        const char* hname = g_builtin_hovers[i].name;
        if (strcmp(hname, name) == 0 || (hname[0] != '#' && strcmp(hname, lookup) == 0)) {
            return &g_builtin_hovers[i];
        }
    }
    return NULL;
}

static char* format_builtin_doc(const KeywordHover* h) {
    if (!h) return NULL;
    SB sb;
    sb_init(&sb);
    sb_appendf(&sb, "```rook\n%s\n```\n%s", h->syntax, h->desc);
    if (h->example && h->example[0]) {
        sb_appendf(&sb, "\n\n*Example:*\n```rook\n%s\n```", h->example);
    }
    char* res = sb_strdup(&sb);
    sb_free(&sb);
    return res;
}

static int format_builtin_hover(const char* name, SB* doc_sb) {
    const KeywordHover* h = find_builtin_hover(name);
    if (!h) return 0;
    char* doc = format_builtin_doc(h);
    if (doc) {
        sb_append(doc_sb, doc);
        free(doc);
        return 1;
    }
    return 0;
}

void lsp_handle_completion(LspDoc* doc, int line, int character, SB* res) {
    sb_append(res, "{\"isIncomplete\":false,\"items\":[");
    int count = 0;

    int prefix_len = 0;
    const char* prefix = lsp_get_line_prefix(doc->text, doc->len, line, character, &prefix_len);

    const char* trim_p = prefix;
    while (*trim_p == ' ' || *trim_p == '\t') trim_p++;

    int is_comp_dir = (strncmp(trim_p, "#comprise", 9) == 0 || strncmp(trim_p, "comprise ", 9) == 0);
    int is_inc_dir = (strncmp(trim_p, "#include", 8) == 0 || strncmp(trim_p, "include ", 8) == 0);

    char basedir[4096] = ".";
    if (doc->path) {
        const char* root = lsp_find_project_root(doc->path);
        if (root && strcmp(root, ".") != 0) {
            snprintf(basedir, sizeof(basedir), "%s", root);
        }
        const char* slash = strrchr(doc->path, '/');
#ifdef _WIN32
        const char* bslash = strrchr(doc->path, '\\');
        if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
        if (slash && slash > doc->path) {
            size_t dlen = (size_t)(slash - doc->path);
            if (dlen < sizeof(basedir)) {
                /* Use project root if found, otherwise fall back to file's directory */
                if (strcmp(basedir, ".") == 0 || root == NULL) {
                    memcpy(basedir, doc->path, dlen);
                    basedir[dlen] = '\0';
                }
            }
        }
    }

    if (is_comp_dir) {
        int has_quote = 0;
        for (int i = 0; i < prefix_len; i++) {
            if (prefix[i] == '"') has_quote = 1;
        }
        if (has_quote) {
            DIR* d = opendir(basedir);
            if (d) {
                struct dirent* de;
                while ((de = readdir(d)) != NULL) {
                    size_t nl = strlen(de->d_name);
                    if (nl > 5 && strcmp(de->d_name + nl - 5, ".rook") == 0) {
                        const char* doc_base = doc->path ? strrchr(doc->path, '/') : NULL;
#ifdef _WIN32
                        const char* doc_bbase = doc->path ? strrchr(doc->path, '\\') : NULL;
                        if (!doc_base || (doc_bbase && doc_bbase > doc_base)) doc_base = doc_bbase;
#endif
                        doc_base = doc_base ? doc_base + 1 : doc->path;
                        if (!doc_base || strcmp(doc_base, de->d_name) != 0) {
                            append_completion_item_ext(res, &count, de->d_name, 17 /* File */, "Local Rook file", NULL, 1, "0_local", "Local comprised Rook module");
                        }
                    }
                }
                closedir(d);
            }
            char src_sub[4096];
            snprintf(src_sub, sizeof(src_sub), "%s/src", basedir);
            d = opendir(src_sub);
            if (d) {
                struct dirent* de;
                while ((de = readdir(d)) != NULL) {
                    size_t nl = strlen(de->d_name);
                    if (nl > 5 && strcmp(de->d_name + nl - 5, ".rook") == 0) {
                        char rel_path[512];
                        snprintf(rel_path, sizeof(rel_path), "src/%s", de->d_name);
                        append_completion_item_ext(res, &count, rel_path, 17 /* File */, "Local Rook file", NULL, 1, "0_localsrc", "Local comprised Rook module in src/");
                    }
                }
                closedir(d);
            }
            lsp_workspace_complete_modules("", res, &count);
            sb_append(res, "]}");
            return;
        }

        static const struct { const char* mod; const char* desc; } std_mods[] = {
            {"std/io", "Standard I/O: print, println, file reading and writing"},
            {"std/str", "String manipulation: length, slice, split, trim, format"},
            {"std/vec", "Dynamic resizable vector array"},
            {"std/math", "Mathematical operations, trigonometry, powers, rounding"},
            {"std/mem", "Memory management: alloc, free, copy, set"},
            {"std/os", "Operating system utilities: environment, args, process"},
            {"std/fs", "File system operations: exists, read, write, remove"},
            {"std/result", "Result[T, E] type for robust error handling"},
            {"std/test", "Unit testing framework and test assertions"},
            {"std/json", "JSON parser, serializer, and value manipulation"},
            {"std/toml", "TOML configuration file parser"},
            {"std/atomic", "Atomic memory operations and barriers"},
            {"std/sync", "Thread synchronization: Mutex, CondVar, Lock"},
            {"std/log", "Structured logging: info, warn, error, debug"},
            {"std/std", "Core standard prelude combining common utilities"},
            {NULL, NULL}
        };
        for (int i = 0; std_mods[i].mod; i++) {
            append_completion_item_ext(res, &count, std_mods[i].mod, 9 /* Module */, "Standard library module", NULL, 1, "0_std", std_mods[i].desc);
        }
        sb_append(res, "]}");
        return;
    }

    if (is_inc_dir) {
        int has_quote = 0;
        for (int i = 0; i < prefix_len; i++) {
            if (prefix[i] == '"') has_quote = 1;
        }
        if (has_quote) {
            DIR* d = opendir(basedir);
            if (d) {
                struct dirent* de;
                while ((de = readdir(d)) != NULL) {
                    size_t nl = strlen(de->d_name);
                    if (nl > 2 && strcmp(de->d_name + nl - 2, ".h") == 0) {
                        append_completion_item_ext(res, &count, de->d_name, 17 /* File */, "Local C Header", NULL, 1, "0_cheader", "C header in project directory");
                    }
                }
                closedir(d);
            }
            char inc_sub[4096];
            snprintf(inc_sub, sizeof(inc_sub), "%s/include", basedir);
            d = opendir(inc_sub);
            if (d) {
                struct dirent* de;
                while ((de = readdir(d)) != NULL) {
                    size_t nl = strlen(de->d_name);
                    if (nl > 2 && strcmp(de->d_name + nl - 2, ".h") == 0) {
                        append_completion_item_ext(res, &count, de->d_name, 17 /* File */, "C Header in include/", NULL, 1, "0_cheader", "C header in include/");
                    }
                }
                closedir(d);
            }
            sb_append(res, "]}");
            return;
        }

        static const struct { const char* header; const char* desc; } c_headers[] = {
            {"stdio.h", "Standard input/output operations (printf, scanf, fopen, fgets)"},
            {"stdlib.h", "General utilities: memory allocation (malloc, free), process control, conversions"},
            {"string.h", "String handling functions (strlen, strcmp, strcpy, memcpy, memset)"},
            {"stdbool.h", "Boolean type definitions (true, false, bool)"},
            {"stdint.h", "Exact-width integer types (int8_t, int16_t, int32_t, int64_t, uint8_t, ...)"},
            {"math.h", "Common mathematical functions (sin, cos, sqrt, pow, fabs, floor, ceil)"},
            {"time.h", "Date and time handling functions (time, clock, difftime, strftime)"},
            {"errno.h", "Macros for reporting and retrieving error conditions through errno"},
            {"assert.h", "Macro for program diagnostics assertions"},
            {"pthread.h", "POSIX threads interface for multi-threading"},
            {"fcntl.h", "File control operations and open flags"},
            {"unistd.h", "Standard symbolic constants and POSIX system calls (read, write, close, sleep)"},
            {"sys/stat.h", "Data returned by the stat function and file status flags"},
            {"ctype.h", "Character classification and mapping functions (isalpha, isdigit, tolower)"},
            {"stddef.h", "Standard type definitions (size_t, ptrdiff_t, NULL, offsetof)"},
            {"limits.h", "Sizes of basic integer types and environment limits"},
            {"float.h", "Limits of floating-point types"},
            {NULL, NULL}
        };
        for (int i = 0; c_headers[i].header; i++) {
            append_completion_item_ext(res, &count, c_headers[i].header, 17 /* File */, "C Standard Header", NULL, 1, "0_header", c_headers[i].desc);
        }
        sb_append(res, "]}");
        return;
    }

    /* 3. Check for preprocessor directive completions when starting with '#' */
    if (trim_p[0] == '#') {
        append_completion_item_ext(res, &count, "#comprise", 15 /* Snippet */, "Comprise Rook module", "#comprise <${1:std/io}>", 2, "0_comp", "Include a Rook standard or local module");
        append_completion_item_ext(res, &count, "#include", 15 /* Snippet */, "Include C header", "#include <${1:stdio.h}>", 2, "0_inc", "Include a C header file");
        append_completion_item_ext(res, &count, "#pragma", 14 /* Keyword */, "Compiler directive", "#pragma ", 1, "1_pragma", "Rook pragma directive");
        append_completion_item_ext(res, &count, "#define", 14 /* Keyword */, "Macro definition", "#define ", 1, "1_define", "C preprocessor macro");
        sb_append(res, "]}");
        return;
    }

    /* 4. Check for member access completion: e.g. "ident." or "ident->" */
    int member_op_idx = -1;
    for (int i = prefix_len - 1; i >= 0; i--) {
        if (prefix[i] == '.') {
            member_op_idx = i;
            break;
        }
        if (i > 0 && prefix[i - 1] == '-' && prefix[i] == '>') {
            member_op_idx = i - 1;
            break;
        }
        if (!isalnum((unsigned char)prefix[i]) && prefix[i] != '_') break;
    }

    if (member_op_idx >= 0) {
        int ident_end = member_op_idx;
        int ident_start = ident_end;
        while (ident_start > 0 && (isalnum((unsigned char)prefix[ident_start - 1]) || prefix[ident_start - 1] == '_')) {
            ident_start--;
        }
        int idlen = ident_end - ident_start;
        char idbuf[128];
        if (idlen > 0 && idlen < (int)sizeof(idbuf)) {
            memcpy(idbuf, prefix + ident_start, idlen);
            idbuf[idlen] = '\0';

            /* Case A: Is idbuf a module alias? */
            int found_module = 0;
            if (doc->prog) {
                for (int i = 0; i < doc->prog->nitems; i++) {
                    Item* it = doc->prog->items[i];
                    if (it->kind == TOP_FN && it->fn && it->fn->mod_prefix && strcmp(it->fn->mod_prefix, idbuf) == 0) {
                        char* sig = format_fn_sig(it->fn);
                        append_completion_item_ext(res, &count, it->fn->name, 3 /* Function */, sig, NULL, 1, "0_fn", sig);
                        free(sig);
                        found_module = 1;
                    } else if (it->kind == TOP_STRUCT && it->st && it->st->mod_prefix && strcmp(it->st->mod_prefix, idbuf) == 0) {
                        append_completion_item_ext(res, &count, it->st->name, 22 /* Struct */, "Module struct", NULL, 1, "1_st", "Struct");
                        found_module = 1;
                    } else if (it->kind == TOP_ENUM && it->ed && it->ed->mod_prefix && strcmp(it->ed->mod_prefix, idbuf) == 0) {
                        append_completion_item_ext(res, &count, it->ed->name, 13 /* Enum */, "Module enum", NULL, 1, "2_en", "Enum");
                        found_module = 1;
                    }
                }
            }
            if (found_module) {
                sb_append(res, "]}");
                return;
            }

            /* Case B: Is idbuf an enum name? (e.g. Shape.Rect) */
            if (doc->sema) {
                EnumDef* ed = sema_lookup_enum(doc->sema, idbuf);
                if (ed) {
                    for (int i = 0; i < ed->nvariants; i++) {
                        append_completion_item_ext(res, &count, ed->variants[i].name, 20 /* EnumMember */, "Enum variant", NULL, 1, "0_var", "Enum variant");
                    }
                    sb_append(res, "]}");
                    return;
                }
            }

            /* Case C: Struct member / method completion */
            const char* target_type = NULL;
            if (doc->sema && sema_lookup_struct(doc->sema, idbuf)) {
                target_type = idbuf;
            } else {
                target_type = find_var_type(doc, idbuf, line);
            }
            if (target_type && doc->sema) {
                StructDef* st = sema_lookup_struct(doc->sema, target_type);
                while (st) {
                    for (int i = 0; i < st->nfields; i++) {
                        char* ft = format_ast_type(st->fields[i].type);
                        append_completion_item_ext(res, &count, st->fields[i].name, 5 /* Field */, ft ? ft : "field", NULL, 1, "0_field", "Struct field");
                        free(ft);
                    }
                    st = st->parent ? sema_lookup_struct(doc->sema, st->parent) : NULL;
                }
                /* Also find methods for target_type */
                if (doc->prog) {
                    for (int i = 0; i < doc->prog->nitems; i++) {
                        Item* it = doc->prog->items[i];
                        if (it->kind == TOP_IMPL && it->im && it->im->target && it->im->target->name && strcmp(it->im->target->name, target_type) == 0) {
                            for (int m = 0; m < it->im->nmethods; m++) {
                                char* sig = format_fn_sig(it->im->methods[m]);
                                append_completion_item_ext(res, &count, it->im->methods[m]->name, 2 /* Method */, sig, NULL, 1, "1_method", sig);
                                free(sig);
                            }
                        }
                    }
                }
                sb_append(res, "]}");
                return;
            }
        }
    }

    /* 5. General Scope completion */

    /* A. Local variables & parameters */
    if (doc->prog) {
        collect_scope_locals(doc->prog, line + 1, res, &count);
    }

    /* Code snippets */
    append_completion_item_ext(res, &count, "func", 15 /* Snippet */, "Function declaration", "${1:void} ${2:name}(${3:params}) {\n\t${0}\n}", 2, "1_snip_func", "C-style function declaration");
    append_completion_item_ext(res, &count, "function", 15 /* Snippet */, "Function declaration", "${1:void} ${2:name}(${3:params}) {\n\t${0}\n}", 2, "1_snip_function", "C-style function declaration");
    append_completion_item_ext(res, &count, "main", 15 /* Snippet */, "Main entrypoint", "int main(int argc, char** argv) {\n\t${0}\n\treturn 0;\n}", 2, "1_snip_main", "Program main entrypoint");
    append_completion_item_ext(res, &count, "struct", 15 /* Snippet */, "Struct definition", "struct ${1:Name} {\n\t${2:field}: ${3:int};\n};", 2, "1_snip_struct", "Struct type declaration");
    append_completion_item_ext(res, &count, "sum", 15 /* Snippet */, "Sum type definition", "sum ${1:Name} {\n\t${2:Variant},\n}", 2, "1_snip_sum", "Sum / Algebraic data type");
    append_completion_item_ext(res, &count, "impl", 15 /* Snippet */, "Implementation block", "impl ${1:Target} {\n\t${0}\n}", 2, "1_snip_impl", "Method implementation block");
    append_completion_item_ext(res, &count, "if", 15 /* Snippet */, "If statement", "if (${1:condition}) {\n\t${0}\n}", 2, "1_snip_if", "Conditional branch");
    append_completion_item_ext(res, &count, "ifel", 15 /* Snippet */, "If-else statement", "if (${1:condition}) {\n\t${2}\n} else {\n\t${0}\n}", 2, "1_snip_ifel", "Conditional branch with fallback");
    append_completion_item_ext(res, &count, "for", 15 /* Snippet */, "For loop", "for (${1:int i = 0}; ${2:i < count}; ${3:i++}) {\n\t${0}\n}", 2, "1_snip_for", "Indexed for loop");
    append_completion_item_ext(res, &count, "forin", 15 /* Snippet */, "For-in iterator", "for (${1:item} in ${2:iter}) {\n\t${0}\n}", 2, "1_snip_forin", "Iterator loop");
    append_completion_item_ext(res, &count, "while", 15 /* Snippet */, "While loop", "while (${1:condition}) {\n\t${0}\n}", 2, "1_snip_while", "Loop while condition holds");
    append_completion_item_ext(res, &count, "match", 15 /* Snippet */, "Pattern match", "match (${1:expr}) {\n\t${2:pattern} => ${3:action},\n}", 2, "1_snip_match", "Pattern matching expression");
    append_completion_item_ext(res, &count, "switch", 15 /* Snippet */, "Switch statement", "switch (${1:expr}) {\ncase ${2:val}:\n\t${0}\n\tbreak;\ndefault:\n\tbreak;\n}", 2, "1_snip_switch", "Switch branching");
    append_completion_item_ext(res, &count, "defer", 15 /* Snippet */, "Defer statement", "defer ${1:action};", 2, "1_snip_defer", "Execute action on scope exit");
    append_completion_item_ext(res, &count, "println", 15 /* Snippet */, "Print with newline", "println(\"${1:message}\");", 2, "1_snip_println", "Print string followed by newline");
    append_completion_item_ext(res, &count, "print", 15 /* Snippet */, "Print string", "print(\"${1:message}\");", 2, "1_snip_print", "Print string to stdout");
    append_completion_item_ext(res, &count, "#comprise", 15 /* Snippet */, "Comprise module", "#comprise <${1:std/io}>", 2, "1_snip_comp", "Module import directive");
    append_completion_item_ext(res, &count, "#include", 15 /* Snippet */, "Include C header", "#include <${1:stdio.h}>", 2, "1_snip_inc", "C header include");

    /* Language keywords */
    static const struct { const char* kw; const char* doc; } keywords[] = {
        {"struct", "Defines a composite record type with named fields"},
        {"impl", "Attaches methods and functions to a struct or sum type"},
        {"sum", "Defines an algebraic data type / tagged union"},
        {"enum", "Defines an enumeration of named integer constants"},
        {"alias", "Defines a type alias for an existing type"},
        {"extern", "Declares an external C function or variable"},
        {"auto", "Infers variable type from initializer expression"},
        {"const", "Constant variable modifier (immutable)"},
        {"mut", "Mutable variable modifier"},
        {"return", "Returns from the current function"},
        {"if", "Conditional branch construct"},
        {"else", "Fallback branch of an if condition"},
        {"while", "Loop while condition evaluates to true"},
        {"for", "Iterative loop construct (indexed or for-in)"},
        {"in", "Iterator specifier in for-in loop"},
        {"switch", "Multi-way integer or character branch"},
        {"case", "Case arm within a switch statement"},
        {"default", "Default arm within a switch statement"},
        {"match", "Pattern match expression for sum types"},
        {"defer", "Schedules cleanup statement at end of scope"},
        {"break", "Breaks out of innermost loop or switch"},
        {"continue", "Advances to next loop iteration"},
        {"comprise", "Imports standard library or project module"},
        {"as", "Aliases an imported module"},
        {"sizeof", "Evaluates memory size of type or expression in bytes"},
        {"cast", "Explicit type conversion"},
        {"true", "Boolean literal true"},
        {"false", "Boolean literal false"},
        {"null", "Null pointer literal"},
        {NULL, NULL}
    };
    for (int i = 0; keywords[i].kw; i++) {
        const KeywordHover* kh = find_builtin_hover(keywords[i].kw);
        if (kh) {
            char* mdoc = format_builtin_doc(kh);
            append_completion_item_ext(res, &count, kh->name, 14 /* Keyword */, kh->detail ? kh->detail : "Keyword", NULL, 1, "2_keyword", mdoc ? mdoc : keywords[i].doc);
            if (mdoc) free(mdoc);
        } else {
            append_completion_item_ext(res, &count, keywords[i].kw, 14 /* Keyword */, "Keyword", NULL, 1, "2_keyword", keywords[i].doc);
        }
    }

    /* Built-in & standard types */
    static const struct { const char* name; const char* desc; } types[] = {
        {"int", "Standard signed 32-bit integer"},
        {"int8", "8-bit signed integer (-128 to 127)"},
        {"int16", "16-bit signed integer (-32,768 to 32,767)"},
        {"int32", "32-bit signed integer"},
        {"int64", "64-bit signed integer"},
        {"i8", "8-bit signed integer (-128 to 127)"},
        {"i16", "16-bit signed integer (-32,768 to 32,767)"},
        {"i32", "32-bit signed integer"},
        {"i64", "64-bit signed integer"},
        {"uint", "Standard unsigned 32-bit integer"},
        {"uint8", "8-bit unsigned integer (0 to 255)"},
        {"uint16", "16-bit unsigned integer (0 to 65,535)"},
        {"uint32", "32-bit unsigned integer"},
        {"uint64", "64-bit unsigned integer"},
        {"u8", "8-bit unsigned integer (0 to 255)"},
        {"u16", "16-bit unsigned integer (0 to 65,535)"},
        {"u32", "32-bit unsigned integer"},
        {"u64", "64-bit unsigned integer"},
        {"float", "32-bit single-precision floating point"},
        {"float32", "32-bit single-precision floating point"},
        {"double", "64-bit double-precision floating point"},
        {"float64", "64-bit double-precision floating point"},
        {"bool", "Boolean type (true or false)"},
        {"char", "8-bit character / byte"},
        {"void", "Unit type / absence of value"},
        {"size_t", "Unsigned integer type for sizes and counts"},
        {"isize", "Signed pointer-sized integer"},
        {"usize", "Unsigned pointer-sized integer"},
        {"intptr_t", "Signed integer type holding a pointer"},
        {"uintptr_t", "Unsigned integer type holding a pointer"},
        {"Str", "Rook standard string type with UTF-8 buffer and length"},
        {"string", "Rook string type alias with UTF-8 buffer and length"},
        {"Vec", "Rook resizable dynamic vector array"},
        {"Result", "Sum type for error handling: Result[T, E]"},
        {"Option", "Sum type for optional values: Option[T]"},
        {"StringBuilder", "Efficient mutable string builder"},
        {"File", "Standard file handle abstraction"},
        {NULL, NULL}
    };
    for (int i = 0; types[i].name; i++) {
        const KeywordHover* kh = find_builtin_hover(types[i].name);
        if (kh) {
            char* mdoc = format_builtin_doc(kh);
            append_completion_item_ext(res, &count, kh->name, 25 /* Type */, kh->detail ? kh->detail : "Built-in type", NULL, 1, "3_type", mdoc ? mdoc : types[i].desc);
            if (mdoc) free(mdoc);
        } else {
            append_completion_item_ext(res, &count, types[i].name, 25 /* Type */, "Built-in type", NULL, 1, "3_type", types[i].desc);
        }
    }

    /* Top-level functions, structs, enums in AST */
    if (doc->prog) {
        for (int i = 0; i < doc->prog->nitems; i++) {
            Item* it = doc->prog->items[i];
            if (it->kind == TOP_FN && it->fn && !it->fn->mod_prefix) {
                char* sig = format_fn_sig(it->fn);
                append_completion_item_ext(res, &count, it->fn->name, 3 /* Function */, sig, NULL, 1, "4_fn", "Function");
                free(sig);
            } else if (it->kind == TOP_STRUCT && it->st && !it->st->mod_prefix) {
                append_completion_item_ext(res, &count, it->st->name, 22 /* Struct */, "Struct", NULL, 1, "4_struct", "Struct");
            } else if (it->kind == TOP_ENUM && it->ed && !it->ed->mod_prefix) {
                append_completion_item_ext(res, &count, it->ed->name, 13 /* Enum */, "Sum / Enum", NULL, 1, "4_enum", "Sum type");
            }
        }
    }

    /* Current word prefix at cursor */
    char word[128] = "";
    get_word_at_pos(doc->text, doc->len, line, character, word, sizeof(word));

    /* All C functions from sema (commandlist.json + libclang imported headers) */
    size_t n_cfuncs = sema_cfunc_count();
    for (size_t i = 0; i < n_cfuncs; i++) {
        const char* cname = sema_cfunc_name(i);
        if (!cname || !cname[0]) continue;

        const char* cret = sema_cfunc_ret_at(i);
        const char* cheader = sema_cfunc_header_at(i);
        char detail[256];
        snprintf(detail, sizeof(detail), "%s %s(...)", cret ? cret : "void", cname);
        char doc_buf[512];
        if (cheader && cheader[0]) {
            snprintf(doc_buf, sizeof(doc_buf), "C function from `%s`", cheader);
        } else {
            snprintf(doc_buf, sizeof(doc_buf), "C standard function");
        }
        append_completion_item_ext(res, &count, cname, 3 /* Function */, detail, NULL, 1, "5_cfunc", doc_buf);
    }

    /* All C symbols from doc->sema scope (structs, typedefs, enums, constants) */
    if (doc->sema) {
        int nsyms = sema_scope_sym_count(doc->sema);
        for (int i = 0; i < nsyms; i++) {
            Sym* sym = sema_scope_sym_get(doc->sema, i);
            if (!sym || !sym->name || !sym->name[0]) continue;

            int skind = 6; /* Variable */
            const char* sdetail = "C symbol";
            if (sym->kind == SYM_STRUCT) {
                skind = 22; /* Struct */
                sdetail = "C struct";
            } else if (sym->kind == SYM_TYPE) {
                skind = 25; /* Type */
                sdetail = "C typedef";
            } else if (sym->kind == SYM_ENUM) {
                skind = 13; /* Enum */
                sdetail = "C enum";
            } else if (sym->kind == SYM_ENUMVARIANT) {
                skind = 20; /* EnumMember */
                sdetail = "C enum constant";
            }
            char sdoc[512] = "";
            if (sym->source_file && sym->source_file[0]) {
                snprintf(sdoc, sizeof(sdoc), "Imported from `%s`", sym->source_file);
            }
            append_completion_item_ext(res, &count, sym->name, skind, sdetail, NULL, 1, "6_csym", sdoc[0] ? sdoc : sdetail);
        }
    }

    /* All workspace symbols from project files and standard library */
    lsp_workspace_complete(word, res, &count, doc ? doc->path : NULL);

    sb_append(res, "]}");
}

/* ─── Hover ─────────────────────────────────────────────────────────── */

static int hover_param_or_field(Program* prog, int line1, const char* sym_name, SB* doc_sb) {
    if (!prog || !sym_name || !sym_name[0]) return 0;

    /* Check fields in structs */
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_STRUCT && it->st) {
            for (int f = 0; f < it->st->nfields; f++) {
                if (strcmp(it->st->fields[f].name, sym_name) == 0) {
                    char* ft = format_ast_type(it->st->fields[f].type);
                    sb_appendf(doc_sb, "```rook\n%s.%s: %s\n```\n*Field of `struct %s`*",
                               it->st->name, sym_name, ft ? ft : "void", it->st->name);
                    free(ft);
                    return 1;
                }
            }
        }
    }

    /* Check parameters in functions / methods prioritizing enclosing function */
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_FN && it->fn) {
            int fn_start = it->fn->line;
            int fn_end = (i + 1 < prog->nitems && prog->items[i + 1]->fn) ? prog->items[i + 1]->fn->line : 1000000;
            if (line1 >= fn_start && line1 < fn_end) {
                for (int p = 0; p < it->fn->nparams; p++) {
                    if (it->fn->params[p].name && strcmp(it->fn->params[p].name, sym_name) == 0) {
                        char* pt = format_ast_type(it->fn->params[p].type);
                        sb_appendf(doc_sb, "```rook\n%s: %s\n```\n*Parameter of `%s`*",
                                   sym_name, pt ? pt : "void", it->fn->name);
                        free(pt);
                        return 1;
                    }
                }
            }
        } else if (it->kind == TOP_IMPL && it->im) {
            for (int m = 0; m < it->im->nmethods; m++) {
                FnDef* fn = it->im->methods[m];
                if (!fn) continue;
                int m_start = fn->line;
                int m_end = (m + 1 < it->im->nmethods && it->im->methods[m + 1]) ? it->im->methods[m + 1]->line : 1000000;
                if (line1 >= m_start && line1 < m_end) {
                    for (int p = 0; p < fn->nparams; p++) {
                        if (fn->params[p].name && strcmp(fn->params[p].name, sym_name) == 0) {
                            char* pt = format_ast_type(fn->params[p].type);
                            const char* tgt = (it->im->target && it->im->target->name) ? it->im->target->name : "impl";
                            sb_appendf(doc_sb, "```rook\n%s: %s\n```\n*Parameter of `%s::%s`*",
                                       sym_name, pt ? pt : "void", tgt, fn->name);
                            free(pt);
                            return 1;
                        }
                    }
                }
            }
        }
    }

    /* Fallback parameter check across any function or method */
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_FN && it->fn) {
            for (int p = 0; p < it->fn->nparams; p++) {
                if (it->fn->params[p].name && strcmp(it->fn->params[p].name, sym_name) == 0) {
                    char* pt = format_ast_type(it->fn->params[p].type);
                    sb_appendf(doc_sb, "```rook\n%s: %s\n```\n*Parameter of `%s`*",
                               sym_name, pt ? pt : "void", it->fn->name);
                    free(pt);
                    return 1;
                }
            }
        } else if (it->kind == TOP_IMPL && it->im) {
            for (int m = 0; m < it->im->nmethods; m++) {
                FnDef* fn = it->im->methods[m];
                if (!fn) continue;
                for (int p = 0; p < fn->nparams; p++) {
                    if (fn->params[p].name && strcmp(fn->params[p].name, sym_name) == 0) {
                        char* pt = format_ast_type(fn->params[p].type);
                        const char* tgt = (it->im->target && it->im->target->name) ? it->im->target->name : "impl";
                        sb_appendf(doc_sb, "```rook\n%s: %s\n```\n*Parameter of `%s::%s`*",
                                   sym_name, pt ? pt : "void", tgt, fn->name);
                        free(pt);
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

void lsp_handle_hover(LspDoc* doc, int line, int character, SB* res) {
    if (!doc) {
        sb_append(res, "null");
        return;
    }

    Expr* id = doc->prog ? find_ident_at(doc->prog, line + 1, character + 1) : NULL;
    char word[128] = "";
    get_word_at_pos(doc->text, doc->len, line, character, word, sizeof(word));
    const char* sym_name = (id && id->str) ? id->str : word;
    if (!sym_name || !sym_name[0]) {
        sb_append(res, "null");
        return;
    }

    SB doc_sb;
    sb_init(&doc_sb);

    /* 1. If we have AST Expr with def */
    if (id && id->def_kind == DEF_VAR && id->def) {
        Decl* d = (Decl*)id->def;
        char* tstr = format_ast_type(id->type ? id->type : d->type);
        sb_appendf(&doc_sb, "```rook\n%s: %s\n```\n*Local variable*", id->str, tstr);
        free(tstr);
    } else if (id && id->def_kind == DEF_FN && id->def) {
        FnDef* f = (FnDef*)id->def;
        char* sig = format_fn_sig(f);
        sb_appendf(&doc_sb, "```rook\n%s\n```", sig);
        free(sig);
        if (f->source_file && f->source_file[0]) {
            sb_appendf(&doc_sb, "\n*Defined in `%s`*", f->source_file);
        }
    } else if (id && id->def_kind == DEF_STRUCT && id->def) {
        StructDef* s = (StructDef*)id->def;
        sb_appendf(&doc_sb, "```rook\nstruct %s", s->name);
        if (s->parent) sb_appendf(&doc_sb, " : %s", s->parent);
        sb_append(&doc_sb, " {\n");
        for (int i = 0; i < s->nfields; i++) {
            char* ft = format_ast_type(s->fields[i].type);
            sb_appendf(&doc_sb, "    %s: %s;\n", s->fields[i].name, ft ? ft : "void");
            free(ft);
        }
        sb_append(&doc_sb, "}\n```");
        if (s->source_file && s->source_file[0]) {
            sb_appendf(&doc_sb, "\n*Defined in `%s`*", s->source_file);
        }
    } else if (id && id->def_kind == DEF_ENUM && id->def) {
        EnumDef* e = (EnumDef*)id->def;
        sb_appendf(&doc_sb, "```rook\nsum %s {\n", e->name);
        for (int i = 0; i < e->nvariants; i++) {
            sb_appendf(&doc_sb, "    %s,\n", e->variants[i].name);
        }
        sb_append(&doc_sb, "}\n```");
        if (e->source_file && e->source_file[0]) {
            sb_appendf(&doc_sb, "\n*Defined in `%s`*", e->source_file);
        }
    } else if (id && id->def_kind == DEF_VARIANT && id->def) {
        EnumVariant* v = (EnumVariant*)id->def;
        const char* owner = doc->sema ? sema_lookup_variant(doc->sema, v->name) : NULL;
        if (owner) {
            sb_appendf(&doc_sb, "```rook\n%s::%s\n```\n*Variant of `sum %s`*", owner, v->name, owner);
        } else {
            sb_appendf(&doc_sb, "```rook\nvariant %s\n```", v->name);
        }
    }
    /* 2. Primitive types, keywords, and language constructs */
    else if (format_builtin_hover(sym_name, &doc_sb) || format_builtin_hover(word, &doc_sb)) {
        /* Builtin hover successfully populated */
    }
    /* 3. Enum variant lookup (e.g. AI_STATE_DEAD) */
    else if (doc->sema && sema_lookup_variant(doc->sema, sym_name)) {
        const char* owner = sema_lookup_variant(doc->sema, sym_name);
        sb_appendf(&doc_sb, "```rook\n%s::%s\n```\n*Variant of `sum %s`*", owner, sym_name, owner);
    }
    /* 4. C function in sema */
    else if (sema_is_cfunc(sym_name)) {
        const char* ret = sema_cfunc_ret(sym_name);
        const char* h_file = NULL;
        int h_line = 0, h_col = 0;
        sema_lookup_cfunc_loc(sym_name, &h_file, &h_line, &h_col);
        int np = sema_cfunc_nparams(sym_name);
        int is_var = sema_cfunc_is_variadic(sym_name);
        sb_appendf(&doc_sb, "```rook\nextern %s %s(", ret ? ret : "void", sym_name);
        for (int i = 0; i < np; i++) {
            const char* pt = sema_lookup_cfunc_param(sym_name, i);
            if (i > 0) sb_append(&doc_sb, ", ");
            sb_append(&doc_sb, pt ? pt : "int");
        }
        if (is_var) {
            if (np > 0) sb_append(&doc_sb, ", ...");
            else sb_append(&doc_sb, "...");
        }
        sb_append(&doc_sb, ")\n```\n");
        if (h_file && h_file[0]) {
            sb_appendf(&doc_sb, "*C function from `%s`*", h_file);
        } else {
            sb_append(&doc_sb, "*C standard function*");
        }
    }
    /* 5. Struct in sema (Rook or C struct) */
    else if (doc->sema && sema_lookup_struct(doc->sema, sym_name)) {
        StructDef* st = sema_lookup_struct(doc->sema, sym_name);
        sb_appendf(&doc_sb, "```rook\nstruct %s", st->name);
        if (st->parent) sb_appendf(&doc_sb, " : %s", st->parent);
        sb_append(&doc_sb, " {\n");
        for (int i = 0; i < st->nfields; i++) {
            char* ft = format_ast_type(st->fields[i].type);
            sb_appendf(&doc_sb, "    %s: %s;\n", st->fields[i].name, ft ? ft : "int");
            free(ft);
        }
        sb_append(&doc_sb, "}\n```\n");
        if (st->source_file && st->source_file[0]) {
            sb_appendf(&doc_sb, "*Defined in `%s`*", st->source_file);
        }
    }
    /* 6. Enum in sema */
    else if (doc->sema && sema_lookup_enum(doc->sema, sym_name)) {
        EnumDef* ed = sema_lookup_enum(doc->sema, sym_name);
        sb_appendf(&doc_sb, "```rook\nsum %s {\n", ed->name);
        for (int i = 0; i < ed->nvariants; i++) {
            sb_appendf(&doc_sb, "    %s,\n", ed->variants[i].name);
        }
        sb_append(&doc_sb, "}\n```\n");
        if (ed->source_file && ed->source_file[0]) {
            sb_appendf(&doc_sb, "*Defined in `%s`*", ed->source_file);
        }
    }
    /* 7. Top-level AST function or method in doc->prog */
    else if (doc->prog) {
        int found_fn = 0;
        for (int i = 0; i < doc->prog->nitems; i++) {
            Item* it = doc->prog->items[i];
            if (it->kind == TOP_FN && it->fn && strcmp(it->fn->name, sym_name) == 0) {
                char* sig = format_fn_sig(it->fn);
                sb_appendf(&doc_sb, "```rook\n%s\n```\n", sig);
                free(sig);
                if (it->fn->source_file && it->fn->source_file[0]) {
                    sb_appendf(&doc_sb, "*Defined in `%s`*", it->fn->source_file);
                }
                found_fn = 1;
                break;
            } else if (it->kind == TOP_IMPL && it->im) {
                for (int m = 0; m < it->im->nmethods; m++) {
                    FnDef* fn = it->im->methods[m];
                    if (fn && strcmp(fn->name, sym_name) == 0) {
                        char* sig = format_fn_sig(fn);
                        const char* tgt = (it->im->target && it->im->target->name) ? it->im->target->name : "impl";
                        sb_appendf(&doc_sb, "```rook\n%s\n```\n*Method of `%s`*", sig, tgt);
                        free(sig);
                        if (fn->source_file && fn->source_file[0]) {
                            sb_appendf(&doc_sb, " in `%s`*", fn->source_file);
                        }
                        found_fn = 1;
                        break;
                    }
                }
                if (found_fn) break;
            } else if (it->kind == TOP_ENUM && it->ed) {
                for (int v = 0; v < it->ed->nvariants; v++) {
                    if (it->ed->variants[v].name && strcmp(it->ed->variants[v].name, sym_name) == 0) {
                        sb_appendf(&doc_sb, "```rook\n%s::%s\n```\n*Variant of `sum %s`*",
                                   it->ed->name, sym_name, it->ed->name);
                        found_fn = 1;
                        break;
                    }
                }
                if (found_fn) break;
            }
        }
        if (!found_fn) {
            if (hover_param_or_field(doc->prog, line + 1, sym_name, &doc_sb)) {
                /* Handled param or field */
            } else {
                const WorkspaceSymbol* ws = lsp_workspace_lookup(sym_name);
                if (ws) {
                    sb_appendf(&doc_sb, "```rook\n%s\n```\n", ws->signature[0] ? ws->signature : ws->name);
                    if (ws->doc[0]) {
                        sb_appendf(&doc_sb, "%s\n\n", ws->doc);
                    }
                    sb_appendf(&doc_sb, "*Defined in `%s`*", ws->file_path);
                } else if (id && id->type) {
                    char* tstr = format_ast_type(id->type);
                    sb_appendf(&doc_sb, "```rook\n%s: %s\n```", id->str, tstr);
                    free(tstr);
                } else {
                    sb_appendf(&doc_sb, "```rook\n%s\n```", sym_name);
                }
            }
        }
    } else {
        const WorkspaceSymbol* ws = lsp_workspace_lookup(sym_name);
        if (ws) {
            sb_appendf(&doc_sb, "```rook\n%s\n```\n", ws->signature[0] ? ws->signature : ws->name);
            if (ws->doc[0]) {
                sb_appendf(&doc_sb, "%s\n\n", ws->doc);
            }
            sb_appendf(&doc_sb, "*Defined in `%s`*", ws->file_path);
        } else {
            sb_appendf(&doc_sb, "```rook\n%s\n```", sym_name);
        }
    }

    char* content = sb_strdup(&doc_sb);
    sb_free(&doc_sb);

    sb_append(res, "{\"contents\":{\"kind\":\"markdown\",\"value\":");
    json_emit_escaped_str(res, content);
    sb_append(res, "}}");
    free(content);
}

/* ─── Goto Definition ───────────────────────────────────────────────── */

void lsp_handle_definition(LspDoc* doc, int line, int character, SB* res) {
    if (!doc) {
        sb_append(res, "null");
        return;
    }

    char doc_dir[4096] = ".";
    char project_root[4096] = ".";
    if (doc->path) {
        const char* root = lsp_find_project_root(doc->path);
        if (root && strcmp(root, ".") != 0) snprintf(project_root, sizeof(project_root), "%s", root);
        const char* slash = strrchr(doc->path, '/');
#ifdef _WIN32
        const char* bslash = strrchr(doc->path, '\\');
        if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
        if (slash && slash > doc->path) {
            size_t dlen = (size_t)(slash - doc->path);
            if (dlen < sizeof(doc_dir)) {
                memcpy(doc_dir, doc->path, dlen);
                doc_dir[dlen] = '\0';
            }
        }
        if (strcmp(project_root, ".") == 0) snprintf(project_root, sizeof(project_root), "%s", doc_dir);
    }
    const char* basedir = doc_dir;

    char std_path[4096];
    const char* inc_list[32];
    size_t n_inc = 0;
    if (rokade_get_std_dir(std_path, sizeof(std_path)) == 0) {
        inc_list[n_inc++] = std_path;
    }
    char root_src[4096];
    char root_vendor[4096];
    if (project_root[0] && strcmp(project_root, ".") != 0) {
        inc_list[n_inc++] = project_root;
        snprintf(root_src, sizeof(root_src), "%s/src", project_root);
        if (access(root_src, R_OK) == 0) inc_list[n_inc++] = root_src;
        snprintf(root_vendor, sizeof(root_vendor), "%s/vendor", project_root);
        if (access(root_vendor, R_OK) == 0) inc_list[n_inc++] = root_vendor;
    }

    /* 1. Check if cursor is on a #comprise, comprise, or #include line */
    int line_offset = lsp_pos_to_offset(doc->text, doc->len, line, 0);
    const char* line_start = doc->text + line_offset;
    const char* line_end = line_start;
    while (line_end < doc->text + doc->len && *line_end != '\n' && *line_end != '\r') line_end++;
    int cur_line_len = (int)(line_end - line_start);
    if (cur_line_len > 0) {
        char line_buf[1024];
        int copy_len = cur_line_len < (int)sizeof(line_buf) - 1 ? cur_line_len : (int)sizeof(line_buf) - 1;
        memcpy(line_buf, line_start, copy_len);
        line_buf[copy_len] = '\0';

        const char* s = line_buf;
        while (*s == ' ' || *s == '\t') s++;

        int is_comprise = 0;
        int is_include = 0;
        const char* after = NULL;

        if (s[0] == '#' && strncmp(s + 1, "comprise", 8) == 0) {
            after = s + 9;
            is_comprise = 1;
        } else if (strncmp(s, "comprise", 8) == 0 && (s[8] == ' ' || s[8] == '\t')) {
            after = s + 8;
            is_comprise = 1;
        } else if (s[0] == '#' && strncmp(s + 1, "include", 7) == 0) {
            after = s + 8;
            is_include = 1;
        } else if (strncmp(s, "include", 7) == 0 && (s[7] == ' ' || s[7] == '\t')) {
            after = s + 7;
            is_include = 1;
        }

        if ((is_comprise || is_include) && after) {
            while (*after == ' ' || *after == '\t') after++;
            char quote = 0;
            if (*after == '<' || *after == '"') { quote = *after; after++; }
            const char* id0 = after;
            while (*after && *after != '\r' && *after != '\n' && *after != ';') {
                if (quote == '<' && *after == '>') break;
                if (quote == '"' && *after == '"') break;
                if (!quote && (*after == ' ' || *after == '\t')) break;
                after++;
            }
            size_t idlen = (size_t)(after - id0);
            char target_name[512] = "";
            if (idlen > 0 && idlen < sizeof(target_name)) {
                memcpy(target_name, id0, idlen);
                target_name[idlen] = '\0';
            }
            if (target_name[0]) {
                char* resolved = NULL;
                if (is_comprise) {
                    resolved = resolve_include_path(target_name, basedir, inc_list, n_inc);
                    if (!resolved) resolved = resolve_c_header_path(target_name, basedir, inc_list, n_inc);
                } else {
                    resolved = resolve_c_header_path(target_name, basedir, inc_list, n_inc);
                    if (!resolved) resolved = resolve_include_path(target_name, basedir, inc_list, n_inc);
                }
                if (resolved) {
                    char target_uri[4096];
                    lsp_path_to_uri(resolved, target_uri, sizeof(target_uri));
                    free(resolved);
                    sb_append(res, "{\"uri\":");
                    json_emit_escaped_str(res, target_uri);
                    sb_append(res, ",\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":0}}}");
                    return;
                }
            }
        }
    }

    /* 2. Check AST identifier at cursor */
    Expr* id = doc->prog ? find_ident_at(doc->prog, line + 1, character + 1) : NULL;
    if (id && id->def_kind != DEF_NONE && id->def) {
        int dl = 0, dc = 0;
        const char* target_file = NULL;
        if (def_location(id->def_kind, id->def, &dl, &dc, &target_file) == 0) {
            int target_line = dl > 0 ? dl - 1 : 0;
            int target_col = dc > 0 ? dc - 1 : 0;

            char target_uri[4096];
            if (target_file && target_file[0]) {
                lsp_path_to_uri(target_file, target_uri, sizeof(target_uri));
            } else {
                snprintf(target_uri, sizeof(target_uri), "%s", doc->uri);
            }

            sb_append(res, "{\"uri\":");
            json_emit_escaped_str(res, target_uri);
            sb_appendf(res, ",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}",
                       target_line, target_col, target_line, target_col);
            return;
        }
    }

    /* 3. Symbol lookup (from AST expr or extracted word at cursor) */
    char word[128] = "";
    get_word_at_pos(doc->text, doc->len, line, character, word, sizeof(word));
    const char* sym_name = (id && id->str) ? id->str : word;
    if (sym_name && sym_name[0]) {
        /* A. Check C function location */
        const char* c_header = NULL;
        int c_line = 0, c_col = 0;
        if (sema_lookup_cfunc_loc(sym_name, &c_header, &c_line, &c_col) && c_header && c_header[0]) {
            char target_uri[4096];
            lsp_path_to_uri(c_header, target_uri, sizeof(target_uri));
            int tline = c_line > 0 ? c_line - 1 : 0;
            int tcol = c_col > 0 ? c_col - 1 : 0;
            sb_append(res, "{\"uri\":");
            json_emit_escaped_str(res, target_uri);
            sb_appendf(res, ",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}",
                       tline, tcol, tline, tcol);
            return;
        }

        /* B. Fallback libc header for known libc symbols */
        const char* libc_header = lookup_libc_header_for_symbol(sym_name);
        if (libc_header) {
            char* resolved = resolve_c_header_path(libc_header, basedir, inc_list, n_inc);
            if (resolved) {
                char target_uri[4096];
                lsp_path_to_uri(resolved, target_uri, sizeof(target_uri));
                free(resolved);
                sb_append(res, "{\"uri\":");
                json_emit_escaped_str(res, target_uri);
                sb_append(res, ",\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":0}}}");
                return;
            }
        }

        /* C. Check Struct in sema */
        if (doc->sema) {
            StructDef* st = sema_lookup_struct(doc->sema, sym_name);
            if (st && st->source_file && st->source_file[0]) {
                char target_uri[4096];
                lsp_path_to_uri(st->source_file, target_uri, sizeof(target_uri));
                int tline = st->line > 0 ? st->line - 1 : 0;
                int tcol = st->col > 0 ? st->col - 1 : 0;
                sb_append(res, "{\"uri\":");
                json_emit_escaped_str(res, target_uri);
                sb_appendf(res, ",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}",
                           tline, tcol, tline, tcol);
                return;
            }

            /* D. Check Enum in sema */
            EnumDef* ed = sema_lookup_enum(doc->sema, sym_name);
            if (ed && ed->source_file && ed->source_file[0]) {
                char target_uri[4096];
                lsp_path_to_uri(ed->source_file, target_uri, sizeof(target_uri));
                int tline = ed->line > 0 ? ed->line - 1 : 0;
                int tcol = ed->col > 0 ? ed->col - 1 : 0;
                sb_append(res, "{\"uri\":");
                json_emit_escaped_str(res, target_uri);
                sb_appendf(res, ",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}",
                           tline, tcol, tline, tcol);
                return;
            }

            /* E. Check Symbol in sema scope */
            Sym* sym = sema_lookup(doc->sema, sym_name);
            if (sym && sym->source_file && sym->source_file[0]) {
                char target_uri[4096];
                lsp_path_to_uri(sym->source_file, target_uri, sizeof(target_uri));
                int tline = sym->line > 0 ? sym->line - 1 : 0;
                int tcol = sym->col > 0 ? sym->col - 1 : 0;
                sb_append(res, "{\"uri\":");
                json_emit_escaped_str(res, target_uri);
                sb_appendf(res, ",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}",
                           tline, tcol, tline, tcol);
                return;
            }
        }

        /* F. AST top-level symbol lookup (including comprised stdlib items) */
        if (doc->prog) {
            for (int i = 0; i < doc->prog->nitems; i++) {
                Item* it = doc->prog->items[i];
                if (it->kind == TOP_FN && it->fn && strcmp(it->fn->name, sym_name) == 0) {
                    int tl = it->fn->line > 0 ? it->fn->line - 1 : 0;
                    int tc = it->fn->col > 0 ? it->fn->col - 1 : 0;
                    char target_uri[4096];
                    if (it->fn->source_file && it->fn->source_file[0])
                        lsp_path_to_uri(it->fn->source_file, target_uri, sizeof(target_uri));
                    else
                        snprintf(target_uri, sizeof(target_uri), "%s", doc->uri);
                    sb_append(res, "{\"uri\":");
                    json_emit_escaped_str(res, target_uri);
                    sb_appendf(res, ",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}",
                               tl, tc, tl, tc);
                    return;
                }
                if (it->kind == TOP_STRUCT && it->st && strcmp(it->st->name, sym_name) == 0) {
                    int tl = it->st->line > 0 ? it->st->line - 1 : 0;
                    int tc = it->st->col > 0 ? it->st->col - 1 : 0;
                    char target_uri[4096];
                    if (it->st->source_file && it->st->source_file[0])
                        lsp_path_to_uri(it->st->source_file, target_uri, sizeof(target_uri));
                    else
                        snprintf(target_uri, sizeof(target_uri), "%s", doc->uri);
                    sb_append(res, "{\"uri\":");
                    json_emit_escaped_str(res, target_uri);
                    sb_appendf(res, ",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}",
                               tl, tc, tl, tc);
                    return;
                }
                if (it->kind == TOP_ENUM && it->ed && strcmp(it->ed->name, sym_name) == 0) {
                    int tl = it->ed->line > 0 ? it->ed->line - 1 : 0;
                    int tc = it->ed->col > 0 ? it->ed->col - 1 : 0;
                    char target_uri[4096];
                    if (it->ed->source_file && it->ed->source_file[0])
                        lsp_path_to_uri(it->ed->source_file, target_uri, sizeof(target_uri));
                    else
                        snprintf(target_uri, sizeof(target_uri), "%s", doc->uri);
                    sb_append(res, "{\"uri\":");
                    json_emit_escaped_str(res, target_uri);
                    sb_appendf(res, ",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}",
                               tl, tc, tl, tc);
                    return;
                }
            }
        }

        /* G. Global Workspace symbol lookup (any file in the workspace or stdlib) */
        int ws_line = 0, ws_col = 0;
        char ws_uri[4096];
        if (lsp_workspace_find_definition(sym_name, ws_uri, sizeof(ws_uri), &ws_line, &ws_col)) {
            sb_append(res, "{\"uri\":");
            json_emit_escaped_str(res, ws_uri);
            sb_appendf(res, ",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}",
                       ws_line, ws_col, ws_line, ws_col);
            return;
        }
    }

    sb_append(res, "null");
}

/* ─── Signature Help ────────────────────────────────────────────────── */

void lsp_handle_signature_help(LspDoc* doc, int line, int character, SB* res) {
    int prefix_len = 0;
    const char* prefix = lsp_get_line_prefix(doc->text, doc->len, line, character, &prefix_len);
    if (prefix_len <= 0) {
        sb_append(res, "null");
        return;
    }

    /* Find opening paren */
    int paren_idx = -1;
    int comma_count = 0;
    int depth = 0;
    for (int i = prefix_len - 1; i >= 0; i--) {
        char c = prefix[i];
        if (c == ')') depth++;
        else if (c == '(') {
            if (depth > 0) depth--;
            else { paren_idx = i; break; }
        } else if (c == ',' && depth == 0) {
            comma_count++;
        }
    }

    if (paren_idx <= 0) {
        sb_append(res, "null");
        return;
    }

    /* Extract callee identifier */
    int fn_end = paren_idx;
    while (fn_end > 0 && (prefix[fn_end - 1] == ' ' || prefix[fn_end - 1] == '\t')) fn_end--;
    int fn_start = fn_end;
    while (fn_start > 0 && (isalnum((unsigned char)prefix[fn_start - 1]) || prefix[fn_start - 1] == '_')) fn_start--;
    int fn_len = fn_end - fn_start;
    if (fn_len <= 0) {
        sb_append(res, "null");
        return;
    }

    char fn_name[128];
    if (fn_len >= (int)sizeof(fn_name)) fn_len = sizeof(fn_name) - 1;
    memcpy(fn_name, prefix + fn_start, fn_len);
    fn_name[fn_len] = '\0';

    FnDef* target_fn = NULL;
    if (doc->prog) {
        for (int i = 0; i < doc->prog->nitems; i++) {
            Item* it = doc->prog->items[i];
            if (it->kind == TOP_FN && it->fn && strcmp(it->fn->name, fn_name) == 0) {
                target_fn = it->fn;
                break;
            } else if (it->kind == TOP_IMPL && it->im) {
                for (int m = 0; m < it->im->nmethods; m++) {
                    if (strcmp(it->im->methods[m]->name, fn_name) == 0) {
                        target_fn = it->im->methods[m];
                        break;
                    }
                }
                if (target_fn) break;
            }
        }
    }

    if (!target_fn) {
        sb_append(res, "null");
        return;
    }

    char* sig = format_fn_sig(target_fn);
    sb_append(res, "{\"signatures\":[{\"label\":");
    json_emit_escaped_str(res, sig);
    free(sig);
    sb_append(res, ",\"parameters\":[");
    for (int i = 0; i < target_fn->nparams; i++) {
        if (i > 0) sb_append(res, ",");
        char* pt = format_ast_type(target_fn->params[i].type);
        char pbuf[256];
        if (target_fn->params[i].name) {
            snprintf(pbuf, sizeof(pbuf), "%s %s", pt, target_fn->params[i].name);
        } else {
            snprintf(pbuf, sizeof(pbuf), "%s", pt);
        }
        free(pt);
        sb_append(res, "{\"label\":");
        json_emit_escaped_str(res, pbuf);
        sb_append(res, "}");
    }
    sb_appendf(res, "]}],\"activeSignature\":0,\"activeParameter\":%d}", comma_count);
}

/* ─── Document Symbols ──────────────────────────────────────────────── */

void lsp_handle_document_symbols(LspDoc* doc, SB* res) {
    if (!doc || !doc->prog) {
        sb_append(res, "[]");
        return;
    }

    sb_append(res, "[");
    int count = 0;
    int limit = doc->local_items_count > 0 ? doc->local_items_count : doc->prog->nitems;
    for (int i = 0; i < limit && i < doc->prog->nitems; i++) {
        Item* it = doc->prog->items[i];
        const char* name = NULL;
        int kind = 0;
        int line = 0, col = 0;
        switch (it->kind) {
        case TOP_FN:
            if (!it->fn || it->fn->mod_prefix) continue;
            name = it->fn->name; kind = 12; /* Function */
            line = it->fn->line; col = it->fn->col;
            break;
        case TOP_STRUCT:
            if (!it->st || it->st->mod_prefix) continue;
            name = it->st->name; kind = 23; /* Struct */
            line = it->st->line; col = it->st->col;
            break;
        case TOP_ENUM:
            if (!it->ed || it->ed->mod_prefix) continue;
            name = it->ed->name; kind = 10; /* Enum */
            line = it->ed->line; col = it->ed->col;
            break;
        case TOP_IMPL:
            if (it->im) {
                for (int m = 0; m < it->im->nmethods; m++) {
                    FnDef* fn = it->im->methods[m];
                    if (!fn || fn->mod_prefix) continue;
                    if (count > 0) sb_append(res, ",");
                    int l = fn->line > 0 ? fn->line - 1 : 0;
                    int c = fn->col > 0 ? fn->col - 1 : 0;
                    sb_append(res, "{\"name\":");
                    json_emit_escaped_str(res, fn->name);
                    sb_appendf(res, ",\"kind\":6,\"location\":{\"uri\":");
                    json_emit_escaped_str(res, doc->uri);
                    sb_appendf(res, ",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}}",
                               l, c, l, c);
                    count++;
                }
            }
            continue;
        default: continue;
        }

        if (count > 0) sb_append(res, ",");
        int l = line > 0 ? line - 1 : 0;
        int c = col > 0 ? col - 1 : 0;
        sb_append(res, "{\"name\":");
        json_emit_escaped_str(res, name);
        sb_appendf(res, ",\"kind\":%d,\"location\":{\"uri\":", kind);
        json_emit_escaped_str(res, doc->uri);
        sb_appendf(res, ",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}}",
                   l, c, l, c);
        count++;
    }
    sb_append(res, "]");
}

/* ─── Formatting ────────────────────────────────────────────────────── */

void lsp_handle_formatting(LspDoc* doc, SB* res) {
    if (!doc || !doc->prog) {
        sb_append(res, "[]");
        return;
    }

    int orig_nitems = doc->prog->nitems;
    if (doc->local_items_count > 0 && doc->local_items_count < orig_nitems) {
        doc->prog->nitems = doc->local_items_count;
    }

    int elen = 0;
    char* formatted = emit_program(doc->prog, &elen);
    doc->prog->nitems = orig_nitems;

    if (!formatted) {
        sb_append(res, "[]");
        return;
    }

    /* Replace full document */
    sb_append(res, "[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":999999,\"character\":0}},\"newText\":");
    json_emit_escaped_str(res, formatted);
    sb_append(res, "}]");
    free(formatted);
}

/* ─── Semantic Tokens ───────────────────────────────────────────────── */
/* Legend types:
   0: type, 1: function, 2: variable, 3: parameter, 4: keyword,
   5: comment, 6: string, 7: number, 8: operator, 9: enumMember */

static int is_operator_punct(const char* s, int len) {
    if (len == 1) {
        char c = s[0];
        return c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
               c == '=' || c == '<' || c == '>' || c == '!' || c == '&' ||
               c == '|' || c == '^' || c == '~';
    }
    if (len == 2) {
        return (s[0] == '=' && s[1] == '=') ||
               (s[0] == '!' && s[1] == '=') ||
               (s[0] == '<' && s[1] == '=') ||
               (s[0] == '>' && s[1] == '=') ||
               (s[0] == '&' && s[1] == '&') ||
               (s[0] == '|' && s[1] == '|') ||
               (s[0] == '+' && s[1] == '=') ||
               (s[0] == '-' && s[1] == '=') ||
               (s[0] == '*' && s[1] == '=') ||
               (s[0] == '/' && s[1] == '=') ||
               (s[0] == '<' && s[1] == '<') ||
               (s[0] == '>' && s[1] == '>') ||
               (s[0] == '-' && s[1] == '>') ||
               (s[0] == '=' && s[1] == '>');
    }
    return 0;
}

static int is_rook_keyword(const char* s, int len) {
    static const char* kws[] = {
        "struct", "impl", "sum", "enum", "extern", "auto", "const",
        "return", "if", "else", "while", "for", "in", "switch", "case",
        "default", "match", "defer", "break", "continue", "comprise", "as",
        "true", "false", "null", NULL
    };
    for (int i = 0; kws[i]; i++) {
        if ((int)strlen(kws[i]) == len && memcmp(s, kws[i], len) == 0) return 1;
    }
    return 0;
}

static int is_builtin_type(const char* s, int len) {
    static const char* tys[] = {
        "void", "bool", "int", "int8", "int16", "int32", "int64",
        "uint8", "uint16", "uint32", "uint64", "float", "double",
        "char", "size_t", "ssize_t", "uintptr_t", "intptr_t",
        "Str", "Vec", "Result", "Option", "StringBuilder", NULL
    };
    for (int i = 0; tys[i]; i++) {
        if ((int)strlen(tys[i]) == len && memcmp(s, tys[i], len) == 0) return 1;
    }
    return 0;
}

void lsp_handle_semantic_tokens(LspDoc* doc, SB* res) {
    sb_append(res, "{\"data\":[");
    if (!doc || !doc->toks || doc->ntoks <= 0) {
        sb_append(res, "]}");
        return;
    }

    int prev_line = 0;
    int prev_col = 0;
    int first = 1;

    for (int i = 0; i < doc->ntoks; i++) {
        Token* t = &doc->toks[i];
        if (t->kind == TK_EOF) break;

        int tok_type = -1;
        int tok_mod = 0;

        if (t->kind == TK_NUMBER) {
            tok_type = 7; /* number */
        } else if (t->kind == TK_STRING || t->kind == TK_CHAR) {
            tok_type = 6; /* string */
        } else if (t->kind == TK_PUNCT) {
            if (is_operator_punct(t->text, t->len)) {
                tok_type = 8; /* operator */
            }
        } else if (t->kind == TK_IDENT) {
            if (is_rook_keyword(t->text, t->len)) {
                tok_type = 4; /* keyword */
            } else if (is_builtin_type(t->text, t->len)) {
                tok_type = 0; /* type */
            } else {
                char id_str[128];
                int id_len = t->len < (int)sizeof(id_str) ? t->len : (int)sizeof(id_str) - 1;
                memcpy(id_str, t->text, id_len);
                id_str[id_len] = '\0';

                if (doc->sema && (sema_lookup_struct(doc->sema, id_str) || sema_lookup_enum(doc->sema, id_str))) {
                    tok_type = 0; /* type */
                } else if (i > 0 && doc->toks[i - 1].kind == TK_PUNCT && doc->toks[i - 1].len == 1 && doc->toks[i - 1].text[0] == '.') {
                    /* Member: could be enum variant, struct field, or method */
                    if (i + 1 < doc->ntoks && doc->toks[i + 1].kind == TK_PUNCT && doc->toks[i + 1].len == 1 && doc->toks[i + 1].text[0] == '(') {
                        tok_type = 1; /* function/method */
                    } else {
                        tok_type = 9; /* enumMember / property */
                    }
                } else if (i + 1 < doc->ntoks && doc->toks[i + 1].kind == TK_PUNCT && doc->toks[i + 1].len == 1 && doc->toks[i + 1].text[0] == '(') {
                    tok_type = 1; /* function */
                } else {
                    tok_type = 2; /* variable */
                }
            }
        }

        if (tok_type >= 0) {
            int cur_line = t->line > 0 ? t->line - 1 : 0;
            int cur_col = t->col > 0 ? t->col - 1 : 0;

            int delta_line = cur_line - prev_line;
            int delta_col = (delta_line == 0) ? (cur_col - prev_col) : cur_col;

            if (!first) sb_append(res, ",");
            sb_appendf(res, "%d,%d,%d,%d,%d", delta_line, delta_col, t->len, tok_type, tok_mod);
            first = 0;

            prev_line = cur_line;
            prev_col = cur_col;
        }
    }

    sb_append(res, "]}");
}
