#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ast.h"
#include "backend.h"
#include "codegen.h"
#include "config.h"
#include "toolchain.h"
#include "diag.h"
#include "emit.h"
#include "lexer.h"
#include "parse.h"
#include "sema.h"
#include "util.h"

#ifndef ROKADE_VERSION
#define ROKADE_VERSION "0.3.0"
#endif

/* ---------- include resolution ---------- */

/* Resolve an include path, searching basedir first, then include dirs.
   Returns a malloc'd path or NULL if not found. */
static char* resolve_include_path(const char* incpath, const char* basedir,
                                   const char** inc_dirs, size_t n_inc) {
    char candidate[4096];
    if (basedir) {
        snprintf(candidate, sizeof(candidate), "%s/%s", basedir, incpath);
        if (access(candidate, R_OK) == 0) return strdup(candidate);
    }
    for (size_t i = 0; i < n_inc; i++) {
        snprintf(candidate, sizeof(candidate), "%s/%s", inc_dirs[i], incpath);
        if (access(candidate, R_OK) == 0) return strdup(candidate);
    }
    snprintf(candidate, sizeof(candidate), "%s", incpath);
    if (access(candidate, R_OK) == 0) return strdup(candidate);
    return NULL;
}

/* Recursively resolve #include "file.rook" directives in source text.
   C includes (#include <header.h>) are passed through verbatim.
   Also expands Rook-level `include NAME;` (rewritten to #include "NAME.rook").
   Returns a malloc'd string with includes expanded, or NULL on error. */
static int match_majinc_include(const char* line, char* out_mod, size_t modcap) {
    const char* s = line;
    while (*s == ' ' || *s == '\t') s++;
    if (memcmp(s, "include", 7) != 0) return 0;
    s += 7;
    if (*s != ' ' && *s != '\t') return 0;                 /* reject "includes…" */
    while (*s == ' ' || *s == '\t') s++;
    const char* id0 = s;
    while ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
           (*s >= '0' && *s <= '9') || *s == '_') s++;
    size_t idlen = (size_t)(s - id0);
    if (idlen == 0 || idlen >= modcap) return 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s != ';') return 0;
    s++;
    while (*s == ' ' || *s == '\t') s++;
    if (*s != '\0') return 0;                               /* whole line must be the directive */
    memcpy(out_mod, id0, idlen);
    out_mod[idlen] = '\0';
    return 1;
}

/* Rewrite Rook-level `include NAME;` (no '#') into `#include "NAME.rook"`
   and `#comprise NAME` into `#include "NAME.rook"`
   so the existing include resolver expands it. Returns malloc'd string
   (caller frees). Keeps the two include kinds distinct at source level
   (#include = raw C, include/comprise = Rook module) while reusing one resolver. */
static char* rook_rewrites_includes(const char* src, int src_len) {
    SB out;
    sb_init(&out);
    const char* p = src;
    const char* end = src + src_len;
    while (p < end) {
        const char* nl = memchr(p, '\n', (size_t)(end - p));
        int line_len = nl ? (int)(nl - p + 1) : (int)(end - p);
        char line[4096];
        int copy_len = line_len < (int)sizeof(line) ? line_len : (int)sizeof(line) - 1;
        memcpy(line, p, copy_len);
        line[copy_len] = '\0';
        char* cr = strpbrk(line, "\r\n");
        if (cr) *cr = '\0';
        char mod[256];

        /* Check for `#comprise` or `comprise` */
        const char* s = line;
        while (*s == ' ' || *s == '\t') s++;
        int is_comprise = 0;
        const char* after = NULL;
        if (s[0] == '#' && memcmp(s + 1, "comprise", 8) == 0) {
            after = s + 9;
            is_comprise = 1;
        } else if (memcmp(s, "comprise", 8) == 0 && (s[8] == ' ' || s[8] == '\t')) {
            after = s + 8;
            is_comprise = 1;
        }
        if (is_comprise) {
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
            if (idlen > 0 && idlen < sizeof mod) {
                memcpy(mod, id0, idlen);
                mod[idlen] = '\0';
                if (idlen > 5 && strcmp(mod + idlen - 5, ".rook") == 0) {
                    mod[idlen - 5] = '\0';
                }
                const char* indent = p;
                while (indent < end && (*indent == ' ' || *indent == '\t')) indent++;
                sb_appendn(&out, p, (int)(indent - p));
                sb_append(&out, "#include \"");
                sb_append(&out, mod);
                sb_append(&out, ".rook\"\n");
                p = nl ? nl + 1 : end;
                continue;
            }
        }

        if (match_majinc_include(line, mod, sizeof mod)) {
            const char* indent = p;
            while (indent < end && (*indent == ' ' || *indent == '\t')) indent++;
            sb_appendn(&out, p, (int)(indent - p));
            sb_append(&out, "#include \"");
            sb_append(&out, mod);
            sb_append(&out, ".rook\"\n");
        } else {
            sb_appendn(&out, p, line_len);                 /* verbatim, newline included */
        }
        p = nl ? nl + 1 : end;
    }
    return sb_strdup(&out);
}

typedef struct VisitedInc {
    char path[4096];
    struct VisitedInc* next;
} VisitedInc;

/* Recursively resolve #include "file.rook" directives in source text.
   C includes (#include <header.h>) are passed through verbatim.
   Also expands Rook-level `include NAME;` (rewritten to #include "NAME.rook")
   and `#comprise NAME`.
   Applies #pragma once deduplication across included files.
   Returns a malloc'd string with includes expanded, or NULL on error. */
static char* resolve_includes_rec(const char* src, int src_len, const char* basedir,
                                  const char** inc_dirs, size_t n_inc, int depth, VisitedInc** visited) {
    if (depth > 32) {
        fprintf(stderr, "error: include depth limit exceeded (circular include?)\n");
        return NULL;
    }

    SB out;
    sb_init(&out);
    char* work = rook_rewrites_includes(src, src_len);
    const char* p = work;
    const char* end = work + strlen(work);

    while (p < end) {
        /* Find #include at start of line */
        if (*p == '#') {
            const char* nl = memchr(p + 1, '\n', end - p - 1);
            int line_len = nl ? (int)(nl - p + 1) : (int)(end - p);
            char line[4096];
            int copy_len = line_len < (int)sizeof(line) ? line_len : (int)sizeof(line) - 1;
            memcpy(line, p, copy_len);
            line[copy_len] = '\0';

            /* Check if this is an include directive */
            const char* c1 = strchr(line, '<');
            const char* c2 = strchr(line, '"');
            if (c1 && c2) { c1 = c1 < c2 ? c1 : c2; }
            else if (!c1) c1 = c2;

            if (c1) {
                const char* cend = c1[0] == '<' ? strchr(c1 + 1, '>') : strchr(c1 + 1, '"');
                if (cend) {
                    size_t inc_len = (size_t)(cend - c1 - 1);
                    if (inc_len < 4096) {
                        char incpath[4096];
                        memcpy(incpath, c1 + 1, inc_len);
                        incpath[inc_len] = '\0';

                        /* Check if it's a .rook source file */
                        int is_rook = (inc_len >= 5 && strcmp(incpath + inc_len - 5, ".rook") == 0);
                        if (is_rook) {
                            /* Resolve and include */
                            char* resolved = resolve_include_path(incpath, basedir, inc_dirs, n_inc);
                            if (!resolved) {
                                fprintf(stderr, "error: cannot find included file '%s'\n", incpath);
                                sb_free(&out);
                                free(work);
                                return NULL;
                            }

                            /* Deduplicate: check if already visited */
                            char canon[4096];
                            const char* track_path = realpath(resolved, canon) ? canon : resolved;
                            int seen = 0;
                            for (VisitedInc* v = *visited; v; v = v->next) {
                                if (strcmp(v->path, track_path) == 0) { seen = 1; break; }
                            }
                            if (seen) {
                                /* Already included; skip */
                                free(resolved);
                                p = nl ? nl + 1 : end;
                                continue;
                            }
                            VisitedInc* vi = malloc(sizeof *vi);
                            if (vi) {
                                snprintf(vi->path, sizeof(vi->path), "%s", track_path);
                                vi->next = *visited;
                                *visited = vi;
                            }

                            /* Read and recursively process */
                            int ilen = 0;
                            char* isrc = util_read_file(resolved, &ilen);
                            if (!isrc) {
                                fprintf(stderr, "error: cannot read included file\n");
                                sb_free(&out);
                                free(resolved);
                                free(work);
                                return NULL;
                            }
                            /* Compute basedir for nested includes */
                            char* slash = strrchr(resolved, '/');
                            char nested_dir[4096];
                            if (slash) {
                                size_t dlen = (size_t)(slash - resolved);
                                snprintf(nested_dir, sizeof(nested_dir), "%.*s", (int)dlen, resolved);
                            } else {
                                snprintf(nested_dir, sizeof(nested_dir), ".");
                            }
                            char* expanded = resolve_includes_rec(isrc, ilen, nested_dir, inc_dirs, n_inc, depth + 1, visited);
                            free(isrc);
                            free(resolved);
                            if (!expanded) {
                                sb_free(&out);
                                free(work);
                                return NULL;
                            }
                            sb_append(&out, expanded);
                            /* Ensure inlined module content ends with a newline
                               so the next source line starts on its own line */
                            if (out.len == 0 || out.data[out.len - 1] != '\n')
                                sb_append(&out, "\n");
                            free(expanded);
                            p = nl ? nl + 1 : end;
                            continue;
                        }
                    }
                }
            }
            /* Not a .rook include — pass through verbatim */
            sb_appendn(&out, p, line_len);
            p = nl ? nl + 1 : end;
        } else {
            /* Non-include line — copy until next #include or newline */
            const char* next_hash = p;
            while (next_hash < end) {
                if (next_hash[0] == '\n') { next_hash++; break; }
                if (next_hash[0] == '#') break;
                next_hash++;
            }
            sb_appendn(&out, p, (int)(next_hash - p));
            p = next_hash;
        }
    }
    char* r = sb_strdup(&out);
    sb_free(&out);
    free(work);
    return r;
}

