#include "lsp_workspace.h"
#include "lsp_json.h"
#include "../parse.h"
#include "../lexer.h"
#include "../ast.h"
#include "../util.h"
#include "../resolve.h"
#include "../sema.h"
#include "../rk_dirent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static WorkspaceSymbol* g_ws_syms = NULL;
static size_t g_ws_count = 0;
static size_t g_ws_cap = 0;
static char g_indexed_root[4096] = "";
static int g_workspace_initialized = 0;

int lsp_workspace_is_initialized(void) {
    return g_workspace_initialized && g_ws_count > 0;
}

void lsp_workspace_populate_raw_names(void) {
    for (size_t i = 0; i < g_ws_count; i++) {
        sema_add_raw_name(g_ws_syms[i].name);
    }
}

static void add_ws_sym(const WorkspaceSymbol* sym) {
    if (!sym || !sym->name[0]) return;
    if (g_ws_count >= g_ws_cap) {
        size_t new_cap = g_ws_cap ? g_ws_cap * 2 : 256;
        WorkspaceSymbol* new_syms = realloc(g_ws_syms, new_cap * sizeof(WorkspaceSymbol));
        if (!new_syms) return;
        g_ws_syms = new_syms;
        g_ws_cap = new_cap;
    }
    g_ws_syms[g_ws_count++] = *sym;
}

void lsp_workspace_free_all(void) {
    if (g_ws_syms) {
        free(g_ws_syms);
        g_ws_syms = NULL;
    }
    g_ws_count = 0;
    g_ws_cap = 0;
    g_workspace_initialized = 0;
    g_indexed_root[0] = '\0';
}

void lsp_workspace_remove_file(const char* file_path) {
    if (!file_path || !file_path[0] || !g_ws_syms) return;
    char real_target[4096];
    const char* norm_target = rk_realpath(file_path, real_target) ? real_target : file_path;

    size_t write_idx = 0;
    for (size_t i = 0; i < g_ws_count; i++) {
        char real_item[4096];
        const char* norm_item = rk_realpath(g_ws_syms[i].file_path, real_item) ? real_item : g_ws_syms[i].file_path;
        if (strcmp(norm_item, norm_target) != 0) {
            if (write_idx != i) {
                g_ws_syms[write_idx] = g_ws_syms[i];
            }
            write_idx++;
        }
    }
    g_ws_count = write_idx;
}

