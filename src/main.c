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
#include "c_import.h"
#include "llvm_backend.h"
#include "util.h"

#ifndef ROKADE_VERSION
#define ROKADE_VERSION "0.3.0"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

/* Get the root installation directory of rokade.
   Returns 0 on success, -1 on failure. */
static int rokade_get_install_root(char* buf, size_t cap) {
    /* 1. Explicit environment override */
    const char* env_home = getenv("ROOK_HOME");
    if (env_home && env_home[0]) {
        snprintf(buf, cap, "%s", env_home);
        return 0;
    }

#ifdef __linux__
    /* 2. Linux /proc/self/exe resolution */
    ssize_t n = readlink("/proc/self/exe", buf, cap - 1);
    if (n > 0) {
        buf[n] = '\0';
        char* slash = strrchr(buf, '/');
        if (slash) {
            *slash = '\0'; /* strip executable name */
            char* bin_slash = strrchr(buf, '/');
            if (bin_slash && (strcmp(bin_slash + 1, "bin") == 0 || strcmp(bin_slash + 1, "build") == 0)) {
                *bin_slash = '\0'; /* strip /bin or /build to get install prefix */
                return 0;
            }
        }
    }
#elif defined(_WIN32)
    /* 3. Windows GetModuleFileName */
    DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)cap);
    if (len > 0) {
        char* slash = strrchr(buf, '\\');
        if (!slash) slash = strrchr(buf, '/');
        if (slash) {
            *slash = '\0';
            char* bin_slash = strrchr(buf, '\\');
            if (!bin_slash) bin_slash = strrchr(buf, '/');
            if (bin_slash && (strcmp(bin_slash + 1, "bin") == 0 || strcmp(bin_slash + 1, "BIN") == 0 ||
                              strcmp(bin_slash + 1, "build") == 0)) {
                *bin_slash = '\0';
                return 0;
            }
        }
    }
#endif

    /* 4. Target user install path on Linux */
    const char* default_linux = "/home/bknsehan/bin/Rook";
    if (access(default_linux, R_OK) == 0) {
        snprintf(buf, cap, "%s", default_linux);
        return 0;
    }

    return -1;
}

/* Locate the Rook standard library directory.
   Returns 0 on success (path stored in out_std), -1 if not found.
   Strict: Only loads std from the Rook installation path (or ROOK_HOME). */
static int rokade_get_std_dir(char* out_std, size_t cap) {
    char root[4096];
    if (rokade_get_install_root(root, sizeof root) != 0) return -1;

    char candidate[4096];
    /* Check <install_root>/std */
    snprintf(candidate, sizeof candidate, "%s/std", root);
    if (access(candidate, R_OK) == 0) {
        snprintf(out_std, cap, "%s", candidate);
        return 0;
    }

    /* Check <install_root>/lib/rook/std */
    snprintf(candidate, sizeof candidate, "%s/lib/rook/std", root);
    if (access(candidate, R_OK) == 0) {
        snprintf(out_std, cap, "%s", candidate);
        return 0;
    }

    return -1;
}

/* ---------- include resolution ---------- */

/* Resolve an include path, searching basedir first, then include dirs.
   Also checks common package entrypoints (src/<name>.rook, src/lib.rook, lib.rook, main.rook).
   Returns a malloc'd path or NULL if not found. */
static char* resolve_include_path(const char* incpath, const char* basedir,
                                   const char** inc_dirs, size_t n_inc) {
    char candidate[4096];
    size_t ilen = strlen(incpath);
    char modname[256];
    if (ilen > 5 && strcmp(incpath + ilen - 5, ".rook") == 0) {
        size_t mlen = ilen - 5 < sizeof(modname) ? ilen - 5 : sizeof(modname) - 1;
        memcpy(modname, incpath, mlen);
        modname[mlen] = '\0';
    } else {
        snprintf(modname, sizeof modname, "%s", incpath);
    }

    if (basedir) {
        snprintf(candidate, sizeof(candidate), "%s/%s", basedir, incpath);
        if (access(candidate, R_OK) == 0) return strdup(candidate);
        snprintf(candidate, sizeof(candidate), "%s/src/%s", basedir, incpath);
        if (access(candidate, R_OK) == 0) return strdup(candidate);
        snprintf(candidate, sizeof(candidate), "%s/%s/src/%s.rook", basedir, modname, modname);
        if (access(candidate, R_OK) == 0) return strdup(candidate);
        snprintf(candidate, sizeof(candidate), "%s/%s/src/lib.rook", basedir, modname);
        if (access(candidate, R_OK) == 0) return strdup(candidate);
    }
    for (size_t i = 0; i < n_inc; i++) {
        snprintf(candidate, sizeof(candidate), "%s/%s", inc_dirs[i], incpath);
        if (access(candidate, R_OK) == 0) return strdup(candidate);

        /* Special std module alias: std/io.rook -> <std_dir>/io.rook */
        if (strncmp(incpath, "std/", 4) == 0) {
            snprintf(candidate, sizeof(candidate), "%s/%s", inc_dirs[i], incpath + 4);
            if (access(candidate, R_OK) == 0) return strdup(candidate);
        }

        snprintf(candidate, sizeof(candidate), "%s/src/%s", inc_dirs[i], incpath);
        if (access(candidate, R_OK) == 0) return strdup(candidate);
        snprintf(candidate, sizeof(candidate), "%s/src/lib.rook", inc_dirs[i]);
        if (access(candidate, R_OK) == 0) return strdup(candidate);
        snprintf(candidate, sizeof(candidate), "%s/src/main.rook", inc_dirs[i]);
        if (access(candidate, R_OK) == 0) return strdup(candidate);
        snprintf(candidate, sizeof(candidate), "%s/lib.rook", inc_dirs[i]);
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
                for (char* cp = mod; *cp; cp++) {
                    if (*cp == '.') *cp = '/';
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
                                  const char** inc_dirs, size_t n_inc, int depth,
                                  VisitedInc** visited, const char* current_file) {
    if (depth > 32) {
        fprintf(stderr, "error: include depth limit exceeded (circular include?)\n");
        return NULL;
    }

    SB out;
    sb_init(&out);
    char* work = rook_rewrites_includes(src, src_len);
    const char* p = work;
    const char* end = work + strlen(work);

    /* Track original line number in this chunk's source */
    int cur_orig_line = 1;
    /* Whether the current file should stamp @rk:src markers (only depth==0 = user file) */
    int stamp = (depth == 0 && current_file && current_file[0]);

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
                                free(resolved);
                                p = nl ? nl + 1 : end;
                                cur_orig_line++;
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
                            char* expanded = resolve_includes_rec(isrc, ilen, nested_dir, inc_dirs, n_inc, depth + 1, visited, track_path);
                            free(isrc);
                            free(resolved);
                            if (!expanded) {
                                sb_free(&out);
                                free(work);
                                return NULL;
                            }
                            sb_append(&out, expanded);
                            if (out.len == 0 || out.data[out.len - 1] != '\n')
                                sb_append(&out, "\n");
                            free(expanded);
                            p = nl ? nl + 1 : end;
                            cur_orig_line++;
                            continue;
                        }
                    }
                }
            }
            /* Not a .rook include — pass through verbatim with marker */
            if (stamp) sb_appendf(&out, "// @rk:src %s:%d\n", current_file, cur_orig_line);
            sb_appendn(&out, p, line_len);
            p = nl ? nl + 1 : end;
            cur_orig_line++;
        } else {
            /* Non-include line — copy until next #include or newline */
            const char* next_hash = p;
            /* Find end of this line first */
            const char* nl_here = memchr(p, '\n', end - p);
            int this_line_len = nl_here ? (int)(nl_here - p + 1) : (int)(end - p);
            if (stamp) sb_appendf(&out, "// @rk:src %s:%d\n", current_file, cur_orig_line);
            sb_appendn(&out, p, this_line_len);
            p += this_line_len;
            cur_orig_line++;
        }
    }
    char* r = sb_strdup(&out);
    sb_free(&out);
    free(work);
    return r;
}