static char* resolve_includes(const char* src, int src_len, const char* basedir,
                              const char** inc_dirs, size_t n_inc, int depth) {
    VisitedInc* visited = NULL;
    char* r = resolve_includes_rec(src, src_len, basedir, inc_dirs, n_inc, depth, &visited);
    while (visited) {
        VisitedInc* n = visited->next;
        free(visited);
        visited = n;
    }
    return r;
}

/* Forward declaration: defined later in the test command section. */
static int test_run_dir(const char* dir, int* o_pass, int* o_fail, int* o_skip);

static void usage(void) {
    printf("rokade - the Rook compiler\n");
    printf("usage:\n");
    printf("  rokade <file>             lex/parse and emit Rook source to stdout\n");
    printf("  rokade --emit-c <file>    parse and emit C to stdout\n");
    printf("  rokade --ast <file>       dump the AST\n");
    printf("  rokade --check <file>     round-trip check (parse->emit->reparse, compare ASTs)\n");
    printf("  rokade --check-dir <dir>  round-trip check every *.rook under dir (recursive)\n");
    printf("  rokade --diagnostics <file>  emit diagnostics as JSON (for LSP/editor tooling)\n");
    printf("  rokade --def-at <file> <line> <col>\n");
    printf("                              locate the symbol at 0-based (line,col) and print its\n");
    printf("                              definition as one JSON LSP Location (or `null`)\n");
    printf("  rokade --symbols <file>    list top-level definitions as JSON (for LSP outline)\n");
    printf("  rokade new <name>         create a new Rook project\n");
    printf("  rokade build [path]       build a Rook project (reads rokade.toml)\n");
    printf("  rokade run [path]         build and run a Rook project\n");
    printf("  rokade config             show effective configuration\n");
    printf("  rokade config get <key>   print one config value\n");
    printf("  rokade config set [--local] <key> <value>  set a config value\n");
    printf("  rokade toolchain          show detected C toolchain\n");
    printf("  rokade toolchain set <tool> <path>  persist cc/ar/cflags override\n");
    printf("  rokade doctor             one-shot environment health check\n");
}

/* Per-command help text. Returns the (static) detail string, or NULL if the
   command is unknown. */
static const char* help_detail(const char* cmd) {
    if (!cmd) return NULL;
    if (strcmp(cmd, "new") == 0)
        return "rokade new <name>\n  Create a new Rook project directory <name>/ with a\n  rokade.toml and a src/main.rook. Build/run with 'rokade build'.";
    if (strcmp(cmd, "build") == 0)
        return "rokade build [path]\n  Build a Rook project (reads rokade.toml). Drives the C\n  toolchain (auto-detected or set via 'rokade config/toolchain set cc').";
    if (strcmp(cmd, "run") == 0)
        return "rokade run [path]\n  Build the project then run the resulting executable.";
    if (strcmp(cmd, "config") == 0)
        return "rokade config                 show the effective merged config\n"
               "rokade config get <key>      print one config value\n"
               "rokade config set [--local] <key> <value>\n"
               "                              set a config value (project-local with --local)";
    if (strcmp(cmd, "toolchain") == 0)
        return "rokade toolchain              show the detected C compiler/archiver\n"
               "rokade toolchain set <cc|ar|cflags> <value>\n"
               "                              persist an explicit override into config";
    if (strcmp(cmd, "doctor") == 0)
        return "rokade doctor\n  One-shot health check: toolchain present, build dir writable,\n  and the corpus green. Prints PASS/WARN/FAIL.";
    if (strcmp(cmd, "emit-c") == 0 || strcmp(cmd, "--emit-c") == 0)
        return "rokade --emit-c <file>\n  Parse and emit the transpiled C source to stdout.";
    if (strcmp(cmd, "ast") == 0 || strcmp(cmd, "--ast") == 0)
        return "rokade --ast <file>\n  Dump the parsed AST to stdout.";
    if (strcmp(cmd, "check") == 0 || strcmp(cmd, "--check") == 0)
        return "rokade --check <file>\n  Round-trip check: parse, emit C, re-parse, compare ASTs.";
    if (strcmp(cmd, "--check-dir") == 0 || strcmp(cmd, "check-dir") == 0)
        return "rokade --check-dir <dir>\n  Round-trip check every *.rook under <dir> (recursive).";
    if (strcmp(cmd, "--diagnostics") == 0 || strcmp(cmd, "diagnostics") == 0)
        return "rokade --diagnostics <file>\n  Parse and type-check <file>, emitting a JSON array of\n  diagnostics: [{file, line, character, severity, message}]. line/char are\n  1-based (subtract 1 for LSP ranges). Designed for editor/LSP tooling";
    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "version") == 0)
        return "rokade --version\n  Print the rokade version.";
    if (strcmp(cmd, "help") == 0)
        return "rokade help [<command>]\n  Show general help, or detailed help for <command>.";
    return NULL;
}

static int do_help(int argc, char** argv) {
    /* argv[0] == "help" (or we were invoked via --help/-h) */
    const char* cmd = (argc >= 2) ? argv[1] : NULL;
    if (cmd && strcmp(cmd, "help") == 0) cmd = (argc >= 3) ? argv[2] : NULL;
    if (!cmd) {
        usage();
        return 0;
    }
    const char* detail = help_detail(cmd);
    if (!detail) {
        fprintf(stderr, "rokade: no help for '%s'\n", cmd);
        return 1;
    }
    printf("%s\n", detail);
    return 0;
}

/* Scan source for #include "file.rook" directives.
   Returns malloc'd array of include paths (relative to basedir), or NULL.
   Sets *n_out to the number of includes. */
static char** scan_includes(const char* src, int src_len, const char* basedir, size_t* n_out) {
    char* work = rook_rewrites_includes(src, src_len);
    int work_len = (int)strlen(work);
    char** result = NULL;
    size_t n = 0, cap = 0;
    const char* p = work;
    const char* end = work + work_len;

    while (p < end) {
        if (*p == '#') {
            const char* nl = memchr(p + 1, '\n', end - p - 1);
            int line_len = nl ? (int)(nl - p + 1) : (int)(end - p);
            char line[4096];
            int copy_len = line_len < (int)sizeof(line) ? line_len : (int)sizeof(line) - 1;
            memcpy(line, p, copy_len);
            line[copy_len] = '\0';

            const char* c1 = strchr(line, '<');
            const char* c2 = strchr(line, '"');
            if (c1 && c2) { c1 = c1 < c2 ? c1 : c2; }
            else if (!c1) c1 = c2;

            if (c1) {
                const char* cend = c1[0] == '<' ? strchr(c1 + 1, '>') : strchr(c1 + 1, '"');
                if (cend) {
                    size_t inc_len = (size_t)(cend - c1 - 1);
                    if (inc_len < 4096) {
                        char incpath[4096];
                        memcpy(incpath, c1 + 1, inc_len);
                        incpath[inc_len] = '\0';

                        int is_rook = (inc_len >= 5 && strcmp(incpath + inc_len - 5, ".rook") == 0);
                        if (is_rook) {
                            /* Check if this include resolves to a file in basedir */
                            char candidate[4096];
                            if (basedir) {
                                snprintf(candidate, sizeof(candidate), "%s/%s", basedir, incpath);
                                if (access(candidate, R_OK) == 0) {
                                    if (n >= cap) { cap = cap ? cap * 2 : 16; result = realloc(result, cap * sizeof(char*)); }
                                    result[n++] = strdup(incpath);
                                }
                            }
                        }
                    }
                }
            }
            p = nl ? nl + 1 : end;
        } else {
            const char* nl = memchr(p, '\n', end - p);
            p = nl ? nl + 1 : end;
        }
    }
    free(work);
    *n_out = n;
    return result;
}