static char* ws_format_ast_type(AstType* t) {
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

static char* ws_format_fn_sig(FnDef* fn) {
    if (!fn) return strdup("");
    SB sb;
    sb_init(&sb);
    char* r = ws_format_ast_type(fn->ret);
    sb_appendf(&sb, "%s %s(", r, fn->name);
    free(r);
    for (int i = 0; i < fn->nparams; i++) {
        if (i > 0) sb_append(&sb, ", ");
        char* pt = ws_format_ast_type(fn->params[i].type);
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

void lsp_workspace_index_file(const char* file_path, const char* content, int content_len) {
    if (!file_path || !file_path[0]) return;

    /* Remove previous symbols for this file */
    lsp_workspace_remove_file(file_path);

    char* allocated_content = NULL;
    if (!content) {
        allocated_content = util_read_file(file_path, &content_len);
        content = allocated_content;
    }
    if (!content || content_len <= 0) {
        if (allocated_content) free(allocated_content);
        return;
    }

    int ntoks = 0;
    Token* toks = lex_all(content, content_len, &ntoks);
    Program* prog = parse_program_tolerant(content, content_len, toks, ntoks);
    if (!prog) {
        if (toks) free(toks);
        if (allocated_content) free(allocated_content);
        return;
    }

    char file_uri[4096];
    lsp_path_to_uri(file_path, file_uri, sizeof(file_uri));

    /* Determine if standard library */
    int is_std = 0;
    char std_dir[4096];
    if (rokade_get_std_dir(std_dir, sizeof(std_dir)) == 0) {
        char real_std[4096], real_file[4096];
        if (rk_realpath(std_dir, real_std) && rk_realpath(file_path, real_file)) {
            if (strncmp(real_file, real_std, strlen(real_std)) == 0) {
                is_std = 1;
            }
        }
    }

    /* Compute module name */
    char mod_name[128] = "";
    if (is_std) {
        const char* base = strrchr(file_path, '/');
#ifdef _WIN32
        const char* bbase = strrchr(file_path, '\\');
        if (!base || (bbase && bbase > base)) base = bbase;
#endif
        base = base ? base + 1 : file_path;
        char stem[128];
        snprintf(stem, sizeof(stem), "%s", base);
        char* dot = strrchr(stem, '.');
        if (dot) *dot = '\0';
        snprintf(mod_name, sizeof(mod_name), "std/%s", stem);
    } else if (g_indexed_root[0]) {
        /* Compute path relative to workspace root */
        char real_root[4096], real_fp[4096];
        if (rk_realpath(g_indexed_root, real_root) && rk_realpath(file_path, real_fp)) {
            size_t rlen = strlen(real_root);
            if (strncmp(real_fp, real_root, rlen) == 0) {
                const char* rel = real_fp + rlen;
                if (*rel == '/' || *rel == '\\') rel++;
                snprintf(mod_name, sizeof(mod_name), "%s", rel);
                char* dot = strrchr(mod_name, '.');
                if (dot) *dot = '\0';
            }
        }
    }
    if (!mod_name[0]) {
        const char* base = strrchr(file_path, '/');
#ifdef _WIN32
        const char* bbase = strrchr(file_path, '\\');
        if (!base || (bbase && bbase > base)) base = bbase;
#endif
        base = base ? base + 1 : file_path;
        snprintf(mod_name, sizeof(mod_name), "%s", base);
        char* dot = strrchr(mod_name, '.');
        if (dot) *dot = '\0';
    }

    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (!it || it->kind == TOP_RAW) continue;

        WorkspaceSymbol sym;
        memset(&sym, 0, sizeof(sym));
        snprintf(sym.file_path, sizeof(sym.file_path), "%s", file_path);
        snprintf(sym.uri, sizeof(sym.uri), "%s", file_uri);
        snprintf(sym.mod_name, sizeof(sym.mod_name), "%s", mod_name);
        sym.is_std = is_std;

        if (it->kind == TOP_FN && it->fn) {
            snprintf(sym.name, sizeof(sym.name), "%s", it->fn->name);
            sym.kind = 3; /* Function */
            sym.line = it->fn->line > 0 ? it->fn->line - 1 : 0;
            sym.character = it->fn->col > 0 ? it->fn->col - 1 : 0;

            char* sig = ws_format_fn_sig(it->fn);
            snprintf(sym.signature, sizeof(sym.signature), "%s", sig);
            free(sig);

            char* ret = ws_format_ast_type(it->fn->ret);
            snprintf(sym.type_or_ret, sizeof(sym.type_or_ret), "%s", ret);
            free(ret);

            snprintf(sym.doc, sizeof(sym.doc), "%s (from %s)", sym.signature, mod_name);
            add_ws_sym(&sym);
        } else if (it->kind == TOP_STRUCT && it->st) {
            snprintf(sym.name, sizeof(sym.name), "%s", it->st->name);
            sym.kind = 22; /* Struct */
            sym.line = it->st->line > 0 ? it->st->line - 1 : 0;
            sym.character = it->st->col > 0 ? it->st->col - 1 : 0;
            snprintf(sym.signature, sizeof(sym.signature), "struct %s", it->st->name);
            snprintf(sym.doc, sizeof(sym.doc), "Struct %s with %d fields (from %s)", it->st->name, it->st->nfields, mod_name);
            add_ws_sym(&sym);
        } else if (it->kind == TOP_ENUM && it->ed) {
            snprintf(sym.name, sizeof(sym.name), "%s", it->ed->name);
            sym.kind = 13; /* Enum */
            sym.line = it->ed->line > 0 ? it->ed->line - 1 : 0;
            sym.character = it->ed->col > 0 ? it->ed->col - 1 : 0;
            snprintf(sym.signature, sizeof(sym.signature), "sum %s", it->ed->name);
            snprintf(sym.doc, sizeof(sym.doc), "Sum type %s with %d variants (from %s)", it->ed->name, it->ed->nvariants, mod_name);
            add_ws_sym(&sym);

            /* Index variants */
            for (int v = 0; v < it->ed->nvariants; v++) {
                EnumVariant* ev = &it->ed->variants[v];
                WorkspaceSymbol vsym;
                memset(&vsym, 0, sizeof(vsym));
                snprintf(vsym.name, sizeof(vsym.name), "%s", ev->name);
                vsym.kind = 20; /* EnumMember */
                snprintf(vsym.file_path, sizeof(vsym.file_path), "%s", file_path);
                snprintf(vsym.uri, sizeof(vsym.uri), "%s", file_uri);
                snprintf(vsym.mod_name, sizeof(vsym.mod_name), "%s", mod_name);
                vsym.is_std = is_std;
                vsym.line = ev->line > 0 ? ev->line - 1 : sym.line;
                vsym.character = ev->col > 0 ? ev->col - 1 : 0;
                snprintf(vsym.signature, sizeof(vsym.signature), "%s::%s", it->ed->name, ev->name);
                snprintf(vsym.doc, sizeof(vsym.doc), "Variant of %s (from %s)", it->ed->name, mod_name);
                add_ws_sym(&vsym);
            }
        } else if (it->kind == TOP_IMPL && it->im) {
            for (int m = 0; m < it->im->nmethods; m++) {
                FnDef* fn = it->im->methods[m];
                WorkspaceSymbol msym;
                memset(&msym, 0, sizeof(msym));
                snprintf(msym.name, sizeof(msym.name), "%s", fn->name);
                msym.kind = 3; /* Function / Method */
                snprintf(msym.file_path, sizeof(msym.file_path), "%s", file_path);
                snprintf(msym.uri, sizeof(msym.uri), "%s", file_uri);
                snprintf(msym.mod_name, sizeof(msym.mod_name), "%s", mod_name);
                msym.is_std = is_std;
                msym.line = fn->line > 0 ? fn->line - 1 : 0;
                msym.character = fn->col > 0 ? fn->col - 1 : 0;

                const char* tgt_name = (it->im->target && it->im->target->name) ? it->im->target->name : "impl";
                char* sig = ws_format_fn_sig(fn);
                snprintf(msym.signature, sizeof(msym.signature), "%s::%s", tgt_name, sig);
                free(sig);

                char* ret = ws_format_ast_type(fn->ret);
                strncpy(msym.type_or_ret, ret, sizeof(msym.type_or_ret) - 1);
                free(ret);

                snprintf(msym.doc, sizeof(msym.doc), "Method on %s (from %s)", tgt_name, mod_name);
                add_ws_sym(&msym);
            }
        }
    }

    /* Index #define macros from file content */
    const char* ptr = content;
    const char* content_end = content + content_len;
    int line_num = 0;
    while (ptr < content_end) {
        const char* line_start = ptr;
        while (ptr < content_end && *ptr != '\n') ptr++;
        const char* line_end = ptr;
        if (ptr < content_end && *ptr == '\n') ptr++;

        const char* cur = line_start;
        while (cur < line_end && (*cur == ' ' || *cur == '\t' || *cur == '\r')) cur++;
        if (cur + 7 <= line_end && strncmp(cur, "#define", 7) == 0 && (cur[7] == ' ' || cur[7] == '\t')) {
            cur += 7;
            while (cur < line_end && (*cur == ' ' || *cur == '\t')) cur++;
            const char* id_start = cur;
            while (cur < line_end && (isalnum((unsigned char)*cur) || *cur == '_')) cur++;
            int id_len = (int)(cur - id_start);
            if (id_len > 0 && id_len < 128) {
                char macro_name[128];
                memcpy(macro_name, id_start, id_len);
                macro_name[id_len] = '\0';

                while (cur < line_end && (*cur == ' ' || *cur == '\t')) cur++;
                char sig[256];
                int val_len = (int)(line_end - cur);
                while (val_len > 0 && (cur[val_len - 1] == '\r' || cur[val_len - 1] == ' ' || cur[val_len - 1] == '\t')) val_len--;
                if (val_len > 0) {
                    snprintf(sig, sizeof(sig), "#define %s %.*s", macro_name, (int)(val_len > 180 ? 180 : val_len), cur);
                } else {
                    snprintf(sig, sizeof(sig), "#define %s", macro_name);
                }

                WorkspaceSymbol msym;
                memset(&msym, 0, sizeof(msym));
                snprintf(msym.name, sizeof(msym.name), "%s", macro_name);
                msym.kind = 14; /* Constant */
                snprintf(msym.file_path, sizeof(msym.file_path), "%s", file_path);
                snprintf(msym.uri, sizeof(msym.uri), "%s", file_uri);
                snprintf(msym.mod_name, sizeof(msym.mod_name), "%s", mod_name);
                msym.is_std = is_std;
                msym.line = line_num;
                msym.character = (int)(id_start - line_start);
                snprintf(msym.signature, sizeof(msym.signature), "%s", sig);
                snprintf(msym.doc, sizeof(msym.doc), "Macro %s (from %s)", macro_name, mod_name);
                add_ws_sym(&msym);
            }
        }
        line_num++;
    }

    program_free(prog);
    if (toks) free(toks);
    if (allocated_content) free(allocated_content);
}

static void scan_dir_recursive(const char* dir_path, int is_std, int depth) {
    if (depth > 12 || !dir_path || !dir_path[0]) return;
    DIR* d = opendir(dir_path);
    if (!d) return;

    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        const char* name = de->d_name;
        if (name[0] == '.') continue;
        if (strcmp(name, "build") == 0 || strcmp(name, "bin") == 0 ||
            strcmp(name, "target") == 0 || strcmp(name, "node_modules") == 0 ||
            strcmp(name, ".git") == 0 || strcmp(name, ".vscode") == 0 ||
            strcmp(name, ".zed") == 0 || strcmp(name, ".cache") == 0) {
            continue;
        }

        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, name);

        size_t nlen = strlen(name);
        if (nlen > 5 && strcmp(name + nlen - 5, ".rook") == 0) {
            lsp_workspace_index_file(full_path, NULL, 0);
        } else {
            DIR* sub = opendir(full_path);
            if (sub) {
                closedir(sub);
                scan_dir_recursive(full_path, is_std, depth + 1);
            }
        }
    }
    closedir(d);
}