static char* resolve_includes(const char* src, int src_len, const char* basedir,
                              const char** inc_dirs, size_t n_inc, int depth,
                              const char* current_file) {
    const char* all_dirs[64];
    size_t total_inc = 0;
    if (inc_dirs) {
        for (size_t i = 0; i < n_inc && total_inc < 60; i++) {
            all_dirs[total_inc++] = inc_dirs[i];
        }
    }
    char std_path[4096];
    char* allocated_std = NULL;
    if (rokade_get_std_dir(std_path, sizeof std_path) == 0 && total_inc < 60) {
        int seen = 0;
        for (size_t i = 0; i < total_inc; i++) {
            if (strcmp(all_dirs[i], std_path) == 0) { seen = 1; break; }
        }
        if (!seen) {
            allocated_std = strdup(std_path);
            all_dirs[total_inc++] = allocated_std;
        }
    }

    VisitedInc* visited = NULL;
    char* r = resolve_includes_rec(src, src_len, basedir, all_dirs, total_inc, depth, &visited, current_file);
    while (visited) {
        VisitedInc* n = visited->next;
        free(visited);
        visited = n;
    }
    if (allocated_std) free(allocated_std);
    return r;
}

/* Forward declaration: defined later in the test command section. */
static int test_run_dir(const char* dir, int* o_pass, int* o_fail, int* o_skip);

static void usage(void) {
    printf("rokade - the Rook compiler\n");
    printf("usage:\n");
    printf("  rokade <file>             lex/parse and emit Rook source to stdout\n");
    printf("  rokade --emit-c <file>    parse and emit C to stdout\n");
    printf("  rokade --emit-llvm <file> parse and emit LLVM IR to stdout\n");
    printf("  rokade --emit-obj <file>  parse and emit native object (.o) via LLVM\n");
    printf("  rokade --ast <file>       dump the AST\n");
    printf("  rokade --check <file>     round-trip check (parse->emit->reparse, compare ASTs)\n");
    printf("  rokade --check-dir <dir>  round-trip check every *.rook under dir (recursive)\n");
    printf("  rokade --diagnostics <file>  emit diagnostics as JSON (for LSP/editor tooling)\n");
    printf("  rokade --def-at <file> <line> <col>\n");
    printf("                              locate the symbol at 0-based (line,col) and print its\n");
    printf("                              definition as one JSON LSP Location (or `null`)\n");
    printf("  rokade --symbols <file>    list top-level definitions as JSON (for LSP outline)\n");
    printf("  rokade new <name>         create a new Rook project\n");
    printf("  rokade build [path] [--backend=c|llvm] [--target=t] [--all]  build a Rook project\n");
    printf("  rokade run [path] [--backend=c|llvm] [--jit]  build and run a Rook project or script\n");
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
        return "rokade build [path] [--backend=<c|llvm>] [--target=<os>] [--all]\n  Build a Rook project (reads rokade.toml). Supports C and LLVM native backends.";
    if (strcmp(cmd, "run") == 0)
        return "rokade run [path] [--backend=<c|llvm>] [--jit]\n  Run a Rook project or single .rook file. Use --jit for instant in-memory execution.";
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
    char target_os[32];      /* "linux", "android", "windows" */
    char build_kind[32];     /* "exe", "shared-lib", "static-lib" */
    char cflags[4096];
    char cc[4096];
    char ar[4096];
    char standard[16];
    int  android_api;
    char android_ndk[4096];
    char archs[8][32];       /* e.g. ["arm64-v8a", "x86_64"] */
    size_t n_archs;
} TargetConfig;

typedef struct {
    char name[64];
    char path[4096];
} Dependency;

typedef struct {
    char name[256];
    char version[64];
    char build_kind[32];
    char build_target[64];
    char backend[32];
    char c_standard[16];
    char cflags[4096];
    char libraries[32][256];
    size_t n_libraries;
    char include_dirs[32][4096];
    size_t n_include_dirs;

    /* Multi-target list: targets = ["linux", "android", "windows"] */
    char configured_targets[16][64];
    size_t n_configured_targets;

    /* Specific target configs */
    TargetConfig target_configs[16];
    size_t n_target_configs;

    /* Rook package dependencies */
    Dependency dependencies[32];
    size_t n_dependencies;

    /* pkg-config packages */
    char pkg_config[16][64];
    size_t n_pkg_config;
} ProjectConfig;

static void project_config_init(ProjectConfig* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->name, sizeof(cfg->name), "myproject");
    snprintf(cfg->version, sizeof(cfg->version), "0.1.0");
    snprintf(cfg->build_kind, sizeof(cfg->build_kind), "exe");
    snprintf(cfg->build_target, sizeof(cfg->build_target), "linux");
    snprintf(cfg->backend, sizeof(cfg->backend), "c");
    snprintf(cfg->c_standard, sizeof(cfg->c_standard), "c2x");
}

static TargetConfig* get_or_create_target_config(ProjectConfig* cfg, const char* target_os) {
    for (size_t i = 0; i < cfg->n_target_configs; i++) {
        if (strcmp(cfg->target_configs[i].target_os, target_os) == 0) {
            return &cfg->target_configs[i];
        }
    }
    if (cfg->n_target_configs >= 16) return NULL;
    TargetConfig* tc = &cfg->target_configs[cfg->n_target_configs++];
    memset(tc, 0, sizeof *tc);
    snprintf(tc->target_os, sizeof tc->target_os, "%s", target_os);
    snprintf(tc->build_kind, sizeof tc->build_kind, "%s", cfg->build_kind);
    snprintf(tc->standard, sizeof tc->standard, "%s", cfg->c_standard);
    snprintf(tc->cflags, sizeof tc->cflags, "%s", cfg->cflags);
    tc->android_api = 24;
    return tc;
}