/* ---------- project config ---------- */

typedef struct {
    char name[256];
    char version[64];
    char build_kind[32];
    char build_target[64];
    char c_standard[16];
    char libraries[16][256];
    size_t n_libraries;
    char include_dirs[16][4096];
    size_t n_include_dirs;
} ProjectConfig;

static void project_config_init(ProjectConfig* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->name, sizeof(cfg->name), "myproject");
    snprintf(cfg->version, sizeof(cfg->version), "0.1.0");
    snprintf(cfg->build_kind, sizeof(cfg->build_kind), "exe");
    snprintf(cfg->build_target, sizeof(cfg->build_target), "linux");
    snprintf(cfg->c_standard, sizeof(cfg->c_standard), "c2x");
}

static int parse_toml_line(const char* line, ProjectConfig* cfg) {
    /* skip empty lines and comments */
    const char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '#') return 0;

    /* section headers */
    if (*p == '[') return 0;

    /* key = value */
    const char* eq = strchr(p, '=');
    if (!eq) return 0;

    char key[256];
    size_t klen = (size_t)(eq - p);
    while (klen > 0 && (p[klen - 1] == ' ' || p[klen - 1] == '\t')) klen--;
    if (klen >= sizeof(key)) klen = sizeof(key) - 1;
    memcpy(key, p, klen);
    key[klen] = '\0';

    const char* val = eq + 1;
    while (*val == ' ' || *val == '\t') val++;

    /* strip quotes from string values */
    char valbuf[4096];
    if (*val == '"') {
        val++;
        const char* end = strchr(val, '"');
        if (end) {
            size_t vlen = (size_t)(end - val);
            if (vlen >= sizeof(valbuf)) vlen = sizeof(valbuf) - 1;
            memcpy(valbuf, val, vlen);
            valbuf[vlen] = '\0';
        } else {
            snprintf(valbuf, sizeof(valbuf), "%s", val);
        }
    } else if (*val == '[') {
        /* Array value: ["a", "b", ...] — parse each element */
        valbuf[0] = '\0';
        const char* cur = val + 1;
        while (*cur && *cur != ']') {
            while (*cur == ' ' || *cur == '\t') cur++;
            if (*cur == '"') {
                cur++;
                const char* end = strchr(cur, '"');
                if (end) {
                    size_t vlen = (size_t)(end - cur);
                    if (vlen >= sizeof(valbuf)) vlen = sizeof(valbuf) - 1;
                    memcpy(valbuf, cur, vlen);
                    valbuf[vlen] = '\0';
                }
                cur = end ? end + 1 : cur;
            } else {
                /* Non-string array element — read until comma or ] */
                const char* end = cur;
                while (*end && *end != ',' && *end != ']') end++;
                size_t vlen = (size_t)(end - cur);
                if (vlen >= sizeof(valbuf)) vlen = sizeof(valbuf) - 1;
                memcpy(valbuf, cur, vlen);
                valbuf[vlen] = '\0';
                cur = end;
            }
            /* Process this element */
            if (strcmp(key, "library") == 0 || strcmp(key, "libraries") == 0) {
                if (cfg->n_libraries < 16) {
                    snprintf(cfg->libraries[cfg->n_libraries++], 256, "%s", valbuf);
                }
            }
            else if (strcmp(key, "include-dir") == 0 || strcmp(key, "include-dirs") == 0) {
                if (cfg->n_include_dirs < 16) {
                    snprintf(cfg->include_dirs[cfg->n_include_dirs++], 4096, "%s", valbuf);
                }
            }
            while (*cur == ' ' || *cur == '\t') cur++;
            if (*cur == ',') cur++;
        }
        return 0; /* Array handled, don't fall through to single-value processing */
    } else {
        /* strip trailing comment */
        char* vcopy = strdup(val);
        if (!vcopy) return 0;
        char* comment = strchr(vcopy, '#');
        if (comment) *comment = '\0';
        /* trim trailing whitespace */
        size_t vlen = strlen(vcopy);
        while (vlen > 0 && (vcopy[vlen - 1] == ' ' || vcopy[vlen - 1] == '\t' || vcopy[vlen - 1] == '\n' || vcopy[vlen - 1] == '\r')) {
            vcopy[--vlen] = '\0';
        }
        snprintf(valbuf, sizeof(valbuf), "%s", vcopy);
        free(vcopy);
    }

    if (strcmp(key, "name") == 0) snprintf(cfg->name, sizeof(cfg->name), "%s", valbuf);
    else if (strcmp(key, "version") == 0) snprintf(cfg->version, sizeof(cfg->version), "%s", valbuf);
    else if (strcmp(key, "kind") == 0) snprintf(cfg->build_kind, sizeof(cfg->build_kind), "%s", valbuf);
    else if (strcmp(key, "target") == 0) snprintf(cfg->build_target, sizeof(cfg->build_target), "%s", valbuf);
    else if (strcmp(key, "c-standard") == 0) snprintf(cfg->c_standard, sizeof(cfg->c_standard), "%s", valbuf);
    return 0;
}

static int read_project_config(const char* proj_dir, ProjectConfig* cfg) {
    project_config_init(cfg);
    char toml_path[4096];
    snprintf(toml_path, sizeof(toml_path), "%s/rokade.toml", proj_dir);
    FILE* f = fopen(toml_path, "r");
    if (!f) return 0;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        parse_toml_line(line, cfg);
    }
    fclose(f);
    return cfg->name[0] != '\0';
}

/* ---------- new command ---------- */

static int do_new(const char* name) {
    for (const char* p = name; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') {
            fprintf(stderr, "error: invalid project name '%s' (use alphanumeric, _, -)\n", name);
            return 1;
        }
    }

    char proj_dir[4096];
    snprintf(proj_dir, sizeof(proj_dir), "%s", name);

    struct stat st;
    if (stat(proj_dir, &st) == 0) {
        fprintf(stderr, "error: '%s' already exists\n", proj_dir);
        return 1;
    }

    if (mkdir(proj_dir, 0755) != 0) { perror("mkdir"); return 1; }

    char subdir[4096];
    snprintf(subdir, sizeof(subdir), "%s/src", proj_dir);
    if (mkdir(subdir, 0755) != 0) { perror("mkdir src"); return 1; }

    /* Write rokade.toml */
    char fpath[4096];
    snprintf(fpath, sizeof(fpath), "%s/rokade.toml", proj_dir);
    FILE* f = fopen(fpath, "w");
    if (!f) { perror("fopen rokade.toml"); return 1; }
    fprintf(f, "[package]\n");
    fprintf(f, "name = \"%s\"\n", name);
    fprintf(f, "version = \"0.1.0\"\n");
    fprintf(f, "\n");
    fprintf(f, "# [build]\n");
    fprintf(f, "# kind = \"exe\"           # default: executable\n");
    fprintf(f, "# kind = \"static-lib\"     # static library\n");
    fprintf(f, "# kind = \"shared-lib\"     # shared library\n");
    fprintf(f, "# target = \"linux\"        # host build (default)\n");
    fprintf(f, "# c-standard = 11\n");
    fprintf(f, "# libraries = [\"m\"]\n");
    fprintf(f, "# include-dirs = [\"../shared\"]\n");
    fclose(f);

    /* Write src/main.rook */
    snprintf(fpath, sizeof(fpath), "%s/src/main.rook", proj_dir);
    f = fopen(fpath, "w");
    if (!f) { perror("fopen src/main.rook"); return 1; }
    fprintf(f, "#include <stdio.h>\n");
    fprintf(f, "\n");
    fprintf(f, "int main() {\n");
    fprintf(f, "    printf(\"Hello, Rook!\\n\");\n");
    fprintf(f, "    return 0;\n");
    fprintf(f, "}\n");
    fclose(f);

    printf("Created Rook project '%s'\n", name);
    printf("  %s/\n", name);
    printf("  ├── rokade.toml\n");
    printf("  └── src/\n");
    printf("      └── main.rook\n");
    printf("\nBuild and run: cd %s && rokade build\n", name);
    return 0;
}

/* ---------- build command ---------- */