void lsp_workspace_init(const char* root_dir) {
    if (!root_dir || !root_dir[0]) return;
    char norm_new[4096];
    if (!rk_realpath(root_dir, norm_new)) snprintf(norm_new, sizeof(norm_new), "%s", root_dir);

    char norm_old[4096];
    if (g_indexed_root[0] && rk_realpath(g_indexed_root, norm_old)) {
        if (strcmp(norm_new, norm_old) == 0 && g_workspace_initialized) {
            return;
        }
    }

    snprintf(g_indexed_root, sizeof(g_indexed_root), "%s", norm_new);
    size_t len = strlen(g_indexed_root);
    while (len > 1 && (g_indexed_root[len - 1] == '/' || g_indexed_root[len - 1] == '\\')) {
        g_indexed_root[--len] = '\0';
    }

    /* Index standard library first */
    char std_path[4096];
    if (rokade_get_std_dir(std_path, sizeof(std_path)) == 0) {
        scan_dir_recursive(std_path, 1 /* is_std */, 0);
    }

    /* Index workspace project files */
    if (g_indexed_root[0]) {
        scan_dir_recursive(g_indexed_root, 0 /* is_std */, 0);
    }

    g_workspace_initialized = 1;
}

int lsp_workspace_find_definition(const char* name, char* out_uri, size_t uri_cap, int* out_line, int* out_col) {
    if (!name || !name[0] || !g_ws_syms) return 0;
    for (size_t i = 0; i < g_ws_count; i++) {
        if (strcmp(g_ws_syms[i].name, name) == 0) {
            if (out_uri) snprintf(out_uri, uri_cap, "%s", g_ws_syms[i].uri);
            if (out_line) *out_line = g_ws_syms[i].line;
            if (out_col) *out_col = g_ws_syms[i].character;
            return 1;
        }
    }
    return 0;
}