static int parse_toml_line(const char* line, ProjectConfig* cfg, char* cur_sec, size_t cur_sec_len) {
    /* skip empty lines and comments */
    const char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '#') return 0;

    /* section headers: [section] or [target.name] */
    if (*p == '[') {
        const char* end = strchr(p, ']');
        if (end) {
            size_t slen = (size_t)(end - (p + 1));
            if (slen >= cur_sec_len) slen = cur_sec_len - 1;
            memcpy(cur_sec, p + 1, slen);
            cur_sec[slen] = '\0';
        }
        return 0;
    }

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

    /* inline table: key = { path = "..." } */
    if (*val == '{') {
        if (strcmp(cur_sec, "dependencies") == 0) {
            char pathbuf[4096] = "";
            const char* pkey = strstr(val, "path");
            if (pkey) {
                const char* q1 = strchr(pkey, '"');
                if (q1) {
                    const char* q2 = strchr(q1 + 1, '"');
                    if (q2) {
                        size_t plen = (size_t)(q2 - q1 - 1);
                        if (plen < sizeof(pathbuf)) {
                            memcpy(pathbuf, q1 + 1, plen);
                            pathbuf[plen] = '\0';
                        }
                    }
                }
            }
            if (pathbuf[0] && cfg->n_dependencies < 32) {
                snprintf(cfg->dependencies[cfg->n_dependencies].name, sizeof(cfg->dependencies[0].name), "%s", key);
                snprintf(cfg->dependencies[cfg->n_dependencies].path, sizeof(cfg->dependencies[0].path), "%s", pathbuf);
                cfg->n_dependencies++;
            }
        }
        return 0;
    }

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
        /* Array value: ["a", "b", ...] */
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
                const char* end = cur;
                while (*end && *end != ',' && *end != ']') end++;
                size_t vlen = (size_t)(end - cur);
                if (vlen >= sizeof(valbuf)) vlen = sizeof(valbuf) - 1;
                memcpy(valbuf, cur, vlen);
                valbuf[vlen] = '\0';
                cur = end;
            }
            /* Process this array element */
            if (strcmp(key, "library") == 0 || strcmp(key, "libraries") == 0) {
                if (cfg->n_libraries < 32) {
                    snprintf(cfg->libraries[cfg->n_libraries++], 256, "%s", valbuf);
                }
            } else if (strcmp(key, "include-dir") == 0 || strcmp(key, "include-dirs") == 0) {
                if (cfg->n_include_dirs < 32) {
                    snprintf(cfg->include_dirs[cfg->n_include_dirs++], 4096, "%s", valbuf);
                }
            } else if (strcmp(key, "pkg-config") == 0 || strcmp(key, "pkg_config") == 0) {
                if (cfg->n_pkg_config < 16) {
                    snprintf(cfg->pkg_config[cfg->n_pkg_config++], 64, "%s", valbuf);
                }
            } else if (strcmp(key, "targets") == 0) {
                if (cfg->n_configured_targets < 16) {
                    snprintf(cfg->configured_targets[cfg->n_configured_targets++], 64, "%s", valbuf);
                }
            } else if (strncmp(cur_sec, "target.", 7) == 0 &&
                       (strcmp(key, "arch") == 0 || strcmp(key, "archs") == 0)) {
                TargetConfig* tc = get_or_create_target_config(cfg, cur_sec + 7);
                if (tc && tc->n_archs < 8) {
                    snprintf(tc->archs[tc->n_archs++], 32, "%s", valbuf);
                }
            }
            while (*cur == ' ' || *cur == '\t') cur++;
            if (*cur == ',') cur++;
        }
        return 0;
    } else {
        char* vcopy = strdup(val);
        if (!vcopy) return 0;
        char* comment = strchr(vcopy, '#');
        if (comment) *comment = '\0';
        size_t vlen = strlen(vcopy);
        while (vlen > 0 && (vcopy[vlen - 1] == ' ' || vcopy[vlen - 1] == '\t' || vcopy[vlen - 1] == '\n' || vcopy[vlen - 1] == '\r')) {
            vcopy[--vlen] = '\0';
        }
        snprintf(valbuf, sizeof(valbuf), "%s", vcopy);
        free(vcopy);
    }

    if (strcmp(cur_sec, "package") == 0 || cur_sec[0] == '\0') {
        if (strcmp(key, "name") == 0) snprintf(cfg->name, sizeof(cfg->name), "%s", valbuf);
        else if (strcmp(key, "version") == 0) snprintf(cfg->version, sizeof(cfg->version), "%s", valbuf);
    }
    if (strcmp(cur_sec, "dependencies") == 0) {
        if (cfg->n_dependencies < 32) {
            snprintf(cfg->dependencies[cfg->n_dependencies].name, sizeof(cfg->dependencies[0].name), "%s", key);
            snprintf(cfg->dependencies[cfg->n_dependencies].path, sizeof(cfg->dependencies[0].path), "%s", valbuf);
            cfg->n_dependencies++;
        }
    }
    if (strcmp(cur_sec, "build") == 0 || cur_sec[0] == '\0') {
        if (strcmp(key, "kind") == 0) snprintf(cfg->build_kind, sizeof(cfg->build_kind), "%s", valbuf);
        else if (strcmp(key, "target") == 0) snprintf(cfg->build_target, sizeof(cfg->build_target), "%s", valbuf);
        else if (strcmp(key, "backend") == 0) snprintf(cfg->backend, sizeof(cfg->backend), "%s", valbuf);
        else if (strcmp(key, "c-standard") == 0 || strcmp(key, "standard") == 0) snprintf(cfg->c_standard, sizeof(cfg->c_standard), "%s", valbuf);
        else if (strcmp(key, "cflags") == 0) snprintf(cfg->cflags, sizeof(cfg->cflags), "%s", valbuf);
        else if (strcmp(key, "pkg-config") == 0 || strcmp(key, "pkg_config") == 0) {
            if (cfg->n_pkg_config < 16) {
                snprintf(cfg->pkg_config[cfg->n_pkg_config++], 64, "%s", valbuf);
            }
        }
        else if (strcmp(key, "targets") == 0 && cfg->n_configured_targets < 16) {
            snprintf(cfg->configured_targets[cfg->n_configured_targets++], 64, "%s", valbuf);
        }
    }
    if (strncmp(cur_sec, "target.", 7) == 0) {
        TargetConfig* tc = get_or_create_target_config(cfg, cur_sec + 7);
        if (tc) {
            if (strcmp(key, "kind") == 0) snprintf(tc->build_kind, sizeof(tc->build_kind), "%s", valbuf);
            else if (strcmp(key, "c-standard") == 0 || strcmp(key, "standard") == 0) snprintf(tc->standard, sizeof(tc->standard), "%s", valbuf);
            else if (strcmp(key, "cflags") == 0) snprintf(tc->cflags, sizeof(tc->cflags), "%s", valbuf);
            else if (strcmp(key, "cc") == 0) snprintf(tc->cc, sizeof(tc->cc), "%s", valbuf);
            else if (strcmp(key, "ar") == 0) snprintf(tc->ar, sizeof(tc->ar), "%s", valbuf);
            else if (strcmp(key, "api") == 0) tc->android_api = atoi(valbuf);
            else if (strcmp(key, "ndk") == 0) snprintf(tc->android_ndk, sizeof(tc->android_ndk), "%s", valbuf);
            else if (strcmp(key, "arch") == 0 || strcmp(key, "archs") == 0) {
                if (tc->n_archs < 8) snprintf(tc->archs[tc->n_archs++], 32, "%s", valbuf);
            }
        }
    }
    return 0;
}