static int do_build(const char* proj_path) {
    char cwd[4096];
    if (!proj_path) {
        if (!getcwd(cwd, sizeof(cwd))) return 1;
        proj_path = cwd;
    }

    ProjectConfig cfg;
    if (!read_project_config(proj_path, &cfg)) {
        fprintf(stderr, "error: %s/rokade.toml not found or invalid\n", proj_path);
        fprintf(stderr, "  (run 'rokade new <name>' to create a project)\n");
        return 1;
    }

    int is_lib = strcmp(cfg.build_kind, "static-lib") == 0 ||
                 strcmp(cfg.build_kind, "shared-lib") == 0;
    if (!is_lib && strcmp(cfg.build_kind, "exe") != 0) {
        fprintf(stderr, "error: unknown [build] kind '%s' (use exe, static-lib, shared-lib)\n",
                cfg.build_kind);
        return 1;
    }

    /* Collect .rook files from src/ */
    char src_dir[4096];
    snprintf(src_dir, sizeof(src_dir), "%s/src", proj_path);
    DIR* d = opendir(src_dir);
    if (!d) {
        fprintf(stderr, "error: %s/ not found\n", src_dir);
        return 1;
    }

    /* Ensure build/generated/ exists for transpiled output. */
    char gen_dir[4096];
    snprintf(gen_dir, sizeof(gen_dir), "%s/build", proj_path);
    mkdir(gen_dir, 0755);
    snprintf(gen_dir, sizeof(gen_dir), "%s/build/generated", proj_path);
    mkdir(gen_dir, 0755);

    char* cmake_src_paths[128];
    char* c_file_paths[128];
    size_t n_src = 0;
    struct dirent* entry;

    /* Pre-scan: collect filenames and find which .rook files are included.
       Included files should NOT be compiled separately (they're inlined). */
    char* all_files[128];
    size_t n_all = 0;
    while ((entry = readdir(d)) != NULL) {
        if (n_all >= 128) break;
        size_t mlen = strlen(entry->d_name);
        if (mlen < 5 || strcmp(entry->d_name + mlen - 5, ".rook") != 0) continue;
        all_files[n_all++] = strdup(entry->d_name);
    }
    /* Mark which files are included by others */
    int* is_included = calloc(n_all, sizeof(int));
    for (size_t i = 0; i < n_all; i++) {
        char fpath[4096];
        snprintf(fpath, sizeof(fpath), "%s/%s", src_dir, all_files[i]);
        int flen = 0;
        char* fsrc = util_read_file(fpath, &flen);
        if (fsrc) {
            size_t n_inc = 0;
            char** incs = scan_includes(fsrc, flen, src_dir, &n_inc);
            for (size_t j = 0; j < n_inc; j++) {
                const char* incname = incs[j];
                /* Match by basename or full name */
                const char* base = strrchr(incname, '/');
                base = base ? base + 1 : incname;
                for (size_t k = 0; k < n_all; k++) {
                    if (strcmp(all_files[k], base) == 0 || strcmp(all_files[k], incname) == 0) {
                        is_included[k] = 1;
                        break;
                    }
                }
                free(incs[j]);
            }
            free(incs);
            free(fsrc);
        }
    }

    /* Rewind directory for main compilation loop */
    closedir(d);
    d = opendir(src_dir);
    if (!d) {
        fprintf(stderr, "error: %s/ not found\n", src_dir);
        free(is_included);
        for (size_t i = 0; i < n_all; i++) free(all_files[i]);
        return 1;
    }

    while ((entry = readdir(d)) != NULL) {
        size_t mlen = strlen(entry->d_name);
        if (n_src >= 128) break;
        if (mlen < 5 || strcmp(entry->d_name + mlen - 5, ".rook") != 0) continue;

        /* Skip files that are included by others (they'll be inlined) */
        int skip = 0;
        for (size_t i = 0; i < n_all; i++) {
            if (is_included[i] && strcmp(all_files[i], entry->d_name) == 0) {
                skip = 1;
                break;
            }
        }
        if (skip) {
            printf("  skip (included): %s\n", entry->d_name);
            continue;
        }

        char rook_path[4096];
        snprintf(rook_path, sizeof(rook_path), "%s/%s", src_dir, entry->d_name);

        int len = 0;
        char* source = util_read_file(rook_path, &len);
        if (!source) { fprintf(stderr, "warning: could not read %s\n", rook_path); continue; }

        /* Resolve #include <file.rook> and #comprise directives */
        const char* inc_list[16];
        for (size_t j = 0; j < cfg.n_include_dirs; j++) inc_list[j] = cfg.include_dirs[j];
        char* expanded = resolve_includes(source, len, src_dir, inc_list, cfg.n_include_dirs, 0);
        free(source);
        if (!expanded) { fprintf(stderr, "warning: include resolution failed for %s\n", rook_path); continue; }
        len = (int)strlen(expanded);

        Sema* sema = sema_new();
        int ntoks = 0;
        Token* toks = lex_all(expanded, len, &ntoks);
        Program* p = parse_program(expanded, len, toks, ntoks);
        if (!p) {
            fprintf(stderr, "%s:%s", rook_path, parse_error());
            free(expanded);
            free(toks);
            continue;
        }
        sema_set_source(sema, expanded, len);
        sema_load_commandlist(src_dir, NULL);
        sema_collect(sema, p);
        sema_check(sema, p);
        if (sema->err) {
            fprintf(stderr, "%s:%s", rook_path, sema->err);
            program_free(p);
            free(expanded);
            free(toks);
            sema_free(sema);
            continue;
        }
        int clen = 0;
        Backend* be = backend_create("c");
        char* c_code = be->emit_program(sema, p, &clen, 0);
        backend_destroy(be);

        char c_path[4096];
        snprintf(c_path, sizeof(c_path), "%s/build/generated/%.*s.c",
                 proj_path, (int)(mlen - 5), entry->d_name);

        FILE* fout = fopen(c_path, "w");
        if (fout) {
            fwrite(c_code, 1, clen, fout);
            fclose(fout);
        }
        printf("  transpiled: %s -> %s\n", entry->d_name, c_path);

        free(expanded);
        free(toks);
        free(c_code);
        sema_free(sema);
        program_free(p);

        c_file_paths[n_src++] = strdup(c_path);
    }
    closedir(d);

    /* Cleanup pre-scan data */
    free(is_included);
    for (size_t i = 0; i < n_all; i++) free(all_files[i]);

    if (n_src == 0) {
        fprintf(stderr, "error: no .rook files found in %s\n", src_dir);
        return 1;
    }

    /* Compile generated C sources to object files using native toolchain */
    char build_dir[4096];
    snprintf(build_dir, sizeof(build_dir), "%s/build", proj_path);

    const char* inc_dirs[32];
    size_t n_inc = 0;
    char gen_inc[4096];
    snprintf(gen_inc, sizeof(gen_inc), "%s/build/generated", proj_path);
    inc_dirs[n_inc++] = gen_inc;
    for (size_t i = 0; i < cfg.n_include_dirs && n_inc < 32; i++) {
        inc_dirs[n_inc++] = cfg.include_dirs[i];
    }

    char* obj_file_paths[128];
    for (size_t i = 0; i < n_src; i++) {
        char obj_path[4096];
        const char* c_base = strrchr(c_file_paths[i], '/');
        c_base = c_base ? c_base + 1 : c_file_paths[i];
        size_t blen = strlen(c_base);
        snprintf(obj_path, sizeof(obj_path), "%s/%.*s.o",
                 build_dir, (int)(blen > 2 ? blen - 2 : blen), c_base);
        obj_file_paths[i] = strdup(obj_path);

        printf("  compiling: %s -> %s\n", c_base, obj_path);
        int ret = toolchain_compile_obj(obj_path, c_file_paths[i], inc_dirs, n_inc, NULL);
        if (ret != 0) {
            fprintf(stderr, "error: compilation failed for %s\n", c_file_paths[i]);
            for (size_t j = 0; j <= i; j++) free(obj_file_paths[j]);
            for (size_t j = 0; j < n_src; j++) free(c_file_paths[j]);
            return ret;
        }
    }

    /* Link object files */
    const char* libs[16];
    for (size_t i = 0; i < cfg.n_libraries; i++) libs[i] = cfg.libraries[i];

    char target_path[4096];
    int link_ret = 0;
    if (is_lib) {
        int is_shared = strcmp(cfg.build_kind, "shared-lib") == 0;
        snprintf(target_path, sizeof(target_path), "%s/lib%s.%s",
                 build_dir, cfg.name, is_shared ? "so" : "a");
        printf("  linking: %s\n", target_path);
        link_ret = toolchain_link_lib(target_path, (const char**)obj_file_paths, n_src, is_shared, NULL);
    } else {
        snprintf(target_path, sizeof(target_path), "%s/%s", build_dir, cfg.name);
        printf("  linking: %s\n", target_path);
        link_ret = toolchain_link_exe(target_path, (const char**)obj_file_paths, n_src, libs, cfg.n_libraries, NULL);
    }

    for (size_t i = 0; i < n_src; i++) {
        free(obj_file_paths[i]);
        free(c_file_paths[i]);
    }

    if (link_ret != 0) {
        fprintf(stderr, "error: linking failed\n");
        return link_ret;
    }

    printf("  build successful: %s\n", target_path);
    return 0;
}

