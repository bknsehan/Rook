#include "lsp_doc.h"
#include "lsp_workspace.h"
#include "../parse.h"
#include "../util.h"
#include "../diag.h"
#include "../resolve.h"
#include "../c_import.h"
#include "../lint.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#ifndef R_OK
#define R_OK 4
#endif
#ifndef F_OK
#define F_OK 0
#endif
#define access _access
#else
#include <unistd.h>
#endif

static LspDoc* g_docs = NULL;
static char g_workspace_root[4096] = "";

void lsp_set_workspace_root(const char* root) {
    if (!root || !root[0]) return;
    snprintf(g_workspace_root, sizeof(g_workspace_root), "%s", root);
    size_t len = strlen(g_workspace_root);
    while (len > 1 && (g_workspace_root[len - 1] == '/' || g_workspace_root[len - 1] == '\\')) {
        g_workspace_root[--len] = '\0';
    }
}

const char* lsp_get_workspace_root(void) {
    return g_workspace_root[0] ? g_workspace_root : NULL;
}

/* Walk up from doc_path to find a directory containing rokade.toml.
   Returns a malloc'd path to the project root directory, or NULL. */
static char* find_project_root_from(const char* doc_path) {
    if (!doc_path || !*doc_path) return NULL;

    char buf[4096];
    snprintf(buf, sizeof(buf), "%s", doc_path);

    /* Strip filename to get directory */
    const char* slash = strrchr(buf, '/');
#ifdef _WIN32
    const char* bslash = strrchr(buf, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    if (slash) {
        size_t dlen = (size_t)(slash - buf);
        if (dlen >= sizeof(buf)) dlen = sizeof(buf) - 1;
        buf[dlen] = '\0';
    } else {
        /* doc_path is just a filename, use current directory "." */
        return strdup(".");
    }

    /* Walk up looking for rokade.toml */
    while (1) {
        char toml_path[4096];
        snprintf(toml_path, sizeof(toml_path), "%s/rokade.toml", buf);
        if (access(toml_path, R_OK) == 0) {
            return strdup(buf);
        }

        /* Move up one level */
        char* parent_slash = strrchr(buf, '/');
#ifdef _WIN32
        char* parent_bslash = strrchr(buf, '\\');
        if (!parent_slash || (parent_bslash && parent_bslash > parent_slash)) parent_slash = parent_bslash;
#endif
        if (!parent_slash || parent_slash == buf) {
            /* Reached root or no more parent */
            break;
        }
        *parent_slash = '\0';
        if (strlen(buf) == 0) break;
    }

    return NULL;
}

/* Find project root. Returns project root from rokade.toml, g_workspace_root, or "." */
const char* lsp_find_project_root(const char* doc_path) {
    if (!doc_path || !*doc_path) return g_workspace_root[0] ? g_workspace_root : ".";
    char* root = find_project_root_from(doc_path);
    if (root) return root;
    if (g_workspace_root[0]) return g_workspace_root;
    return ".";
}

/* Simple string trim: remove leading/trailing whitespace and quotes. */
static void trim_string(char* s) {
    if (!s || !*s) return;
    /* Trim leading whitespace */
    char* start = s;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    /* Trim trailing whitespace and quotes */
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
    if (len >= 2 && s[0] == '"' && s[len-1] == '"') {
        s[len-1] = '\0';
        memmove(s, s+1, len - 1);
    }
}

/* Parse rokade.toml to extract include-dirs from [build] section and
   dependency paths from [dependencies] section.
   Returns number of include dirs found (appended to inc_dirs array). */
int lsp_parse_rokade_toml_inc_dirs(const char* toml_path, char** inc_dirs, int max_inc_dirs, int* n_inc_dirs) {
    if (!toml_path || !inc_dirs || !n_inc_dirs) return 0;

    int file_len = 0;
    char* content = util_read_file(toml_path, &file_len);
    if (!content) return 0;

    int count = *n_inc_dirs;
    int in_build = 0;
    int in_dependencies = 0;
    int in_dep_table = 0;
    char dep_path[4096] = "";
    const char* p = content;
    const char* end = content + file_len;

    while (p < end) {
        const char* nl = memchr(p, '\n', (size_t)(end - p));
        int line_len = nl ? (int)(nl - p) : (int)(end - p);
        char line[2048];
        int copy_len = line_len < (int)sizeof(line) ? line_len : (int)sizeof(line) - 1;
        memcpy(line, p, copy_len);
        line[copy_len] = '\0';
        if (nl) p = nl + 1; else p = end;

        /* Skip comments - strip everything after # */
        char clean_line[2048];
        const char* hash = strchr(line, '#');
        if (hash) {
            size_t hlen = (size_t)(hash - line);
            if (hlen >= sizeof(clean_line)) hlen = sizeof(clean_line) - 1;
            memcpy(clean_line, line, hlen);
            clean_line[hlen] = '\0';
        } else {
            strncpy(clean_line, line, sizeof(clean_line) - 1);
            clean_line[sizeof(clean_line) - 1] = '\0';
        }

        /* Trim leading whitespace */
        char* t = clean_line;
        while (*t == ' ' || *t == '\t') t++;
        if (*t == '\0') continue;

        /* Check for section headers: [section] */
        if (t[0] == '[') {
            char sec[256];
            if (sscanf(t, "[%255[^]]]", sec) == 1) {
                in_build = (strcmp(sec, "build") == 0);
                in_dependencies = (strcmp(sec, "dependencies") == 0);
                in_dep_table = 0;
                continue;
            }
        }

        /* Detect [dependencies.something] subsection tables (legacy format) */
        if (t[0] == '[' && in_dependencies) {
            char sec[256];
            if (sscanf(t, "[%255[^]]]", sec) == 1) {
                in_dep_table = 1;
                dep_path[0] = '\0';
                continue;
            }
        }

        if (in_dependencies && !in_dep_table) {
            /* Handle inline table: key = { path = "..." } */
            const char* eq = strchr(t, '=');
            if (eq) {
                const char* brace = strchr(eq, '{');
                if (brace) {
                    /* Search for path = "..." inside the inline table */
                    const char* path_key = strstr(brace, "path");
                    if (path_key) {
                        const char* path_eq = strchr(path_key, '=');
                        if (path_eq) {
                            path_eq++;
                            while (*path_eq == ' ' || *path_eq == '\t') path_eq++;
                            if (*path_eq == '"') {
                                path_eq++;
                                const char* start = path_eq;
                                while (*path_eq && *path_eq != '"') path_eq++;
                                if (*path_eq == '"') {
                                    int plen = (int)(path_eq - start);
                                    if (plen > 0 && plen < (int)sizeof(dep_path)) {
                                        memcpy(dep_path, start, plen);
                                        dep_path[plen] = '\0';
                                    }
                                }
                            }
                        }
                    }
                }
            }
            continue;
        }

        if (in_build) {
            /* Parse include-dirs = ["src", "vendor/stb"] */
            if (strncmp(t, "include-dirs", 11) == 0) {
                const char* eq = strchr(t, '=');
                if (eq) {
                    eq++;
                    while (*eq == ' ' || *eq == '\t') eq++;
                    /* Parse array values */
                    if (*eq == '[') {
                        eq++;
                        char cur_str[512];
                        int cur_idx = 0;
                        int in_quote = 0;
                        while (*eq && *eq != ']') {
                            if (*eq == '"') {
                                if (!in_quote) {
                                    in_quote = 1;
                                    cur_idx = 0;
                                    eq++;
                                    continue;
                                } else {
                                    /* End of quoted string */
                                    cur_str[cur_idx] = '\0';
                                    if (count < max_inc_dirs) {
                                        trim_string(cur_str);
                                        inc_dirs[count] = strdup(cur_str);
                                        count++;
                                    }
                                    in_quote = 0;
                                    eq++;
                                    continue;
                                }
                            }
                            if (in_quote && cur_idx < (int)sizeof(cur_str) - 1) {
                                cur_str[cur_idx++] = *eq;
                            }
                            eq++;
                        }
                    }
                }
            }
        }

        if (in_dep_table) {
            /* Parse path = "..." inside dependency table */
            if (strncmp(t, "path", 4) == 0) {
                const char* eq = strchr(t, '=');
                if (eq) {
                    eq++;
                    while (*eq == ' ' || *eq == '\t') eq++;
                    if (*eq == '"') {
                        eq++;
                        const char* start = eq;
                        while (*eq && *eq != '"') eq++;
                        if (*eq == '"') {
                            int plen = (int)(eq - start);
                            if (plen > 0 && plen < (int)sizeof(dep_path)) {
                                memcpy(dep_path, start, plen);
                                dep_path[plen] = '\0';
                            }
                        }
                    }
                }
                if (dep_path[0] && count < max_inc_dirs) {
                    inc_dirs[count] = strdup(dep_path);
                    count++;
                    dep_path[0] = '\0';
                }
                in_dep_table = 0;
            }
        }

        /* Handle inline table dep_path collected during [dependencies] section */
        if (in_dependencies && !in_dep_table && dep_path[0]) {
            if (count < max_inc_dirs) {
                inc_dirs[count] = strdup(dep_path);
                count++;
                dep_path[0] = '\0';
            }
        }
    }

    free(content);
    *n_inc_dirs = count;
    return count;
}

void lsp_doc_init(void) {
    g_docs = NULL;
}

static void lsp_free_diags(LspDoc* doc) {
    if (!doc) return;
    for (int i = 0; i < doc->ndiags; i++) {
        free(doc->diags[i].message);
    }
    free(doc->diags);
    doc->diags = NULL;
    doc->ndiags = 0;
}

static void doc_add_extra_buffer(LspDoc* doc, char* buf) {
    if (!doc || !buf) return;
    if (doc->nextra_buffers >= doc->capextra_buffers) {
        int new_cap = doc->capextra_buffers ? doc->capextra_buffers * 2 : 16;
        char** nb = realloc(doc->extra_buffers, new_cap * sizeof(char*));
        if (!nb) return;
        doc->extra_buffers = nb;
        doc->capextra_buffers = new_cap;
    }
    doc->extra_buffers[doc->nextra_buffers++] = buf;
}

static void lsp_free_analysis(LspDoc* doc) {
    if (!doc) return;
    lsp_free_diags(doc);
    if (doc->sema) {
        sema_free(doc->sema);
        doc->sema = NULL;
    }
    if (doc->prog) {
        program_free(doc->prog);
        doc->prog = NULL;
    }
    if (doc->toks) {
        free(doc->toks);
        doc->toks = NULL;
        doc->ntoks = 0;
    }
    if (doc->extra_buffers) {
        for (int i = 0; i < doc->nextra_buffers; i++) {
            if (doc->extra_buffers[i]) free(doc->extra_buffers[i]);
        }
        free(doc->extra_buffers);
        doc->extra_buffers = NULL;
        doc->nextra_buffers = 0;
        doc->capextra_buffers = 0;
    }
}

static void doc_free_node(LspDoc* doc) {
    if (!doc) return;
    lsp_free_analysis(doc);
    free(doc->uri);
    free(doc->path);
    free(doc->text);
    free(doc);
}

void lsp_doc_free_all(void) {
    LspDoc* cur = g_docs;
    while (cur) {
        LspDoc* nxt = cur->next;
        doc_free_node(cur);
        cur = nxt;
    }
    g_docs = NULL;
}

LspDoc* lsp_doc_get(const char* uri) {
    if (!uri) return NULL;
    for (LspDoc* d = g_docs; d; d = d->next) {
        if (strcmp(d->uri, uri) == 0) return d;
    }
    return NULL;
}

LspDoc* lsp_doc_open(const char* uri, const char* text, int version) {
    if (!uri || !text) return NULL;
    LspDoc* d = lsp_doc_get(uri);
    if (d) {
        return lsp_doc_update(uri, text, version);
    }
    d = calloc(1, sizeof *d);
    d->uri = strdup(uri);
    char path_buf[4096];
    if (lsp_uri_to_path(uri, path_buf, sizeof(path_buf))) {
        d->path = strdup(path_buf);
    } else {
        d->path = strdup(uri);
    }
    d->text = strdup(text);
    d->len = (int)strlen(text);
    d->version = version;

    d->next = g_docs;
    g_docs = d;

    lsp_doc_analyze(d);
    return d;
}

LspDoc* lsp_doc_update(const char* uri, const char* text, int version) {
    LspDoc* d = lsp_doc_get(uri);
    if (!d) return lsp_doc_open(uri, text, version);

    free(d->text);
    d->text = strdup(text);
    d->len = (int)strlen(text);
    d->version = version;

    lsp_doc_analyze(d);
    return d;
}

void lsp_doc_close(const char* uri) {
    if (!uri) return;
    LspDoc** prev = &g_docs;
    for (LspDoc* d = g_docs; d; d = d->next) {
        if (strcmp(d->uri, uri) == 0) {
            *prev = d->next;
            doc_free_node(d);
            return;
        }
        prev = &d->next;
    }
}

/* Structured push (1-based in, 0-based stored). Dedups identical entries. */
static void lsp_push_diag(LspDoc* doc, int line1, int col1, int end_col1,
                          int severity, const char* code, const char* message) {
    if (!doc || !message || !message[0]) return;
    if (line1 < 1) line1 = 1;
    if (col1 < 1) col1 = 1;
    if (end_col1 < col1 + 1) end_col1 = col1 + 1;
    if (severity < 1 || severity > 4) severity = 1;
    int line0 = line1 - 1, col0 = col1 - 1, end0 = end_col1 - 1;
    for (int i = 0; i < doc->ndiags; i++) {
        LspDiagnostic* d = &doc->diags[i];
        if (d->line == line0 && d->character == col0 && d->severity == severity &&
            strcmp(d->message, message) == 0)
            return;
    }
    if (doc->ndiags >= 256) return; /* cap per file */
    doc->diags = realloc(doc->diags, (doc->ndiags + 1) * sizeof(LspDiagnostic));
    LspDiagnostic* d = &doc->diags[doc->ndiags];
    d->line = line0;
    d->character = col0;
    d->end_line = line0;
    d->end_character = end0;
    d->severity = severity;
    snprintf(d->code, sizeof d->code, "%s", code ? code : "");
    d->tags = 0;
    if (severity >= 2 && code &&
        (strcmp(code, "W2001") == 0 || strcmp(code, "W2003") == 0))
        d->tags = 1; /* Unnecessary */
    d->message = strdup(message);
    doc->ndiags++;
}

/* Parse diag line into LspDiagnostic (legacy string fallback) */
static void add_diag_from_msg(LspDoc* doc, const char* msg_str) {
    if (!doc || !msg_str || !msg_str[0]) return;
    int line = 1, col = 1;
    const char* p = msg_str;

    /* Check if prefixed with "path:line:col:" or "line:col:" */
    const char* colon1 = strchr(p, ':');
    if (colon1) {
        const char* colon2 = strchr(colon1 + 1, ':');
        if (colon2) {
            const char* colon3 = strchr(colon2 + 1, ':');
            if (colon3) {
                line = atoi(colon1 + 1);
                col = atoi(colon2 + 1);
                p = colon3 + 1;
            } else {
                line = atoi(p);
                col = atoi(colon1 + 1);
                p = colon2 + 1;
            }
        }
    }
    while (*p == ' ' || *p == '\t') p++;

    int sev = 1; /* error */
    if (strncmp(p, "warning:", 8) == 0) {
        sev = 2;
        p += 8;
    } else if (strncmp(p, "error:", 6) == 0) {
        sev = 1;
        p += 6;
    }
    while (*p == ' ' || *p == '\t') p++;

    /* First line only; diag_render appends source + caret lines. */
    char short_msg[1024];
    size_t ml = strcspn(p, "\r\n");
    if (ml >= sizeof short_msg) ml = sizeof short_msg - 1;
    memcpy(short_msg, p, ml);
    short_msg[ml] = '\0';
    lsp_push_diag(doc, line, col, col + 1, sev, "", short_msg);
}

static void lsp_load_comprise_recursive(LspDoc* doc, Program* prog, const char* filepath, const char* alias,
                                        const char* basedir, const char** inc_dirs, size_t n_inc,
                                        char visited[64][4096], int* n_visited, int depth) {
    if (depth > 16 || !filepath || !prog || !doc) return;

    char real_fp[4096];
    const char* track_path = rk_realpath(filepath, real_fp) ? real_fp : filepath;
    for (int v = 0; v < *n_visited; v++) {
        if (strcmp(visited[v], track_path) == 0) return;
    }
    if (*n_visited < 64) {
        snprintf(visited[(*n_visited)++], 4096, "%s", track_path);
    }

    int ilen = 0;
    char* isrc = util_read_file(track_path, &ilen);
    if (!isrc) return;

    int intoks = 0;
    Token* itoks = lex_all(isrc, ilen, &intoks);
    Program* iprog = parse_program_tolerant(isrc, ilen, itoks, intoks);
    if (iprog) {
        for (int i = 0; i < iprog->nitems; i++) {
            Item* it = iprog->items[i];
            if (!it) continue;
            if (it->kind == TOP_RAW) {
                ast_program_add(prog, it);
                continue;
            }
            if (it->kind == TOP_FN && it->fn) {
                if (alias && alias[0] && !it->fn->mod_prefix) it->fn->mod_prefix = strdup(alias);
                if (!it->fn->source_file) it->fn->source_file = strdup(track_path);
            } else if (it->kind == TOP_STRUCT && it->st) {
                if (alias && alias[0] && !it->st->mod_prefix) it->st->mod_prefix = strdup(alias);
                if (!it->st->source_file) it->st->source_file = strdup(track_path);
            } else if (it->kind == TOP_ENUM && it->ed) {
                if (alias && alias[0] && !it->ed->mod_prefix) it->ed->mod_prefix = strdup(alias);
                if (!it->ed->source_file) it->ed->source_file = strdup(track_path);
            } else if (it->kind == TOP_IMPL && it->im) {
                for (int m = 0; m < it->im->nmethods; m++) {
                    if (alias && alias[0] && !it->im->methods[m]->mod_prefix)
                        it->im->methods[m]->mod_prefix = strdup(alias);
                    if (!it->im->methods[m]->source_file)
                        it->im->methods[m]->source_file = strdup(track_path);
                }
            }
            ast_program_add(prog, it);
        }
        free(iprog->items);
        free(iprog);
    }
    free(itoks);

    /* Determine directory of this included file for relative comprises */
    char cur_dir[4096] = ".";
    const char* slash = strrchr(track_path, '/');
#ifdef _WIN32
    const char* bslash = strrchr(track_path, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    if (slash && slash > track_path) {
        size_t dlen = (size_t)(slash - track_path);
        if (dlen < sizeof(cur_dir)) {
            memcpy(cur_dir, track_path, dlen);
            cur_dir[dlen] = '\0';
        }
    }

    /* Scan isrc for nested comprises */
    const char* p = isrc;
    const char* end = isrc + ilen;
    while (p < end) {
        const char* nl = memchr(p, '\n', (size_t)(end - p));
        int line_len = nl ? (int)(nl - p) : (int)(end - p);
        char line[2048];
        int copy_len = line_len < (int)sizeof(line) ? line_len : (int)sizeof(line) - 1;
        memcpy(line, p, copy_len);
        line[copy_len] = '\0';
        p = nl ? nl + 1 : end;

        const char* s = line;
        while (*s == ' ' || *s == '\t') s++;
        int is_sub_comp = 0;
        const char* after = NULL;
        if (s[0] == '#' && strncmp(s + 1, "comprise", 8) == 0) {
            after = s + 9;
            is_sub_comp = 1;
        } else if (strncmp(s, "comprise", 8) == 0 && (s[8] == ' ' || s[8] == '\t')) {
            after = s + 8;
            is_sub_comp = 1;
        } else if (s[0] == '#' && strncmp(s + 1, "include", 7) == 0) {
            after = s + 8;
            is_sub_comp = 1;
        }
        if (is_sub_comp && after) {
            while (*after == ' ' || *after == '\t') after++;
            char quote = 0;
            if (*after == '"' || *after == '<') { quote = *after; after++; }
            const char* id0 = after;
            while (*after && *after != '\r' && *after != '\n' && *after != ';') {
                if (quote == '"' && *after == '"') break;
                if (quote == '<' && *after == '>') break;
                if (!quote && (*after == ' ' || *after == '\t')) break;
                after++;
            }
            size_t idlen = (size_t)(after - id0);
            char sub_modpath[512] = "";
            if (idlen > 0 && idlen < sizeof(sub_modpath)) {
                memcpy(sub_modpath, id0, idlen);
                sub_modpath[idlen] = '\0';
            }
            if (sub_modpath[0]) {
                char* sub_res = resolve_include_path(sub_modpath, cur_dir, inc_dirs, n_inc);
                if (sub_res) {
                    lsp_load_comprise_recursive(doc, prog, sub_res, alias, cur_dir, inc_dirs, n_inc, visited, n_visited, depth + 1);
                    free(sub_res);
                }
            }
        }
    }
    doc_add_extra_buffer(doc, isrc);
}

void lsp_doc_analyze(LspDoc* doc) {
    if (!doc || !doc->text) return;
    lsp_free_analysis(doc);

    doc->toks = lex_all(doc->text, doc->len, &doc->ntoks);
    doc->prog = parse_program_tolerant(doc->text, doc->len, doc->toks, doc->ntoks);
    doc->local_items_count = doc->prog ? doc->prog->nitems : 0;

    /* All syntax errors (was: only the first via parse_error()). */
    for (int i = 0; i < parse_error_count(); i++) {
        const ParseDiag* pd = parse_error_get(i);
        if (pd) lsp_push_diag(doc, pd->line, pd->col, pd->col + pd->len,
                              1, pd->code, pd->msg);
    }

    if (!doc->prog) {
        return;
    }

    /* Determine doc_dir and project_root */
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

    if (!lsp_workspace_is_initialized() && project_root[0] && strcmp(project_root, ".") != 0) {
        lsp_workspace_init(project_root);
    }

    char std_path[4096];
    const char* inc_list[32];
    size_t n_inc = 0;
    if (rokade_get_std_dir(std_path, sizeof(std_path)) == 0) {
        inc_list[n_inc++] = strdup(std_path);
    }
    char root_src[4096];
    char root_vendor[4096];
    if (project_root[0] && strcmp(project_root, ".") != 0) {
        inc_list[n_inc++] = strdup(project_root);
        snprintf(root_src, sizeof(root_src), "%s/src", project_root);
        if (access(root_src, R_OK) == 0) inc_list[n_inc++] = strdup(root_src);
        snprintf(root_vendor, sizeof(root_vendor), "%s/vendor", project_root);
        if (access(root_vendor, R_OK) == 0) inc_list[n_inc++] = strdup(root_vendor);
    }

    /* Parse rokade.toml for project-specific include directories and dependency paths */
    if (project_root[0] && strcmp(project_root, ".") != 0) {
        char toml_path[4096];
        snprintf(toml_path, sizeof(toml_path), "%s/rokade.toml", project_root);
        if (access(toml_path, R_OK) == 0) {
            char* inc_dirs[16] = {0};
            int n_inc_dirs = 0;
            lsp_parse_rokade_toml_inc_dirs(toml_path, inc_dirs, 16, &n_inc_dirs);
            for (int i = 0; i < n_inc_dirs && n_inc < 32; i++) {
                if (inc_dirs[i][0] == '/' || inc_dirs[i][0] == '\\' ||
                    (inc_dirs[i][1] == ':' && (inc_dirs[i][2] == '/' || inc_dirs[i][2] == '\\'))) {
                    inc_list[n_inc++] = inc_dirs[i];
                } else {
                    char full_inc[4096];
                    snprintf(full_inc, sizeof(full_inc), "%s/%s", project_root, inc_dirs[i]);
                    inc_list[n_inc++] = strdup(full_inc);
                    free(inc_dirs[i]);
                }
            }
        }
    }

    char visited[64][4096];
    int n_visited = 0;

    /* Import comprised / included modules into doc->prog */
    const char* p = doc->text;
    const char* end = doc->text + doc->len;
    int current_line = 1;
    while (p < end) {
        const char* nl = memchr(p, '\n', (size_t)(end - p));
        int line_len = nl ? (int)(nl - p) : (int)(end - p);
        char line[2048];
        int copy_len = line_len < (int)sizeof(line) ? line_len : (int)sizeof(line) - 1;
        memcpy(line, p, copy_len);
        line[copy_len] = '\0';
        p = nl ? nl + 1 : end;

        const char* s = line;
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
        }
        if (is_comprise && after) {
            while (*after == ' ' || *after == '\t') after++;
            char quote = 0;
            if (*after == '"' || *after == '<') {
                quote = *after;
                after++;
            }
            const char* id0 = after;
            while (*after && *after != '\r' && *after != '\n' && *after != ';') {
                if (quote == '"' && *after == '"') break;
                if (quote == '<' && *after == '>') break;
                if (!quote && (*after == ' ' || *after == '\t')) break;
                after++;
            }
            size_t idlen = (size_t)(after - id0);
            char modpath[512] = "";
            if (idlen > 0 && idlen < sizeof(modpath)) {
                memcpy(modpath, id0, idlen);
                modpath[idlen] = '\0';
            }
            const char* after_quote = after;
            if (quote && *after == quote) after++;
            while (*after == ' ' || *after == '\t') after++;
            char alias[128] = "";
            if (strncmp(after, "as", 2) == 0 && (after[2] == ' ' || after[2] == '\t')) {
                after += 2;
                while (*after == ' ' || *after == '\t') after++;
                const char* a0 = after;
                while (*after && *after != ' ' && *after != '\t' && *after != ';' && *after != '\r' && *after != '\n') after++;
                size_t alen = (size_t)(after - a0);
                if (alen > 0 && alen < sizeof(alias)) {
                    memcpy(alias, a0, alen);
                    alias[alen] = '\0';
                }
            }

            if (modpath[0]) {
                char* resolved = resolve_include_path(modpath, basedir, inc_list, n_inc);
                if (resolved) {
                    lsp_load_comprise_recursive(doc, doc->prog, resolved, alias[0] ? alias : NULL, basedir, inc_list, n_inc, visited, &n_visited, 0);
                    free(resolved);
                } else {
                    char err_msg[512];
                    snprintf(err_msg, sizeof(err_msg), "cannot find module '%s'", modpath);
                    int col_start = (int)(id0 - line) + 1;
                    int col_end = (int)(after_quote - line) + 1;
                    if (col_end <= col_start) col_end = col_start + (int)idlen;
                    lsp_push_diag(doc, current_line, col_start, col_end, 1, "E1001", err_msg);
                }
            }
        } else if (is_include && after) {
            while (*after == ' ' || *after == '\t') after++;
            char quote = 0;
            if (*after == '"' || *after == '<') {
                quote = *after;
                after++;
            }
            const char* id0 = after;
            while (*after && *after != '\r' && *after != '\n' && *after != ';') {
                if (quote == '"' && *after == '"') break;
                if (quote == '<' && *after == '>') break;
                if (!quote && (*after == ' ' || *after == '\t')) break;
                after++;
            }
            size_t idlen = (size_t)(after - id0);
            char hname[512] = "";
            if (idlen > 0 && idlen < sizeof(hname)) {
                memcpy(hname, id0, idlen);
                hname[idlen] = '\0';
            }
            const char* after_quote = after;
            if (hname[0]) {
                if (idlen >= 5 && strcmp(hname + idlen - 5, ".rook") == 0) {
                    char* resolved = resolve_include_path(hname, basedir, inc_list, n_inc);
                    if (resolved) {
                        lsp_load_comprise_recursive(doc, doc->prog, resolved, NULL, basedir, inc_list, n_inc, visited, &n_visited, 0);
                        free(resolved);
                    } else {
                        char err_msg[512];
                        snprintf(err_msg, sizeof(err_msg), "cannot find module '%s'", hname);
                        int col_start = (int)(id0 - line) + 1;
                        int col_end = (int)(after_quote - line) + 1;
                        if (col_end <= col_start) col_end = col_start + (int)idlen;
                        lsp_push_diag(doc, current_line, col_start, col_end, 1, "E1001", err_msg);
                    }
                } else {
                    char* resolved = resolve_c_header_path(hname, basedir, inc_list, n_inc);
                    if (resolved) {
                        free(resolved);
                    } else {
                        static const char* std_c_hdrs[] = {
                            "stdio.h", "stdlib.h", "string.h", "stdbool.h", "stdint.h",
                            "stddef.h", "math.h", "time.h", "errno.h", "assert.h",
                            "ctype.h", "fcntl.h", "unistd.h", "sys/stat.h", "sys/types.h",
                            "pthread.h", "limits.h", "float.h", "signal.h", "setjmp.h",
                            NULL
                        };
                        int is_std_h = 0;
                        for (int k = 0; std_c_hdrs[k]; k++) {
                            if (strcmp(std_c_hdrs[k], hname) == 0) { is_std_h = 1; break; }
                        }
                        if (!is_std_h) {
                            char err_msg[512];
                            snprintf(err_msg, sizeof(err_msg), "cannot find C header '%s'", hname);
                            int col_start = (int)(id0 - line) + 1;
                            int col_end = (int)(after_quote - line) + 1;
                            if (col_end <= col_start) col_end = col_start + (int)idlen;
                            lsp_push_diag(doc, current_line, col_start, col_end, 1, "E1002", err_msg);
                        }
                    }
                }
            }
        }
        current_line++;
    }

    doc->sema = sema_new();
    sema_set_source(doc->sema, doc->text, doc->len);
    sema_load_commandlist(project_root[0] && strcmp(project_root, ".") != 0 ? project_root : basedir, NULL);

    c_import_init();
    c_import_scan_and_load(doc->sema, doc->text, doc->len, basedir, inc_list, n_inc);
    c_import_program_raw(doc->sema, doc->prog, inc_list, n_inc);

    sema_collect(doc->sema, doc->prog);
    sema_set_raw_hook(lsp_workspace_populate_raw_names);
    /* Per-item checking: only check local document items for diagnostics */
    sema_check_all_ex(doc->sema, doc->prog, doc->local_items_count);

    for (int i = 0; i < sema_diag_count(doc->sema); i++) {
        SemaDiag* sd = sema_diag_get(doc->sema, i);
        if (sd) lsp_push_diag(doc, sd->line, sd->col, sd->end_col,
                              sd->severity, sd->code, sd->message);
    }

    /* AST-only warning passes (unused vars, unreachable code, ...).
       Only lint the local document items, not comprised imports. */
    {
        int before = sema_diag_count(doc->sema);
        lint_run(doc->prog, doc->local_items_count, doc->sema);
        for (int i = before; i < sema_diag_count(doc->sema); i++) {
            SemaDiag* sd = sema_diag_get(doc->sema, i);
            if (sd) lsp_push_diag(doc, sd->line, sd->col, sd->end_col,
                                  sd->severity, sd->code, sd->message);
        }
    }

    /* Legacy fallback: if structured stores are empty but a string error
       exists (e.g. c_import failure), surface it so nothing is swallowed. */
    if (doc->ndiags == 0 && doc->sema->err && doc->sema->err[0]) {
        add_diag_from_msg(doc, doc->sema->err);
    }

    for (size_t i = 0; i < n_inc; i++) {
        if (inc_list[i]) free((void*)inc_list[i]);
    }
}

/* Converts file:// URI to filesystem path */
int lsp_uri_to_path(const char* uri, char* out_path, size_t cap) {
    if (!uri || !out_path || cap == 0) return 0;
    const char* p = uri;
    if (strncmp(p, "file://", 7) == 0) p += 7;

#ifdef _WIN32
    if (p[0] == '/' && p[1] >= 'A' && p[1] <= 'Z' && p[2] == ':') {
        p++;
    }
#endif

    /* Percent-decode into out_path */
    size_t out_idx = 0;
    while (*p && out_idx + 1 < cap) {
        if (*p == '%' && p[1] && p[2]) {
            char hex[3] = { p[1], p[2], '\0' };
            char* endp = NULL;
            long val = strtol(hex, &endp, 16);
            if (endp == hex + 2) {
                out_path[out_idx++] = (char)val;
                p += 3;
                continue;
            }
        }
        out_path[out_idx++] = *p++;
    }
    out_path[out_idx] = '\0';
    return 1;
}

void lsp_path_to_uri(const char* path, char* out_uri, size_t cap) {
    if (!path || !out_uri || cap == 0) return;
    char abs[4096];
    const char* raw = path;
    if (path[0] != '/' && rk_realpath(path, abs)) {
        raw = abs;
    }

    size_t out_idx = 0;
    const char* prefix = "file://";
    size_t plen = strlen(prefix);
    if (plen >= cap) return;
    memcpy(out_uri, prefix, plen);
    out_idx = plen;

    if (raw[0] != '/') {
        if (out_idx + 1 < cap) out_uri[out_idx++] = '/';
    }

    for (const char* p = raw; *p && out_idx + 1 < cap; p++) {
        char c = *p;
#ifdef _WIN32
        if (c == '\\') c = '/';
#endif
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == '.' ||
            c == '_' || c == '-' || c == '~' || c == ':') {
            out_uri[out_idx++] = c;
        } else {
            if (out_idx + 3 < cap) {
                snprintf(out_uri + out_idx, cap - out_idx, "%%%02X", (unsigned char)c);
                out_idx += 3;
            }
        }
    }
    out_uri[out_idx] = '\0';
}