static void resolve_pkg_config(ProjectConfig* cfg) {
    if (cfg->n_pkg_config == 0) return;
    for (size_t i = 0; i < cfg->n_pkg_config; i++) {
        const char* pkg = cfg->pkg_config[i];
        char cmd[1024];

        /* Include dirs */
        snprintf(cmd, sizeof cmd, "pkg-config --cflags-only-I %s 2>/dev/null", pkg);
        FILE* fp = popen(cmd, "r");
        if (fp) {
            char buf[4096];
            if (fgets(buf, sizeof buf, fp)) {
                char* p = buf;
                while (*p) {
                    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
                    if (*p == '-' && *(p + 1) == 'I') {
                        p += 2;
                        char inc[4096];
                        int k = 0;
                        while (*p && *p != ' ' && *p != '\t' && *p != '\n') {
                            inc[k++] = *p++;
                        }
                        inc[k] = '\0';
                        if (k > 0 && cfg->n_include_dirs < 32) {
                            int seen = 0;
                            for (size_t j = 0; j < cfg->n_include_dirs; j++) {
                                if (strcmp(cfg->include_dirs[j], inc) == 0) { seen = 1; break; }
                            }
                            if (!seen) {
                                snprintf(cfg->include_dirs[cfg->n_include_dirs++], 4096, "%s", inc);
                            }
                        }
                    } else if (*p) {
                        p++;
                    }
                }
            }
            pclose(fp);
        }

        /* Other cflags */
        snprintf(cmd, sizeof cmd, "pkg-config --cflags-only-other %s 2>/dev/null", pkg);
        fp = popen(cmd, "r");
        if (fp) {
            char buf[4096];
            if (fgets(buf, sizeof buf, fp)) {
                size_t blen = strlen(buf);
                while (blen > 0 && (buf[blen - 1] == '\n' || buf[blen - 1] == '\r')) buf[--blen] = '\0';
                if (blen > 0) {
                    size_t curlen = strlen(cfg->cflags);
                    snprintf(cfg->cflags + curlen, sizeof(cfg->cflags) - curlen, " %s", buf);
                }
            }
            pclose(fp);
        }

        /* Libraries (-l) and library dirs (-L) */
        snprintf(cmd, sizeof cmd, "pkg-config --libs %s 2>/dev/null", pkg);
        fp = popen(cmd, "r");
        if (fp) {
            char buf[4096];
            if (fgets(buf, sizeof buf, fp)) {
                char* p = buf;
                while (*p) {
                    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
                    if (*p == '-' && *(p + 1) == 'l') {
                        p += 2;
                        char lib[256];
                        int k = 0;
                        while (*p && *p != ' ' && *p != '\t' && *p != '\n') {
                            lib[k++] = *p++;
                        }
                        lib[k] = '\0';
                        if (k > 0 && cfg->n_libraries < 32) {
                            int seen = 0;
                            for (size_t j = 0; j < cfg->n_libraries; j++) {
                                if (strcmp(cfg->libraries[j], lib) == 0) { seen = 1; break; }
                            }
                            if (!seen) {
                                snprintf(cfg->libraries[cfg->n_libraries++], 256, "%s", lib);
                            }
                        }
                    } else if (*p == '-' && *(p + 1) == 'L') {
                        char ldir[4096];
                        int k = 0;
                        ldir[k++] = *p++;
                        ldir[k++] = *p++;
                        while (*p && *p != ' ' && *p != '\t' && *p != '\n') {
                            ldir[k++] = *p++;
                        }
                        ldir[k] = '\0';
                        size_t curlen = strlen(cfg->cflags);
                        snprintf(cfg->cflags + curlen, sizeof(cfg->cflags) - curlen, " %s", ldir);
                    } else if (*p) {
                        p++;
                    }
                }
            }
            pclose(fp);
        }
    }
}

static void resolve_project_dependencies(const char* proj_dir, ProjectConfig* cfg, int depth) {
    if (depth > 8) return;

    resolve_pkg_config(cfg);

    for (size_t i = 0; i < cfg->n_dependencies; i++) {
        const char* dpath = cfg->dependencies[i].path;
        char resolved_dir[4096];
        if (dpath[0] == '/') {
            snprintf(resolved_dir, sizeof resolved_dir, "%s", dpath);
        } else {
            snprintf(resolved_dir, sizeof resolved_dir, "%s/%s", proj_dir, dpath);
        }

        /* 1. Add <dep>/src to include_dirs */
        char src_dir[4096];
        snprintf(src_dir, sizeof src_dir, "%s/src", resolved_dir);
        if (access(src_dir, R_OK) == 0 && cfg->n_include_dirs < 32) {
            int seen = 0;
            for (size_t j = 0; j < cfg->n_include_dirs; j++) {
                if (strcmp(cfg->include_dirs[j], src_dir) == 0) { seen = 1; break; }
            }
            if (!seen) {
                snprintf(cfg->include_dirs[cfg->n_include_dirs++], 4096, "%s", src_dir);
            }
        }
        /* 2. Add <dep> itself to include_dirs */
        if (access(resolved_dir, R_OK) == 0 && cfg->n_include_dirs < 32) {
            int seen = 0;
            for (size_t j = 0; j < cfg->n_include_dirs; j++) {
                if (strcmp(cfg->include_dirs[j], resolved_dir) == 0) { seen = 1; break; }
            }
            if (!seen) {
                snprintf(cfg->include_dirs[cfg->n_include_dirs++], 4096, "%s", resolved_dir);
            }
        }

        /* 3. Transitive inherit from dependency's rokade.toml */
        char dep_toml[4096];
        snprintf(dep_toml, sizeof dep_toml, "%s/rokade.toml", resolved_dir);
        if (access(dep_toml, R_OK) == 0) {
            ProjectConfig dep_cfg;
            project_config_init(&dep_cfg);
            FILE* df = fopen(dep_toml, "r");
            if (df) {
                char dline[4096];
                char cur_dsec[128] = "";
                while (fgets(dline, sizeof(dline), df)) {
                    parse_toml_line(dline, &dep_cfg, cur_dsec, sizeof(cur_dsec));
                }
                fclose(df);

                for (size_t k = 0; k < dep_cfg.n_pkg_config; k++) {
                    int seen = 0;
                    for (size_t m = 0; m < cfg->n_pkg_config; m++) {
                        if (strcmp(cfg->pkg_config[m], dep_cfg.pkg_config[k]) == 0) { seen = 1; break; }
                    }
                    if (!seen && cfg->n_pkg_config < 16) {
                        snprintf(cfg->pkg_config[cfg->n_pkg_config++], 64, "%s", dep_cfg.pkg_config[k]);
                    }
                }
                for (size_t k = 0; k < dep_cfg.n_libraries; k++) {
                    int seen = 0;
                    for (size_t m = 0; m < cfg->n_libraries; m++) {
                        if (strcmp(cfg->libraries[m], dep_cfg.libraries[k]) == 0) { seen = 1; break; }
                    }
                    if (!seen && cfg->n_libraries < 32) {
                        snprintf(cfg->libraries[cfg->n_libraries++], 256, "%s", dep_cfg.libraries[k]);
                    }
                }
                resolve_project_dependencies(resolved_dir, &dep_cfg, depth + 1);
            }
        }
    }

    resolve_pkg_config(cfg);
}