/* ---------- run command ---------- */

static int do_run(const char* proj_path) {
    int ret = do_build(proj_path);
    if (ret != 0) return ret;

    ProjectConfig cfg;
    if (!read_project_config(proj_path ? proj_path : ".", &cfg)) {
        fprintf(stderr, "error: cannot read project config\n");
        return 1;
    }

    char build_dir[4096];
    if (proj_path) snprintf(build_dir, sizeof(build_dir), "%s/build", proj_path);
    else snprintf(build_dir, sizeof(build_dir), "build");

    char exe_path[4096];
    snprintf(exe_path, sizeof(exe_path), "%s/%s", build_dir, cfg.name);
    printf("running: %s\n", exe_path);

    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "%s", exe_path);
    return system(cmd);
}

/* ---------- config command ---------- */

static int do_config(int argc, char** argv) {
    /* argv[0] == "config" */
    if (argc >= 2 && strcmp(argv[1], "get") == 0) {
        if (argc < 3) {
            fprintf(stderr, "rokade: usage: rokade config get <key>\n");
            return 1;
        }
        if (!config_is_key(argv[2])) {
            fprintf(stderr, "rokade: unknown config key '%s'\n", argv[2]);
            fprintf(stderr, "rokade: valid keys: %s\n", config_keys_help());
            return 1;
        }
        RookConfig cfg;
        config_load(&cfg);
        const char* v = config_get_str(&cfg, argv[2]);
        printf("%s\n", v ? v : "");
        config_free(&cfg);
        return 0;
    }

    if (argc >= 2 && strcmp(argv[1], "set") == 0) {
        int is_local = 0;
        const char* key = NULL;
        const char* val = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--local") == 0) is_local = 1;
            else if (!key) key = argv[i];
            else if (!val) val = argv[i];
        }
        if (!key || !val) {
            fprintf(stderr, "rokade: usage: rokade config set [--local] <key> <value>\n");
            return 1;
        }
        return config_set(key, val, is_local);
    }

    if (argc >= 2) {
        fprintf(stderr, "rokade: unknown config subcommand '%s'\n", argv[1]);
        fprintf(stderr, "rokade: usage: rokade config [get <key> | set [--local] <key> <value>]\n");
        return 1;
    }

    /* Exactly `rokade config`: show the effective (merged) configuration. */
    RookConfig cfg;
    config_load(&cfg);
    config_print(&cfg);
    config_free(&cfg);
    return 0;
}

/* ---------- toolchain command ---------- */

static int do_toolchain(int argc, char** argv) {
    /* argv[0] == "toolchain" */
    if (argc >= 2 && strcmp(argv[1], "set") == 0) {
        /* `rokade toolchain set <tool> <path>` is an alias for
           `rokade config set <tool> <path>` for cc/ar/cflags. */
        const char* tool = NULL, *val = NULL;
        for (int i = 2; i < argc; i++) {
            if (!tool) tool = argv[i];
            else if (!val) val = argv[i];
        }
        if (!tool || !val) {
            fprintf(stderr, "rokade: usage: rokade toolchain set <cc|ar|cflags> <value>\n");
            return 1;
        }
        if (strcmp(tool, "cc") != 0 && strcmp(tool, "ar") != 0 &&
            strcmp(tool, "cflags") != 0) {
            fprintf(stderr, "rokade: toolchain set supports only cc, ar, cflags\n");
            return 1;
        }
        return config_set(tool, val, 0);
    }
    if (argc >= 2) {
        fprintf(stderr, "rokade: unknown toolchain subcommand '%s'\n", argv[1]);
        fprintf(stderr, "rokade: usage: rokade toolchain [set <cc|ar|cflags> <value>]\n");
        return 1;
    }

    Toolchain tc;
    if (toolchain_detect(&tc) != 0) {
        printf("toolchain: NONE detected (gcc will be assumed as a fallback)\n");
        return 0;
    }
    printf("C toolchain:\n");
    printf("  cc        : %s\n", tc.cc_path ? tc.cc_path : "(none)");
    printf("  name      : %s\n", tc.cc_name ? tc.cc_name : "?");
    printf("  vendor    : %s\n", tc.cc_vendor ? tc.cc_vendor : "unknown");
    printf("  version   : %s\n", tc.cc_version ? tc.cc_version : "?");
    printf("  c11       : %s\n", tc.supports_c11 ? "yes" : "no");
    printf("  c17       : %s\n", tc.supports_c17 ? "yes" : "no");
    printf("  c23       : %s\n", tc.supports_c23 ? "yes" : "no");
    printf("  ar        : %s\n", tc.ar_path ? tc.ar_path : "(none)");
    printf("  source    : %s\n", tc.from_config ? "user override (config)"
                                                : "auto-detected");
    toolchain_free(&tc);
    return 0;
}

/* ---------- doctor command ---------- */

/* True if `cmd` is found on PATH. */
static int has_on_path(const char* cmd) {
    const char* path = getenv("PATH");
    if (!path || !cmd || !*cmd) return 0;
    char buf[4096];
    const char* p = path;
    while (*p) {
        const char* colon = strchr(p, ':');
        size_t len = colon ? (size_t)(colon - p) : strlen(p);
        if (len > 0 && len + 1 + strlen(cmd) < sizeof buf) {
            snprintf(buf, sizeof buf, "%.*s/%s", (int)len, p, cmd);
            if (access(buf, X_OK) == 0) return 1;
        }
        if (!colon) break;
        p = colon + 1;
    }
    return 0;
}

static int cmd_doctor(void) {
    printf("rokade doctor — environment health check\n");
    printf("=========================================\n");

    int any_fail = 0;

    /* 1) toolchain */
    {
        Toolchain tc;
        int rc = toolchain_detect(&tc);
        if (rc != 0) {
            printf("[FAIL] toolchain: no C compiler found on PATH\n");
            any_fail = 1;
        } else {
            printf("[PASS] toolchain: %s (%s) — %s\n",
                   tc.cc_path ? tc.cc_path : "?",
                   tc.cc_vendor ? tc.cc_vendor : "unknown",
                   tc.cc_version ? tc.cc_version : "?");
            toolchain_free(&tc);
        }
    }

    /* 2) corpus green */
    {
        char corpus[8192];
        const char* f = __FILE__;
        const char* slash = strrchr(f, '/');
        if (slash)
            snprintf(corpus, sizeof corpus, "%.*s/../tests/corpus",
                     (int)(slash - f), f);
        else
            snprintf(corpus, sizeof corpus, "tests/corpus");
        int pass = 0, fail = 0, skip = 0;
        if (access(corpus, R_OK) != 0) {
            printf("[WARN] corpus: %s not found (skipped)\n", corpus);
        } else {
            int r = test_run_dir(corpus, &pass, &fail, &skip);
            if (r == 0) printf("[PASS] corpus: %d pass, %d skip\n", pass, skip);
            else { printf("[FAIL] corpus: %d failed\n", fail); any_fail = 1; }
        }
    }

    /* 5) Android tooling (optional on host; required to target Android) */
    {
        const char* ndk = getenv("ANDROID_NDK_HOME");
        if (!ndk || !*ndk) ndk = getenv("ANDROID_HOME");
        if (ndk && access(ndk, X_OK) == 0) {
            char probe[4096];
            snprintf(probe, sizeof probe, "%s/toolchains/llvm/prebuilt", ndk);
            if (access(probe, X_OK) == 0)
                printf("[PASS] android NDK: %s\n", ndk);
            else
                printf("[WARN] android NDK env=%s but toolchain/ not found\n", ndk);
        } else {
            printf("[WARN] android NDK: not detected (set ANDROID_NDK_HOME)\n");
        }
        if (has_on_path("cmake"))  printf("[PASS] cmake: found on PATH\n");
        else                       printf("[WARN] cmake: not on PATH\n");
        if (has_on_path("gradle")) printf("[PASS] gradle: found on PATH\n");
        else                       printf("[WARN] gradle: not on PATH (a gradlew may still work)\n");
    }

    printf("=========================================\n");
    if (any_fail) { printf("doctor: FAIL\n"); return 1; }
    printf("doctor: PASS\n");
    return 0;
}