int lsp_pos_to_offset(const char* text, int len, int line, int character) {
    if (!text || len <= 0 || line < 0 || character < 0) return 0;
    int cur_line = 0;
    int offset = 0;
    while (offset < len && cur_line < line) {
        if (text[offset] == '\n') cur_line++;
        offset++;
    }
    if (cur_line < line) return len;
    int end_of_line = offset;
    while (end_of_line < len && text[end_of_line] != '\n' && text[end_of_line] != '\r') {
        end_of_line++;
    }
    int target = offset + character;
    return target <= end_of_line ? target : end_of_line;
}

void lsp_offset_to_pos(const char* text, int len, int offset, int* out_line, int* out_char) {
    if (!text || offset <= 0) {
        if (out_line) *out_line = 0;
        if (out_char) *out_char = 0;
        return;
    }
    int line = 0, col = 0;
    int limit = offset < len ? offset : len;
    for (int i = 0; i < limit; i++) {
        if (text[i] == '\n') {
            line++;
            col = 0;
        } else {
            col++;
        }
    }
    if (out_line) *out_line = line;
    if (out_char) *out_char = col;
}

const char* lsp_get_line_prefix(const char* text, int len, int line, int character, int* out_prefix_len) {
    if (!text || len <= 0 || line < 0) {
        if (out_prefix_len) *out_prefix_len = 0;
        return "";
    }
    int cur_line = 0;
    int line_start = 0;
    while (line_start < len && cur_line < line) {
        if (text[line_start] == '\n') cur_line++;
        line_start++;
    }
    if (cur_line < line) {
        if (out_prefix_len) *out_prefix_len = 0;
        return "";
    }
    int cur_pos = lsp_pos_to_offset(text, len, line, character);
    int plen = cur_pos - line_start;
    if (plen < 0) plen = 0;
    if (out_prefix_len) *out_prefix_len = plen;
    return text + line_start;
}