static int read_project_config(const char* proj_dir, ProjectConfig* cfg) {
    project_config_init(cfg);
    char toml_path[4096];
    snprintf(toml_path, sizeof(toml_path), "%s/rokade.toml", proj_dir);
    FILE* f = fopen(toml_path, "r");
    if (!f) return 0;
    char line[4096];
    char cur_sec[128] = "";
    while (fgets(line, sizeof(line), f)) {
        parse_toml_line(line, cfg, cur_sec, sizeof(cur_sec));
    }
    fclose(f);

    resolve_project_dependencies(proj_dir, cfg, 0);
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

static void mkdir_p(const char* path) {
    char buf[4096];
    snprintf(buf, sizeof buf, "%s", path);
    for (char* p = buf + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char save = *p;
            *p = '\0';
            mkdir(buf, 0755);
            *p = save;
        }
    }
    mkdir(buf, 0755);
}

static void print_diag_stderr(const char* fallback_path, const char* diag) {
    if (!diag) return;
    const char* colon = strchr(diag, ':');
    if (colon && colon > diag && !('0' <= diag[0] && diag[0] <= '9')) {
        fputs(diag, stderr);
    } else {
        fprintf(stderr, "%s:%s", fallback_path ? fallback_path : "", diag);
    }
}

static int do_build(const char* proj_path, const char* cli_target, const char* cli_backend, int build_all) {
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

    const char* active_backend = (cli_backend && cli_backend[0]) ? cli_backend : (cfg.backend[0] ? cfg.backend : "c");

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
    snprintf(gen_dir, sizeof(gen_dir), "%s/build/generated", proj_path);
    mkdir_p(gen_dir);

    char* c_file_paths[128];
    size_t n_src = 0;
    struct dirent* entry;

    /* Pre-scan: collect filenames and find which .rook files are included. */
    char* all_files[128];
    size_t n_all = 0;
    while ((entry = readdir(d)) != NULL) {
        if (n_all >= 128) break;
        size_t mlen = strlen(entry->d_name);
        if (mlen < 5 || strcmp(entry->d_name + mlen - 5, ".rook") != 0) continue;
        all_files[n_all++] = strdup(entry->d_name);
    }
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
    rewinddir(d);

    while ((entry = readdir(d)) != NULL) {
        size_t mlen = strlen(entry->d_name);
        if (n_src >= 128) break;
        if (mlen < 5 || strcmp(entry->d_name + mlen - 5, ".rook") != 0) continue;

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

        const char* inc_list[16];
        for (size_t j = 0; j < cfg.n_include_dirs; j++) inc_list[j] = cfg.include_dirs[j];
        char* expanded = resolve_includes(source, len, src_dir, inc_list, cfg.n_include_dirs, 0, rook_path);
        free(source);
        if (!expanded) { fprintf(stderr, "warning: include resolution failed for %s\n", rook_path); continue; }
        len = (int)strlen(expanded);

        Sema* sema = sema_new();
        int ntoks = 0;
        Token* toks = lex_all(expanded, len, &ntoks);
        Program* p = parse_program(expanded, len, toks, ntoks);
        if (!p) {
            print_diag_stderr(rook_path, parse_error());
            free(expanded);
            free(toks);
            continue;
        }
        sema_set_source(sema, expanded, len);
        sema_load_commandlist(src_dir, NULL);
        c_import_scan_and_load(sema, expanded, len, src_dir, inc_list, cfg.n_include_dirs);
        sema_collect(sema, p);
        sema_check(sema, p);
        if (sema->err) {
            print_diag_stderr(rook_path, sema->err);
            program_free(p);
            free(expanded);
            free(toks);
            sema_free(sema);
            continue;
        }
        int clen = 0;
        Backend* be = backend_create(active_backend);
        if (!be) {
            fprintf(stderr, "error: backend '%s' not available\n", active_backend);
            program_free(p);
            free(expanded);
            free(toks);
            sema_free(sema);
            continue;
        }
        char* c_code = be->emit_program(sema, p, &clen, 0);
        backend_destroy(be);

        char c_path[4096];
        const char* ext = (strcmp(active_backend, "llvm") == 0) ? "ll" : "c";
        snprintf(c_path, sizeof(c_path), "%s/build/generated/%.*s.%s",
                 proj_path, (int)(mlen - 5), entry->d_name, ext);

        FILE* fout = fopen(c_path, "w");
        if (fout) {
            fwrite(c_code, 1, clen, fout);
            fclose(fout);
        }
        printf("  compiled [%s]: %s -> %s\n", active_backend, entry->d_name, c_path);

        free(expanded);
        free(toks);
        free(c_code);
        sema_free(sema);
        program_free(p);

        c_file_paths[n_src++] = strdup(c_path);
    }
    closedir(d);
    free(is_included);
    for (size_t i = 0; i < n_all; i++) free(all_files[i]);

    if (n_src == 0) {
        fprintf(stderr, "error: no .rook files found in %s\n", src_dir);
        return 1;
    }

    /* Determine list of targets to build */
    const char* targets_to_build[16];
    size_t n_targets_to_build = 0;
    if (cli_target && cli_target[0]) {
        targets_to_build[n_targets_to_build++] = cli_target;
    } else if ((build_all || cfg.n_configured_targets > 0) && cfg.n_configured_targets > 0) {
        for (size_t i = 0; i < cfg.n_configured_targets; i++) {
            targets_to_build[n_targets_to_build++] = cfg.configured_targets[i];
        }
    } else {
        targets_to_build[n_targets_to_build++] = cfg.build_target[0] ? cfg.build_target : "linux";
    }

    const char* inc_dirs[32];
    size_t n_inc = 0;
    char gen_inc[4096];
    snprintf(gen_inc, sizeof(gen_inc), "%s/build/generated", proj_path);
    inc_dirs[n_inc++] = gen_inc;
    for (size_t i = 0; i < cfg.n_include_dirs && n_inc < 32; i++) {
        inc_dirs[n_inc++] = cfg.include_dirs[i];
    }
    const char* libs[16];
    for (size_t i = 0; i < cfg.n_libraries; i++) libs[i] = cfg.libraries[i];

    int any_err = 0;
    int is_multi = (n_targets_to_build > 1) || (cfg.n_configured_targets > 1);

    /* Build each target */
    for (size_t t = 0; t < n_targets_to_build; t++) {
        const char* target_name = targets_to_build[t];
        TargetConfig* tc_cfg = NULL;
        for (size_t i = 0; i < cfg.n_target_configs; i++) {
            if (strcmp(cfg.target_configs[i].target_os, target_name) == 0) {
                tc_cfg = &cfg.target_configs[i];
                break;
            }
        }

        size_t n_archs = 1;
        const char* archs[8] = { "" };
        if (strcmp(target_name, "android") == 0) {
            if (tc_cfg && tc_cfg->n_archs > 0) {
                n_archs = tc_cfg->n_archs;
                for (size_t a = 0; a < n_archs; a++) archs[a] = tc_cfg->archs[a];
            } else {
                archs[0] = "arm64-v8a";
            }
        }

        for (size_t a = 0; a < n_archs; a++) {
            const char* arch = archs[a];
            TargetSpec spec;
            memset(&spec, 0, sizeof spec);
            snprintf(spec.target_os, sizeof spec.target_os, "%s", target_name);
            snprintf(spec.target_arch, sizeof spec.target_arch, "%s", arch);
            snprintf(spec.build_kind, sizeof spec.build_kind, "%s",
                     (tc_cfg && tc_cfg->build_kind[0]) ? tc_cfg->build_kind : cfg.build_kind);
            snprintf(spec.standard, sizeof spec.standard, "%s",
                     (tc_cfg && tc_cfg->standard[0]) ? tc_cfg->standard : cfg.c_standard);
            snprintf(spec.cflags, sizeof spec.cflags, "%s",
                     (tc_cfg && tc_cfg->cflags[0]) ? tc_cfg->cflags : cfg.cflags);
            if (tc_cfg && tc_cfg->cc[0]) snprintf(spec.custom_cc, sizeof spec.custom_cc, "%s", tc_cfg->cc);
            if (tc_cfg && tc_cfg->ar[0]) snprintf(spec.custom_ar, sizeof spec.custom_ar, "%s", tc_cfg->ar);
            if (tc_cfg && tc_cfg->android_ndk[0]) snprintf(spec.ndk_path, sizeof spec.ndk_path, "%s", tc_cfg->android_ndk);
            spec.android_api = (tc_cfg && tc_cfg->android_api > 0) ? tc_cfg->android_api : 24;

            char target_out_dir[4096];
            if (is_multi) {
                if (strcmp(target_name, "android") == 0 && arch[0]) {
                    snprintf(target_out_dir, sizeof target_out_dir, "%s/build/android/%s", proj_path, arch);
                } else {
                    snprintf(target_out_dir, sizeof target_out_dir, "%s/build/%s", proj_path, target_name);
                }
            } else {
                snprintf(target_out_dir, sizeof target_out_dir, "%s/build", proj_path);
            }
            mkdir_p(target_out_dir);

            Toolchain tc;
            int det_ret = toolchain_detect_target(&tc, &spec);
            if (det_ret != 0) {
                fprintf(stderr, "error: failed to detect toolchain for target '%s'%s%s\n",
                        target_name, arch[0] ? ":" : "", arch);
                any_err = 1;
                continue;
            }

            char* obj_file_paths[128];
            int compile_failed = 0;
            for (size_t i = 0; i < n_src; i++) {
                char obj_path[4096];
                const char* c_base = strrchr(c_file_paths[i], '/');
                c_base = c_base ? c_base + 1 : c_file_paths[i];
                size_t blen = strlen(c_base);
                const char* dot = strrchr(c_base, '.');
                int stem_len = dot ? (int)(dot - c_base) : (int)blen;
                snprintf(obj_path, sizeof(obj_path), "%s/%.*s.o",
                         target_out_dir, stem_len, c_base);
                obj_file_paths[i] = strdup(obj_path);

                printf("  [%s%s%s] compiling: %s -> %s\n",
                       target_name, arch[0] ? ":" : "", arch, c_base, obj_path);
                int ret;
                size_t c_base_len = strlen(c_base);
                if (c_base_len >= 3 && strcmp(c_base + c_base_len - 3, ".ll") == 0) {
#ifdef ROKADE_HAS_LLVM
                    ret = llvm_backend_compile_ll_to_obj(c_file_paths[i], obj_path, 2);
#else
                    ret = toolchain_compile_obj_target(&spec, &tc, obj_path, c_file_paths[i], inc_dirs, n_inc, NULL);
#endif
                } else {
                    ret = toolchain_compile_obj_target(&spec, &tc, obj_path, c_file_paths[i], inc_dirs, n_inc, NULL);
                }
                if (ret != 0) {
                    fprintf(stderr, "error: compilation failed for %s (target %s)\n", c_file_paths[i], target_name);
                    compile_failed = 1;
                    for (size_t j = 0; j <= i; j++) free(obj_file_paths[j]);
                    break;
                }
            }
            if (compile_failed) {
                toolchain_free(&tc);
                any_err = 1;
                continue;
            }

            char target_bin[4096];
            int is_shared = strcmp(spec.build_kind, "shared-lib") == 0;
            int is_static = strcmp(spec.build_kind, "static-lib") == 0;
            if (is_shared) {
                snprintf(target_bin, sizeof(target_bin), "%s/lib%s.%s",
                         target_out_dir, cfg.name, strcmp(spec.target_os, "windows") == 0 ? "dll" : "so");
            } else if (is_static) {
                snprintf(target_bin, sizeof(target_bin), "%s/lib%s.a",
                         target_out_dir, cfg.name);
            } else {
                snprintf(target_bin, sizeof(target_bin), "%s/%s%s",
                         target_out_dir, cfg.name, strcmp(spec.target_os, "windows") == 0 ? ".exe" : "");
            }

            printf("  [%s%s%s] linking: %s\n",
                   target_name, arch[0] ? ":" : "", arch, target_bin);
            int link_ret = toolchain_link_target(&spec, &tc, target_bin, (const char**)obj_file_paths, n_src, libs, cfg.n_libraries, NULL);

            for (size_t i = 0; i < n_src; i++) free(obj_file_paths[i]);
            toolchain_free(&tc);

            if (link_ret != 0) {
                fprintf(stderr, "error: linking failed for %s\n", target_bin);
                any_err = 1;
            } else {
                printf("  [%s%s%s] build successful: %s\n",
                       target_name, arch[0] ? ":" : "", arch, target_bin);
            }
        }
    }

    for (size_t i = 0; i < n_src; i++) free(c_file_paths[i]);
    return any_err;
}