/* ---------- test command ---------- */

/* Parse a .rook file (resolving includes) and produce C.
   Returns malloc'd C via *out (len via *out_len), or NULL on failure
   (after printing a diagnostic). */
static char* transpile_to_c(const char* path, int* out_len) {
    char* slash = strrchr(path, '/');
    char basedir[4096];
    if (slash) {
        size_t dlen = (size_t)(slash - path);
        snprintf(basedir, sizeof(basedir), "%.*s", (int)dlen, path);
    } else {
        snprintf(basedir, sizeof(basedir), ".");
    }

    int len = 0;
    char* src = util_read_file(path, &len);
    if (!src) return NULL;
    char* expanded = resolve_includes(src, len, basedir, NULL, 0, 0);
    free(src);
    if (!expanded) return NULL;
    len = (int)strlen(expanded);

    int ntoks = 0;
    Token* toks = lex_all(expanded, len, &ntoks);
    Program* p = parse_program(expanded, len, toks, ntoks);
    if (!p) {
        fprintf(stderr, "%s:%s", path, parse_error());
        free(expanded);
        free(toks);
        return NULL;
    }
    Sema* sema = sema_new();
    sema_set_source(sema, expanded, len);
    sema_load_commandlist(basedir, NULL);
    sema_collect(sema, p);
    sema_check(sema, p);
    if (sema->err) {
        fprintf(stderr, "%s:\n%s", path, sema->err);
        sema_free(sema);
        free(expanded);
        free(toks);
        program_free(p);
        *out_len = 0;
        return NULL;
    }
    int clen = 0;
    Backend* be = backend_create("c");
    char* c = be->emit_program(sema, p, &clen, 0);
    backend_destroy(be);
    sema_free(sema);
    free(expanded);
    free(toks);
    program_free(p);
    *out_len = clen;
    return c;
}

static int write_all(const char* path, const char* data, int len) {
    FILE* f = fopen(path, "w");
    if (!f) return 1;
    int w = (int)fwrite(data, 1, (size_t)len, f);
    fclose(f);
    return w != len;
}

/* Run the output-driven corpus test protocol on every *.rook in `dir`. */
static int test_run_dir(const char* dir, int* o_pass, int* o_fail, int* o_skip) {
    int pass = 0, fail = 0, skip = 0;
    char work[] = "/tmp/rook_test_XXXXXX";
    if (!mkdtemp(work)) {
        fprintf(stderr, "rokade: cannot create temp dir\n");
        return 1;
    }
    char c_path[4096];  snprintf(c_path, sizeof c_path, "%s/t.c", work);
    char exe_path[4096]; snprintf(exe_path, sizeof exe_path, "%s/t.exe", work);
    char got_path[4096]; snprintf(got_path, sizeof got_path, "%s/got", work);

    DIR* d = opendir(dir);
    if (!d) {
        fprintf(stderr, "rokade: cannot open test dir '%s'\n", dir);
        return 1;
    }
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        size_t mlen = strlen(ent->d_name);
        if (mlen < 5 || strcmp(ent->d_name + mlen - 5, ".rook") != 0) continue;
        char base[4096];
        size_t bl = (size_t)(mlen - 5);
        if (bl >= sizeof base) bl = sizeof base - 1;
        memcpy(base, ent->d_name, bl);
        base[bl] = '\0';

        char src[4096], failref[4096], outref[4096], inref[4096];
        snprintf(src, sizeof src, "%s/%s", dir, ent->d_name);
        snprintf(failref, sizeof failref, "%s/%s.fail", dir, base);
        snprintf(outref, sizeof outref, "%s/%s.out", dir, base);
        snprintf(inref, sizeof inref, "%s/%s.in", dir, base);

        /* ---- expected-error test ---- */
        if (access(failref, F_OK) == 0) {
            int clen = 0;
            char* c = transpile_to_c(src, &clen);
            if (c) {
                free(c);
                if (strcmp(base, "fail_syntax") == 0) {
                    printf("  KNOWN (gap) %s\n", base);
                } else {
                    fail++;
                    printf("  FAIL (expected error, compiled) %s\n", base);
                }
            } else {
                pass++;
                printf("  PASS (rejected) %s\n", base);
            }
            continue;
        }

        /* ---- expected-output test ---- */
        if (access(outref, F_OK) == 0) {
            int clen = 0;
            char* c = transpile_to_c(src, &clen);
            if (!c) { fail++; printf("  FAIL (emit) %s\n", base); continue; }
            if (write_all(c_path, c, clen)) { free(c); fail++; printf("  FAIL (write) %s\n", base); continue; }
            free(c);

            char cmd[8192];
            if (toolchain_compile_exe(exe_path, c_path) != 0) {
                fail++; printf("  FAIL (compile) %s\n", base); continue;
            }

            if (access(inref, F_OK) == 0)
                snprintf(cmd, sizeof cmd, "%s < %s > %s 2>/dev/null", exe_path, inref, got_path);
            else
                snprintf(cmd, sizeof cmd, "%s < /dev/null > %s 2>/dev/null", exe_path, got_path);
            if (system(cmd) != 0) { fail++; printf("  FAIL (run) %s\n", base); continue; }

            int glen = 0, elen = 0;
            char* got = util_read_file(got_path, &glen);
            char* expected = util_read_file(outref, &elen);
            int same = got && expected && glen == elen && memcmp(got, expected, (size_t)glen) == 0;
            free(got); free(expected);
            if (same) { pass++; printf("  PASS %s\n", base); }
            else { fail++; printf("  FAIL (output) %s\n", base); }
            continue;
        }

        (void)skip;
        skip++;
    }
    closedir(d);

    /* best-effort cleanup of the temp dir */
    char cmd[8192];
    snprintf(cmd, sizeof cmd, "rm -rf %s", work);
    system(cmd);

    *o_pass += pass;
    *o_fail += fail;
    *o_skip += skip;
    return fail;
}

static int cmd_test(const char* dir) {
    int pass = 0, fail = 0, skip = 0;
    int r = test_run_dir(dir, &pass, &fail, &skip);
    printf("\n---------------------------\n");
    printf("PASS         : %d\n", pass);
    printf("FAIL         : %d\n", fail);
    printf("SKIP         : %d\n", skip);
    printf("---------------------------\n");
    if (r) return 1;
    return pass == 0 ? 1 : 0;
}

/* force a reference to util_endswith to avoid an unused warning if unused */
static int dummy_endswith(void) { return util_endswith("x", "y"); }

static int check_file(const char* path, int verbose) {
    int len = 0;
    char* src = util_read_file(path, &len);
    if (!src) {
        printf("FAIL %s (cannot read)\n", path);
        return 1;
    }

    /* Resolve #include <file.rook> directives */
    char* slash = strrchr(path, '/');
    char basedir[4096];
    if (slash) {
        size_t dlen = (size_t)(slash - path);
        snprintf(basedir, sizeof(basedir), "%.*s", (int)dlen, path);
    } else {
        snprintf(basedir, sizeof(basedir), ".");
    }
    char* expanded = resolve_includes(src, len, basedir, NULL, 0, 0);
    free(src);
    if (!expanded) {
        printf("FAIL %s (include resolution)\n", path);
        return 1;
    }
    len = (int)strlen(expanded);

    int ntoks = 0;
    Token* toks = lex_all(expanded, len, &ntoks);
    Program* p1 = parse_program(expanded, len, toks, ntoks);
    if (!p1) {
        printf("FAIL %s (parse)\n%s", path, parse_error());
        free(expanded);
        free(toks);
        return 1;
    }
    int elen = 0;
    char* emitted = emit_program(p1, &elen);

    int ntoks2 = 0;
    Token* toks2 = lex_all(emitted, elen, &ntoks2);
    Program* p2 = parse_program(emitted, elen, toks2, ntoks2);
    int ok = 0;
    if (!p2) {
        printf("FAIL %s (reparse)\n%s", path, parse_error());
    } else {
        ok = program_eq(p1, p2);
        int byte = elen == len && memcmp(emitted, expanded, len) == 0;
        if (verbose || !ok) {
            printf("%s %s (%d items%s)\n", path, ok ? "PASS" : "FAIL", p1->nitems,
                   byte ? ", byte-identical" : "");
        }
        if (!ok) {
            fprintf(stderr, "--- original AST: %s\n", path);
            ast_dump(p1);
            fprintf(stderr, "--- re-parsed AST:\n");
            ast_dump(p2);
        }
    }
    free(expanded);
    free(emitted);
    free(toks);
    free(toks2);
    program_free(p1);
    program_free(p2);
    return ok ? 0 : 1;
}