const WorkspaceSymbol* lsp_workspace_lookup(const char* name) {
    if (!name || !name[0] || !g_ws_syms) return NULL;
    for (size_t i = 0; i < g_ws_count; i++) {
        if (strcmp(g_ws_syms[i].name, name) == 0) {
            return &g_ws_syms[i];
        }
    }
    return NULL;
}

static int str_istarts_with(const char* str, const char* prefix) {
    if (!str || !prefix) return 0;
    while (*prefix) {
        if (tolower((unsigned char)*str) != tolower((unsigned char)*prefix)) return 0;
        str++;
        prefix++;
    }
    return 1;
}

static int str_icontains(const char* str, const char* needle) {
    if (!str || !needle) return 0;
    if (!*needle) return 1;
    size_t nlen = strlen(needle);
    for (size_t i = 0; str[i]; i++) {
        if (tolower((unsigned char)str[i]) == tolower((unsigned char)needle[0])) {
            size_t j = 0;
            while (needle[j] && tolower((unsigned char)str[i + j]) == tolower((unsigned char)needle[j])) {
                j++;
            }
            if (j == nlen) return 1;
        }
    }
    return 0;
}

void lsp_workspace_complete(const char* prefix, SB* res, int* count, const char* current_file) {
    if (!g_ws_syms || !res || !count) return;

    char norm_cur[4096] = "";
    if (current_file) {
        rk_realpath(current_file, norm_cur);
    }

    int ws_emitted = 0;
    for (size_t i = 0; i < g_ws_count; i++) {
        const WorkspaceSymbol* s = &g_ws_syms[i];

        /* Skip symbols from current open file (already in local AST) */
        if (norm_cur[0] && s->file_path[0]) {
            char norm_fp[4096];
            if (rk_realpath(s->file_path, norm_fp) && strcmp(norm_cur, norm_fp) == 0) {
                continue;
            }
        }

        if (prefix && prefix[0] && !str_istarts_with(s->name, prefix)) {
            continue;
        }

        if (*count > 0) sb_append(res, ",");
        (*count)++;

        char detail[512];
        if (s->is_std) {
            snprintf(detail, sizeof(detail), "%s (from %s)", s->signature[0] ? s->signature : s->name, s->mod_name);
        } else {
            snprintf(detail, sizeof(detail), "%s (%s)", s->signature[0] ? s->signature : s->name, s->mod_name);
        }

        sb_append(res, "{\"label\":");
        json_emit_escaped_str(res, s->name);
        sb_appendf(res, ",\"kind\":%d", s->kind);
        sb_append(res, ",\"detail\":");
        json_emit_escaped_str(res, detail);
        sb_append(res, ",\"sortText\":\"7_ws\"");
        sb_append(res, ",\"documentation\":{\"kind\":\"markdown\",\"value\":");
        json_emit_escaped_str(res, s->doc);
        sb_append(res, "}}");

        ws_emitted++;
        if (ws_emitted >= 200) break;
    }
}