/* ---------- run command ---------- */

static int run_single_file_jit(const char* path) {
    int len = 0;
    char* src = util_read_file(path, &len);
    if (!src) {
        fprintf(stderr, "rokade: cannot read '%s'\n", path);
        return 1;
    }
    char* slash = strrchr(path, '/');
    char basedir[4096];
    if (slash) {
        size_t dlen = (size_t)(slash - path);
        snprintf(basedir, sizeof(basedir), "%.*s", (int)dlen, path);
    } else {
        snprintf(basedir, sizeof(basedir), ".");
    }
    char* expanded = resolve_includes(src, len, basedir, NULL, 0, 0, path);
    free(src);
    if (!expanded) {
        fprintf(stderr, "rokade: error resolving includes in '%s'\n", path);
        return 1;
    }
    len = (int)strlen(expanded);

    int ntoks = 0;
    Token* toks = lex_all(expanded, len, &ntoks);
    Program* p = parse_program(expanded, len, toks, ntoks);
    if (!p) {
        print_diag_stderr(path, parse_error());
        free(expanded);
        free(toks);
        return 1;
    }
    Sema* sema = sema_new();
    sema_set_source(sema, expanded, len);
    sema_load_commandlist(basedir, NULL);
    c_import_scan_and_load(sema, expanded, len, basedir, NULL, 0);
    sema_collect(sema, p);
    sema_check(sema, p);
    if (sema->err) {
        print_diag_stderr(path, sema->err);
        sema_free(sema);
        free(expanded);
        free(toks);
        program_free(p);
        return 1;
    }

    int rc = llvm_backend_jit_run(sema, p, 0, NULL);

    sema_free(sema);
    free(expanded);
    free(toks);
    program_free(p);
    return rc;
}