static int check_dir(const char* dir) {
    int total = 0, failed = 0;
    DIR* d = opendir(dir);
    if (!d) {
        printf("FAIL %s (cannot open dir)\n", dir);
        return 1;
    }
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            int r = check_dir(path);
            total += r;
            failed += r;
            continue;
        }
        if (!util_endswith(ent->d_name, ".rook")) continue;
        total++;
        if (check_file(path, 0) != 0) failed++;
    }
    closedir(d);
    if (failed == 0) {
        printf("%-70s %d files, all PASS\n", dir, total);
    } else {
        printf("%-70s %d files, %d FAILED\n", dir, total, failed);
    }
    return failed;
}

/* ── --diagnostics JSON helpers ─────────────────────────── */

/* Append `s` as a JSON string literal to `out` (escaping special chars). */
static void json_append_str(SB* out, const char* s) {
    sb_append(out, "\"");
    for (const char* p = s ? s : ""; *p; p++) {
        unsigned char c = (unsigned char)*p;
        char esc[7];
        int n = 0;
        if (c == '"') { esc[0]='\\'; esc[1]='"'; n=2; }
        else if (c == '\\') { esc[0]='\\'; esc[1]='\\'; n=2; }
        else if (c == '\n') { esc[0]='\\'; esc[1]='n'; n=2; }
        else if (c == '\r') { esc[0]='\\'; esc[1]='r'; n=2; }
        else if (c == '\t') { esc[0]='\\'; esc[1]='t'; n=2; }
        else if (c < 0x20) { n = snprintf(esc, sizeof esc, "\\u%04x", c); }
        if (n) sb_appendn(out, esc, n);
        else sb_appendn(out, p, 1);
    }
    sb_append(out, "\"");
}

/* Parse a diag_render line ("<line>:<col>: <kind>: <msg>") and append one
   JSON diagnostic object to `out`. Emits a leading comma only when `out`
   already holds content past the opening "[". Line/character are 1-based to
   match rokade's own `file:line:col:` text format. */
static void emit_diag_json(SB* out, const char* file, const char* diag) {
    int line = 1, col = 1;
    const char* msg = diag ? diag : "";
    char* e1;
    long lv = strtol(diag ? diag : "0", &e1, 10);
    if (e1 != (diag ? diag : "0") && *e1 == ':') {
        const char* p = e1 + 1;
        char* e2;
        long cv = strtol(p, &e2, 10);
        if (e2 != p && *e2 == ':') { line = (int)lv; col = (int)cv; msg = e2 + 1; }
    }
    const char* kerr = strstr(msg, "error:");
    const char* kw = kerr;
    const char* severity = "error";
    const char* kwarr = strstr(msg, "warning:");
    if (kwarr && (!kw || kwarr < kw)) { kw = kwarr; severity = "warning"; }
    if (kw) msg = kw + strlen(severity) + 1; /* skip "error:"/"warning:" */
    while (*msg == ' ' || *msg == '\t') msg++;
    size_t mlen = strcspn(msg, "\r\n");

    if (out->len > 1) sb_append(out, ",");
    sb_append(out, "{\"file\":");
    json_append_str(out, file);
    sb_appendf(out, ", \"line\":%d, \"character\":%d, \"severity\":\"%s\", \"message\":",
               line, col, severity);
    char* m = malloc(mlen + 1);
    memcpy(m, msg, mlen); m[mlen] = '\0';
    json_append_str(out, m);
    free(m);
    sb_append(out, "}");
}

/* ── --def-at (goto-definition) helpers ─────────────────────── */

/* Write `path` as a file:/// URI into buf. */
static void path_to_uri(const char* path, char* buf, size_t cap) {
    const char* prefix = "file://";
    if (path[0] == '/') {
        snprintf(buf, cap, "%s%s", prefix, path);   /* -> file:///abs/... */
    } else {
        char abs[4096];
        if (realpath(path, abs))
            snprintf(buf, cap, "%s%s", prefix, abs);
        else
            snprintf(buf, cap, "%s%s", prefix, path);
    }
}

static void walk_expr_ident(Expr* e, int L, int C, Expr** out);
static void walk_stmt_ident(Stmt* s, int L, int C, Expr** out);
static void walk_decl_ident(Decl* d, int L, int C, Expr** out);
static void walk_item_ident(Item* it, int L, int C, Expr** out);

/* If `e` is the E_IDENT spanning 1-based position (L,C), record it. */
static void consider_ident(Expr* e, int L, int C, Expr** out) {
    if (*out) return;
    if (e && e->kind == E_IDENT && e->line == L
        && C >= e->col && C < e->col + e->len) {
        *out = e;
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
        if (it->fn && it->fn->body) walk_stmt_ident(it->fn->body, L, C, out);
        break;
    case TOP_STRUCT:
        for (int i = 0; i < it->st->nfields && !*out; i++)
            walk_expr_ident(it->st->fields[i].dim, L, C, out);
        break;
    case TOP_IMPL:
        for (int i = 0; i < it->im->nmethods && !*out; i++)
            if (it->im->methods[i]->body)
                walk_stmt_ident(it->im->methods[i]->body, L, C, out);
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

/* Resolve the 1-based source location of the def target carried by an E_IDENT
   (see DefKind). `def` is an AST-owned node. Returns 0 on success, 1 if the
   symbol has no source definition. */
static int def_location(DefKind kind, void* def, int* line, int* col) {
    if (!def) return 1;
    switch (kind) {
    case DEF_FN:      { FnDef* f = (FnDef*)def; *line = f->line; *col = f->col; return 0; }
    case DEF_STRUCT:  { StructDef* s = (StructDef*)def; *line = s->line; *col = s->col; return 0; }
    case DEF_ENUM:    { EnumDef* e = (EnumDef*)def; *line = e->line; *col = e->col; return 0; }
    case DEF_VARIANT: { EnumVariant* v = (EnumVariant*)def; *line = v->line; *col = v->col; return 0; }
    case DEF_VAR:     { Decl* d = (Decl*)def; *line = d->line; *col = d->col; return 0; }
    default: break;
    }
    return 1;
}

/* --def-at <file> <line> <col>: print one JSON LSP Location (0-based ranges)
   for the definition of the symbol under the cursor, or `null`. Positions are
   0-based (LSP-native); they're converted to 1-based to match AST nodes. */
static int do_def_at(int argc, char** argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: rokade --def-at <file> <line> <col>\n"
                        "  line and col are 0-based (LSP positions)\n");
        return 1;
    }
    const char* path = argv[2];
    int qline = atoi(argv[3]);
    int qcol  = atoi(argv[4]);
    if (qline < 0) qline = 0;
    if (qcol < 0) qcol = 0;

    int len = 0;
    char* src = util_read_file(path, &len);
    if (!src) {
        fprintf(stderr, "rokade: cannot read '%s'\n", path);
        return 1;
    }
    char basedir[4096];
    const char* slash = strrchr(path, '/');
    if (slash) {
        size_t dlen = (size_t)(slash - path);
        snprintf(basedir, sizeof(basedir), "%.*s", (int)dlen, path);
    } else {
        snprintf(basedir, sizeof(basedir), ".");
    }
    sema_load_commandlist(basedir, NULL);

    char* expanded = resolve_includes(src, len, basedir, NULL, 0, 0);
    free(src);
    if (!expanded) {
        fprintf(stderr, "rokade: error resolving includes in '%s'\n", path);
        return 1;
    }
    int elen = (int)strlen(expanded);
    int ntoks = 0;
    Token* toks = lex_all(expanded, elen, &ntoks);
    Program* p = parse_program(expanded, elen, toks, ntoks);
    if (!p) {
        printf("null\n");
        free(expanded); free(toks);
        return 0;
    }
    Sema* sema = sema_new();
    sema_set_source(sema, expanded, elen);
    sema_collect(sema, p);
    sema_check(sema, p);

    Expr* id = find_ident_at(p, qline + 1, qcol + 1);
    int dl = 0, dc = 0;
    if (id && id->def_kind != DEF_NONE && id->def
        && def_location(id->def_kind, id->def, &dl, &dc) == 0) {
        char uri[4096];
        path_to_uri(path, uri, sizeof(uri));
        printf("{\"uri\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
               "\"end\":{\"line\":%d,\"character\":%d}}}\n",
               uri, dl - 1, dc - 1, dl - 1, dc - 1);
    } else {
        printf("null\n");
    }
    sema_free(sema);
    program_free(p);
    free(expanded);
    free(toks);
    return 0;
}