void lsp_workspace_search_symbols(const char* query, SB* res) {
    sb_append(res, "[");
    if (!g_ws_syms) {
        sb_append(res, "]");
        return;
    }

    int count = 0;
    for (size_t i = 0; i < g_ws_count; i++) {
        const WorkspaceSymbol* s = &g_ws_syms[i];
        if (query && query[0] && !str_icontains(s->name, query) && !str_icontains(s->mod_name, query)) {
            continue;
        }

        if (count > 0) sb_append(res, ",");
        count++;

        sb_append(res, "{\"name\":");
        json_emit_escaped_str(res, s->name);
        sb_appendf(res, ",\"kind\":%d", s->kind);
        sb_append(res, ",\"containerName\":");
        json_emit_escaped_str(res, s->mod_name);
        sb_append(res, ",\"location\":{\"uri\":");
        json_emit_escaped_str(res, s->uri);
        sb_appendf(res, ",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}}",
                   s->line, s->character, s->line, s->character);

        if (count >= 100) break;
    }
    sb_append(res, "]");
}

void lsp_workspace_complete_modules(const char* prefix, SB* res, int* count) {
    if (!g_ws_syms || !res || !count) return;

    char emitted[256][128];
    int n_emitted = 0;

    for (size_t i = 0; i < g_ws_count && n_emitted < 256; i++) {
        const char* m = g_ws_syms[i].mod_name;
        if (!m || !m[0]) continue;

        int already = 0;
        for (int j = 0; j < n_emitted; j++) {
            if (strcmp(emitted[j], m) == 0) { already = 1; break; }
        }
        if (already) continue;

        if (prefix && prefix[0] && !str_istarts_with(m, prefix)) continue;

        snprintf(emitted[n_emitted++], sizeof(emitted[0]), "%s", m);

        if (*count > 0) sb_append(res, ",");
        (*count)++;

        sb_append(res, "{\"label\":");
        json_emit_escaped_str(res, m);
        sb_appendf(res, ",\"kind\":9,\"detail\":");
        if (g_ws_syms[i].is_std) {
            json_emit_escaped_str(res, "Standard library module");
        } else {
            json_emit_escaped_str(res, "Project module");
        }
        sb_append(res, ",\"sortText\":\"0_mod\"}");
    }
}