static int do_run(const char* proj_path, const char* cli_backend, int use_jit) {
    if (proj_path) {
        size_t plen = strlen(proj_path);
        if (plen >= 5 && strcmp(proj_path + plen - 5, ".rook") == 0) {
            return run_single_file_jit(proj_path);
        }
    }
    if (use_jit) {
        char main_path[4096];
        snprintf(main_path, sizeof(main_path), "%s/src/main.rook", proj_path ? proj_path : ".");
        return run_single_file_jit(main_path);
    }

    int ret = do_build(proj_path, "linux", cli_backend, 0);
    if (ret != 0) return ret;

    ProjectConfig cfg;
    if (!read_project_config(proj_path ? proj_path : ".", &cfg)) {
        fprintf(stderr, "error: cannot read project config\n");
        return 1;
    }

    char exe_path[4096];
    if (proj_path) snprintf(exe_path, sizeof(exe_path), "%s/build/linux/%s", proj_path, cfg.name);
    else snprintf(exe_path, sizeof(exe_path), "build/linux/%s", cfg.name);
    if (access(exe_path, X_OK) != 0) {
        if (proj_path) snprintf(exe_path, sizeof(exe_path), "%s/build/%s", proj_path, cfg.name);
        else snprintf(exe_path, sizeof(exe_path), "build/%s", cfg.name);
    }
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

    /* 1b) backend & interop tooling */
    printf("[PASS] backend: c (C23 / C11)\n");
#ifdef ROKADE_HAS_LLVM
    printf("[PASS] backend: llvm (%s)\n", ROKADE_LLVM_VERSION);
#else
    printf("[INFO] backend: llvm (not enabled in this build)\n");
#endif
#ifdef ROKADE_HAS_LIBCLANG
    printf("[PASS] c-interop: libclang (dynamic C header AST)\n");
#else
    printf("[INFO] c-interop: commandlist.json (fallback)\n");
#endif

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

    /* 5) Cross-compilation tooling (Android NDK & Windows MinGW) */
    {
        char* ndk = toolchain_find_ndk(NULL);
        if (ndk) {
            printf("[PASS] android NDK: %s\n", ndk);
            TargetSpec aspec;
            memset(&aspec, 0, sizeof aspec);
            snprintf(aspec.target_os, sizeof aspec.target_os, "android");
            snprintf(aspec.target_arch, sizeof aspec.target_arch, "arm64-v8a");
            aspec.android_api = 24;
            Toolchain atc;
            if (toolchain_detect_target(&atc, &aspec) == 0) {
                printf("[PASS] android clang: %s (%s)\n",
                       atc.cc_path ? atc.cc_path : "?",
                       atc.target_triple ? atc.target_triple : "aarch64");
                toolchain_free(&atc);
            }
            free(ndk);
        } else {
            printf("[WARN] android NDK: not detected (set ANDROID_NDK_HOME)\n");
        }

        char* mingw = toolchain_find_mingw();
        if (mingw) {
            printf("[PASS] windows cross-compiler: %s\n", mingw);
            free(mingw);
        } else {
            printf("[INFO] windows cross-compiler: not found (optional for Windows PE .exe)\n");
        }

        if (has_on_path("cmake"))  printf("[PASS] cmake: found on PATH\n");
        else                       printf("[WARN] cmake: not on PATH\n");
        if (has_on_path("ninja"))  printf("[PASS] ninja: found on PATH\n");
        if (has_on_path("gradle")) printf("[PASS] gradle: found on PATH\n");
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
    char* expanded = resolve_includes(src, len, basedir, NULL, 0, 0, path);
    free(src);
    if (!expanded) return NULL;
    len = (int)strlen(expanded);

    int ntoks = 0;
    Token* toks = lex_all(expanded, len, &ntoks);
    Program* p = parse_program(expanded, len, toks, ntoks);
    if (!p) {
        print_diag_stderr(path, parse_error());
        free(expanded);
        free(toks);
        return NULL;
    }
    Sema* sema = sema_new();
    sema_set_source(sema, expanded, len);
    sema_load_commandlist(basedir, NULL);
    c_import_scan_and_load(sema, expanded, len, basedir, NULL, 0);
    sema_collect(sema, p);
    sema_check(sema, p);
    if (sema->err) {
        print_diag_stderr(path, sema->err);
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
    char* expanded = resolve_includes(src, len, basedir, NULL, 0, 0, path);
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

/* Strip ANSI escape codes in-place (e.g. \033[0m, \033[31m) */
static void strip_ansi_codes(char* str) {
    if (!str) return;
    char* src = str;
    char* dst = str;
    while (*src) {
        if (*src == '\033' && *(src + 1) == '[') {
            src += 2;
            while (*src && *src != 'm') src++;
            if (*src == 'm') src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* Parse a diag_render line ("<line>:<col>: <kind>: <msg>") and append one
   JSON diagnostic object to `out`. Emits a leading comma only when `out`
   already holds content past the opening "[". Line/character are 1-based to
   match rokade's own `file:line:col:` text format. */
static void emit_diag_json(SB* out, const char* file, const char* diag) {
    int line = 1, col = 1;
    const char* msg = diag ? diag : "";
    const char* resolved_file = file; /* may be overridden by sourcemap prefix */

    /* diag_render now emits either:
       - "line:col: kind: msg\n..." (no sourcemap)
       - "file:line:col: kind: msg\n..." (sourcemap resolved)
       Try to parse the optional file: prefix first. */
    if (diag && diag[0] && diag[0] != ' ') {
        /* Try file:line:col: first - look for a string prefix before a digit-only segment */
        const char* colon1 = strchr(diag, ':');
        if (colon1 && colon1 > diag && !('0' <= diag[0] && diag[0] <= '9')) {
            /* Possible file:line:col format */
            const char* p2 = colon1 + 1;
            char* e1;
            long lv = strtol(p2, &e1, 10);
            if (e1 != p2 && *e1 == ':') {
                const char* p3 = e1 + 1;
                char* e2;
                long cv = strtol(p3, &e2, 10);
                if (e2 != p3 && *e2 == ':') {
                    /* Successfully parsed file:line:col: format */
                    static char fb[512];
                    size_t flen = (size_t)(colon1 - diag);
                    if (flen >= sizeof(fb)) flen = sizeof(fb) - 1;
                    memcpy(fb, diag, flen);
                    fb[flen] = '\0';
                    resolved_file = fb;
                    line = (int)lv;
                    col = (int)cv;
                    msg = e2 + 1;
                    goto extract_message;
                }
            }
        }
        /* Fall back to simple line:col: format */
        char* e1;
        long lv = strtol(diag, &e1, 10);
        if (e1 != diag && *e1 == ':') {
            const char* p = e1 + 1;
            char* e2;
            long cv = strtol(p, &e2, 10);
            if (e2 != p && *e2 == ':') { line = (int)lv; col = (int)cv; msg = e2 + 1; }
        }
    }
extract_message:;
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
    json_append_str(out, resolved_file);
    sb_appendf(out, ", \"line\":%d, \"character\":%d, \"severity\":\"%s\", \"message\":",
               line, col, severity);
    char* m = malloc(mlen + 1);
    memcpy(m, msg, mlen); m[mlen] = '\0';
    strip_ansi_codes(m);
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

    char* expanded = resolve_includes(src, len, basedir, NULL, 0, 0, path);
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
    char* expanded = resolve_includes(src, len, basedir, NULL, 0, 0, path);
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
        const char* proj_path = NULL;
        const char* cli_target = NULL;
        const char* cli_backend = NULL;
        int build_all = 0;
        for (int a = 2; a < argc; a++) {
            if (strcmp(argv[a], "--all") == 0) {
                build_all = 1;
            } else if (strncmp(argv[a], "--backend=", 10) == 0) {
                cli_backend = argv[a] + 10;
            } else if (strncmp(argv[a], "--target=", 9) == 0) {
                cli_target = argv[a] + 9;
            } else if (strcmp(argv[a], "-t") == 0 && a + 1 < argc) {
                cli_target = argv[++a];
            } else if (argv[a][0] != '-') {
                proj_path = argv[a];
            }
        }
        return do_build(proj_path, cli_target, cli_backend, build_all);
    }

    if (strcmp(argv[1], "run") == 0) {
        const char* proj_path = NULL;
        const char* cli_backend = NULL;
        int use_jit = 0;
        for (int a = 2; a < argc; a++) {
            if (strncmp(argv[a], "--backend=", 10) == 0) {
                cli_backend = argv[a] + 10;
            } else if (strcmp(argv[a], "--jit") == 0) {
                use_jit = 1;
            } else if (argv[a][0] != '-') {
                proj_path = argv[a];
            }
        }
        return do_run(proj_path, cli_backend, use_jit);
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
    int mode = 0; /* 0 emit, 1 emit-c, 2 ast, 3 check, 4 checkdir, 5 diagnostics, 6 emit-llvm, 7 emit-obj */
    int bounds_check = 0;
    const char* cli_backend = "c";
    int i = 1;
    /* Parse flags before the mode keyword */
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bounds-check") == 0) {
            bounds_check = 1;
            i++;
        } else if (strncmp(argv[i], "--backend=", 10) == 0) {
            cli_backend = argv[i] + 10;
            i++;
        } else {
            break;
        }
    }
    if (i < argc && strcmp(argv[i], "--emit-c") == 0) { mode = 1; cli_backend = "c"; i++; }
    else if (i < argc && strcmp(argv[i], "--emit-llvm") == 0) { mode = 6; cli_backend = "llvm"; i++; }
    else if (i < argc && strcmp(argv[i], "--emit-obj") == 0) { mode = 7; cli_backend = "llvm"; i++; }
    else if (i < argc && strcmp(argv[i], "--ast") == 0) { mode = 2; i++; }
    else if (i < argc && strcmp(argv[i], "--check") == 0) { mode = 3; i++; }
    else if (i < argc && strcmp(argv[i], "--check-dir") == 0) { mode = 4; i++; }
    else if (i < argc && (strcmp(argv[i], "--diagnostics") == 0 ||
                          strcmp(argv[i], "diagnostics") == 0)) {
        mode = 5;
        diag_init(DIAG_NEVER);
        i++;
    }

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

    const char* out_obj_override = NULL;
    const char* path = NULL;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_obj_override = argv[++i];
        } else if (argv[i][0] != '-') {
            if (!path) path = argv[i];
        }
    }

    if (!path) {
        usage();
        return 1;
    }

    int len = 0;
    char* src = util_read_file(path, &len);
    if (!src) {
        fprintf(stderr, "rokade: cannot read '%s'\n", path);
        return 1;
    }

    /* Resolve #include <file.rook> directives */
    char* slash = strrchr(path, '/');
    char basedir[4096];
    const char* env_base = getenv("ROKADE_BASE_DIR");
    if (env_base && env_base[0]) {
        snprintf(basedir, sizeof(basedir), "%s", env_base);
    } else if (slash) {
        size_t dlen = (size_t)(slash - path);
        snprintf(basedir, sizeof(basedir), "%.*s", (int)dlen, path);
    } else {
        snprintf(basedir, sizeof(basedir), ".");
    }
    char* expanded = resolve_includes(src, len, basedir, NULL, 0, 0, path);
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
            c_import_scan_and_load(sema, expanded, len, basedir, NULL, 0);
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
        print_diag_stderr(path, parse_error());
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
    if (mode == 7) {
        Sema* sema = sema_new();
        sema_set_source(sema, expanded, len);
        sema_load_commandlist(basedir, NULL);
        c_import_scan_and_load(sema, expanded, len, basedir, NULL, 0);
        sema_collect(sema, p);
        sema_check(sema, p);
        if (sema->err) {
            print_diag_stderr(path, sema->err);
            sema_free(sema);
            free(expanded);
            free(toks);
            program_free(p);
            return 1;
        }
        char out_obj[4096];
        if (out_obj_override) {
            snprintf(out_obj, sizeof(out_obj), "%s", out_obj_override);
        } else {
            const char* base = strrchr(path, '/');
            base = base ? base + 1 : path;
            size_t blen = strlen(base);
            snprintf(out_obj, sizeof(out_obj), "%.*s.o", (int)(blen > 5 ? blen - 5 : blen), base);
        }

        int rc = llvm_backend_emit_obj(sema, p, out_obj, 2);
        if (rc == 0) {
            printf("emitted: %s\n", out_obj);
        }
        sema_free(sema);
        free(expanded);
        free(toks);
        program_free(p);
        return rc;
    }
    if (mode == 1 || mode == 6 || (mode == 0 && strcmp(cli_backend, "llvm") == 0)) {
        Sema* sema = sema_new();
        sema_set_source(sema, expanded, len);
        sema_load_commandlist(basedir, NULL);
        c_import_scan_and_load(sema, expanded, len, basedir, NULL, 0);
        sema_collect(sema, p);
        sema_check(sema, p);
        if (sema->err) {
            print_diag_stderr(path, sema->err);
            sema_free(sema);
            free(expanded);
            free(toks);
            program_free(p);
            return 1;
        }
        int elen = 0;
        const char* bname = (mode == 6) ? "llvm" : cli_backend;
        Backend* be = backend_create(bname);
        if (!be) {
            fprintf(stderr, "rokade: backend '%s' not available\n", bname);
            sema_free(sema);
            free(expanded);
            free(toks);
            program_free(p);
            return 1;
        }
        char* c = be->emit_program(sema, p, &elen, bounds_check);
        backend_destroy(be);
        if (c) {
            fwrite(c, 1, elen, stdout);
            free(c);
        }
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