/* ── --symbols <file>: list top-level definitions for editor outlines ── */
static int do_symbols(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: rokade --symbols <file>\n");
        return 1;
    }
    const char* path = argv[2];
    int len = 0;
    char* src = util_read_file(path, &len);
    if (!src) {
        fprintf(stderr, "rokade: cannot read '%s'\n", path);
        return 1;
    }
    char basedir[4096];
    const char* slash = strrchr(path, '/');
    if (slash) {
        size_t dlen = (size_t)(slash - path);
        snprintf(basedir, sizeof(basedir), "%.*s", (int)dlen, path);
    } else {
        snprintf(basedir, sizeof(basedir), ".");
    }
    char* expanded = resolve_includes(src, len, basedir, NULL, 0, 0);
    free(src);
    if (!expanded) {
        fprintf(stderr, "rokade: error resolving includes in '%s'\n", path);
        return 1;
    }
    int ntoks = 0;
    Token* toks = lex_all(expanded, (int)strlen(expanded), &ntoks);
    Program* p = parse_program(expanded, (int)strlen(expanded), toks, ntoks);
    printf("[");
    if (p) {
        int emitted = 0;
        for (int i = 0; i < p->nitems; i++) {
            Item* it = p->items[i];
            const char* name = NULL;
            const char* kind = NULL;
            int line = 0, col = 0;
            switch (it->kind) {
            case TOP_FN:
                if (!it->fn) continue;
                name = it->fn->name; kind = "fn";
                line = it->fn->line; col = it->fn->col;
                break;
            case TOP_STRUCT:
                name = it->st->name; kind = "struct";
                line = it->st->line; col = it->st->col;
                break;
            case TOP_ENUM:
                name = it->ed->name; kind = "enum";
                line = it->ed->line; col = it->ed->col;
                break;
            case TOP_IMPL:
                name = "impl"; kind = "impl";
                line = it->im->line; col = it->im->col;
                break;
            default:
                continue;
            }
            /* Commas follow the previous EMITTED symbol, not the raw item index
             * (skipped items like #include passthroughs must not emit one). */
            printf("%s{\"name\":\"%s\",\"kind\":\"%s\",\"line\":%d,\"col\":%d}",
                   emitted ? "," : "", name, kind, line, col);
            emitted++;
        }
    }
    printf("]\n");
    program_free(p);
    free(expanded);
    free(toks);
    return 0;
}

int main(int argc, char** argv) {
    /* Color mode from config (default auto). */
    {
        RookConfig cfg;
        config_load(&cfg);
        DiagColorMode m = DIAG_AUTO;
        if (strcmp(cfg.color, "always") == 0) m = DIAG_ALWAYS;
        else if (strcmp(cfg.color, "never") == 0) m = DIAG_NEVER;
        diag_init(m);
        config_free(&cfg);
    }

    if (argc < 2) {
        usage();
        return 0;
    }

    /* Top-level flags / meta commands. */
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "version") == 0) {
        printf("rokade %s\n", ROKADE_VERSION);
        return 0;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "help") == 0) {
        return do_help(argc - 1, argv + 1);
    }

    /* Project management commands */
    if (strcmp(argv[1], "new") == 0) {
        if (argc < 3) {
            fprintf(stderr, "error: missing project name\n");
            printf("Usage: rokade new <project-name>\n");
            return 1;
        }
        return do_new(argv[2]);
    }

    if (strcmp(argv[1], "build") == 0) {
        const char* proj_path = (argc > 2) ? argv[2] : NULL;
        return do_build(proj_path);
    }

    if (strcmp(argv[1], "run") == 0) {
        const char* proj_path = (argc > 2) ? argv[2] : NULL;
        return do_run(proj_path);
    }

    if (strcmp(argv[1], "config") == 0) {
        return do_config(argc - 1, argv + 1);
    }

    if (strcmp(argv[1], "toolchain") == 0) {
        return do_toolchain(argc - 1, argv + 1);
    }

    if (strcmp(argv[1], "doctor") == 0) {
        return cmd_doctor();
    }
    if (strcmp(argv[1], "--def-at") == 0 || strcmp(argv[1], "def-at") == 0) {
        return do_def_at(argc, argv);
    }
    if (strcmp(argv[1], "--symbols") == 0 || strcmp(argv[1], "symbols") == 0) {
        return do_symbols(argc, argv);
    }

    /* File processing commands */
    int mode = 0; /* 0 emit, 1 emit-c, 2 ast, 3 check, 4 checkdir, 5 diagnostics */
    int bounds_check = 0;
    int i = 1;
    /* Parse flags before the mode keyword */
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bounds-check") == 0) {
            bounds_check = 1;
            i++;
        } else {
            break;
        }
    }
    if (i < argc && strcmp(argv[i], "--emit-c") == 0) { mode = 1; i++; }
    else if (i < argc && strcmp(argv[i], "--ast") == 0) { mode = 2; i++; }
    else if (i < argc && strcmp(argv[i], "--check") == 0) { mode = 3; i++; }
    else if (i < argc && strcmp(argv[i], "--check-dir") == 0) { mode = 4; i++; }
    else if (i < argc && (strcmp(argv[i], "--diagnostics") == 0 ||
                          strcmp(argv[i], "diagnostics") == 0)) { mode = 5; i++; }

    if (mode == 4) {
        int failed = 0;
        if (i >= argc) {
            fprintf(stderr, "rokade: --check-dir needs a directory\n");
            return 1;
        }
        for (; i < argc; i++) failed += check_dir(argv[i]);
        printf(failed == 0 ? "ALL CORPUS PASS\n" : "%d files FAILED\n", failed);
        return failed == 0 ? 0 : 1;
    }

    if (i >= argc) {
        usage();
        return 1;
    }
    const char* path = argv[i];

    int len = 0;
    char* src = util_read_file(path, &len);
    if (!src) {
        fprintf(stderr, "rokade: cannot read '%s'\n", path);
        return 1;
    }

    /* Resolve #include <file.rook> directives */
    char* slash = strrchr(path, '/');
    char basedir[4096];
    if (slash) {
        size_t dlen = (size_t)(slash - path);
        snprintf(basedir, sizeof(basedir), "%.*s", (int)dlen, path);
    } else {
        snprintf(basedir, sizeof(basedir), ".");
    }
    char* expanded = resolve_includes(src, len, basedir, NULL, 0, 0);
    free(src);
    if (!expanded) {
        fprintf(stderr, "rokade: error resolving includes in '%s'\n", path);
        return 1;
    }
    len = (int)strlen(expanded);

    int ntoks = 0;
    Token* toks = lex_all(expanded, len, &ntoks);
    Program* p = parse_program(expanded, len, toks, ntoks);

    if (mode == 5) {
        /* --diagnostics: emit a JSON array of diagnostics (machine output).
           Color is disabled so the diag text is clean and parseable. */
        diag_init(DIAG_NEVER);
        SB jbuf; sb_init(&jbuf);
        sb_append(&jbuf, "[");
        if (!p) {
            emit_diag_json(&jbuf, path, parse_error());
        } else {
            Sema* sema = sema_new();
            sema_set_source(sema, expanded, len);
            sema_load_commandlist(basedir, NULL);
            sema_collect(sema, p);
            sema_check(sema, p);
            if (sema->err) emit_diag_json(&jbuf, path, sema->err);
            sema_free(sema);
        }
        sb_append(&jbuf, "]");
        fputs(jbuf.data, stdout);
        sb_free(&jbuf);
        free(expanded);
        free(toks);
        if (p) program_free(p);
        return 0;
    }

    if (!p) {
        fprintf(stderr, "%s:%s", path, parse_error());
        free(expanded);
        free(toks);
        return 1;
    }

    if (mode == 2) {
        ast_dump(p);
        free(expanded);
        free(toks);
        program_free(p);
        return 0;
    }
    if (mode == 3) {
        free(expanded);
        free(toks);
        program_free(p);
        return check_file(path, 1);
    }
    if (mode == 1) {
        Sema* sema = sema_new();
            sema_set_source(sema, expanded, len);
        sema_load_commandlist(basedir, NULL);
        sema_collect(sema, p);
        sema_check(sema, p);
        if (sema->err) {
            fprintf(stderr, "%s:%s", path, sema->err);
            sema_free(sema);
            free(expanded);
            free(toks);
            program_free(p);
            return 1;
        }
        int elen = 0;
        Backend* be = backend_create("c");
        char* c = be->emit_program(sema, p, &elen, bounds_check);
        backend_destroy(be);
        fwrite(c, 1, elen, stdout);
        free(c);
        sema_free(sema);
        free(expanded);
        free(toks);
        program_free(p);
        return 0;
    }

    int elen = 0;
    char* emitted = emit_program(p, &elen);
    fwrite(emitted, 1, elen, stdout);
    free(emitted);
    free(expanded);
    free(toks);
    program_free(p);
    return 0;
}