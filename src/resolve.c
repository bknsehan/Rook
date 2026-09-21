#include "resolve.h"
#include "util.h"
#include "toolchain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#define access _access
#define R_OK 4
#else
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/* Get the root installation directory of rokade.
   Returns 0 on success, -1 on failure. */
int rokade_get_install_root(char* buf, size_t cap) {
    /* 1. Explicit environment override */
    const char* env_home = getenv("ROOK_HOME");
    if (!env_home || !env_home[0]) env_home = getenv("ROKADE_PATH");
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
            char check[4096];
            snprintf(check, sizeof(check), "%s/std/result.rook", buf);
            if (access(check, R_OK) == 0) return 0;

            char* bin_slash = strrchr(buf, '/');
            if (bin_slash) {
                *bin_slash = '\0';
                snprintf(check, sizeof(check), "%s/std/result.rook", buf);
                if (access(check, R_OK) == 0) return 0;
                if (strcmp(bin_slash + 1, "bin") == 0 || strcmp(bin_slash + 1, "build") == 0) {
                    return 0;
                }
            }
        }
    }
#elif defined(__APPLE__)
    /* 2. macOS _NSGetExecutablePath */
    uint32_t size = (uint32_t)cap;
    char apple_path[4096];
    if (_NSGetExecutablePath(apple_path, &size) == 0) {
        if (realpath(apple_path, buf) != NULL) {
            char* slash = strrchr(buf, '/');
            if (slash) {
                *slash = '\0'; /* strip executable name */
                char check[4096];
                snprintf(check, sizeof(check), "%s/std/result.rook", buf);
                if (access(check, R_OK) == 0) return 0;

                char* bin_slash = strrchr(buf, '/');
                if (bin_slash) {
                    *bin_slash = '\0';
                    snprintf(check, sizeof(check), "%s/std/result.rook", buf);
                    if (access(check, R_OK) == 0) return 0;
                    if (strcmp(bin_slash + 1, "bin") == 0 || strcmp(bin_slash + 1, "build") == 0) {
                        return 0;
                    }
                }
            }
        }
    }
#elif defined(_WIN32)
    /* 3. Windows GetModuleFileName */
    DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)cap);
    if (len > 0) {
        for (char* p = buf; *p; p++) if (*p == '\\') *p = '/';
        char* slash = strrchr(buf, '/');
        if (slash) {
            *slash = '\0';
            char check[4096];
            snprintf(check, sizeof(check), "%s/std/result.rook", buf);
            if (access(check, R_OK) == 0) return 0;

            char* bin_slash = strrchr(buf, '/');
            if (bin_slash) {
                *bin_slash = '\0';
                snprintf(check, sizeof(check), "%s/std/result.rook", buf);
                if (access(check, R_OK) == 0) return 0;
                if (strcasecmp(bin_slash + 1, "bin") == 0 || strcasecmp(bin_slash + 1, "build") == 0) {
                    return 0;
                }
            }
        }
    }
#endif

    /* Check current working directory for std/ (e.g. running from repo root) */
    if (access("std/result.rook", R_OK) == 0) {
        snprintf(buf, cap, ".");
        return 0;
    }

    /* 4. $HOME/bin/Rook, %USERPROFILE%/.rook, etc. */
    const char* home = getenv("HOME");
#ifdef _WIN32
    if (!home || !home[0]) home = getenv("USERPROFILE");
#endif
    if (home && home[0]) {
        snprintf(buf, cap, "%s/bin/Rook", home);
        if (access(buf, R_OK) == 0) return 0;

        snprintf(buf, cap, "%s/.local/share/rook", home);
        if (access(buf, R_OK) == 0) return 0;

#ifdef _WIN32
        snprintf(buf, cap, "%s/.rook", home);
        if (access(buf, R_OK) == 0) return 0;
#endif
    }

    /* 5. System-wide install */
    if (access("/usr/local/lib/rook", R_OK) == 0) {
        snprintf(buf, cap, "/usr/local/lib/rook");
        return 0;
    }

    return -1;
}

int rokade_get_std_dir(char* out_std, size_t cap) {
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

static int try_candidate(char* out, size_t out_cap, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out, out_cap, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= out_cap) return 0;
    return access(out, R_OK) == 0;
}

static int rk_path_prefix_eq(const char* cand, const char* prefix, size_t n) {
#ifdef _WIN32
    for (size_t i = 0; i < n; i++) {
        char c1 = cand[i];
        char c2 = prefix[i];
        if (c1 == '\\') c1 = '/';
        if (c2 == '\\') c2 = '/';
        if (tolower((unsigned char)c1) != tolower((unsigned char)c2)) return 0;
    }
    return 1;
#else
    return strncmp(cand, prefix, n) == 0;
#endif
}

static int is_path_in_jail(const char* candidate, const char* basedir, const char** inc_dirs, size_t n_inc) {
    char real_cand[4096];
    if (!rk_realpath(candidate, real_cand)) return 0;

    /* 1. Check against basedir and its ancestor directories */
    if (basedir) {
        char cur_base[4096];
        snprintf(cur_base, sizeof(cur_base), "%s", basedir);
        while (cur_base[0]) {
            char real_base[4096];
            if (rk_realpath(cur_base, real_base)) {
                size_t blen = strlen(real_base);
                if (rk_path_prefix_eq(real_cand, real_base, blen) &&
                    (real_cand[blen] == '/' || real_cand[blen] == '\\' || real_cand[blen] == '\0')) {
                    return 1;
                }
            }
            char* slash = strrchr(cur_base, '/');
#ifdef _WIN32
            char* bslash = strrchr(cur_base, '\\');
            if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
            if (!slash || slash == cur_base) break;
            *slash = '\0';
        }
    }

    /* 2. Check against standard library directory */
    char std_dir[4096];
    if (rokade_get_std_dir(std_dir, sizeof(std_dir)) == 0) {
        char real_std[4096];
        if (rk_realpath(std_dir, real_std)) {
            size_t slen = strlen(real_std);
            if (rk_path_prefix_eq(real_cand, real_std, slen) && (real_cand[slen] == '/' || real_cand[slen] == '\\' || real_cand[slen] == '\0')) {
                return 1;
            }
        }
    }

    /* 3. Check against explicit include directories (includes vendor / dependencies) */
    for (size_t i = 0; i < n_inc; i++) {
        if (!inc_dirs[i]) continue;
        char real_inc[4096];
        if (rk_realpath(inc_dirs[i], real_inc)) {
            size_t ilen = strlen(real_inc);
            if (rk_path_prefix_eq(real_cand, real_inc, ilen) && (real_cand[ilen] == '/' || real_cand[ilen] == '\\' || real_cand[ilen] == '\0')) {
                return 1;
            }
        }
    }

    return 0;
}

#define CHECK_CANDIDATE(...) do { \
    if (try_candidate(candidate, sizeof(candidate), __VA_ARGS__) && \
        is_path_in_jail(candidate, basedir, inc_dirs, n_inc)) \
        return strdup(candidate); \
} while (0)

char* resolve_include_path(const char* incpath, const char* basedir,
                           const char** inc_dirs, size_t n_inc) {
    char clean_inc[1024];
    const char* p = incpath;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '<' || *p == '"') p++;
    size_t clen = 0;
    while (*p && *p != '>' && *p != '"' && *p != '\r' && *p != '\n' && clen + 1 < sizeof(clean_inc)) {
        clean_inc[clen++] = *p++;
    }
    while (clen > 0 && (clean_inc[clen - 1] == ' ' || clean_inc[clen - 1] == '\t')) clen--;
    clean_inc[clen] = '\0';
    incpath = clean_inc;

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

    char slash_mod[256];
    snprintf(slash_mod, sizeof(slash_mod), "%s", modname);
    int has_dot = 0;
    for (char* cp = slash_mod; *cp; cp++) {
        if (*cp == '.') {
            *cp = '/';
            has_dot = 1;
        }
    }

    CHECK_CANDIDATE("%s", incpath);
    CHECK_CANDIDATE("%s.rook", modname);
    if (has_dot) {
        CHECK_CANDIDATE("%s.rook", slash_mod);
        CHECK_CANDIDATE("%s", slash_mod);
    }
    if (basedir) {
        CHECK_CANDIDATE("%s/%s", basedir, incpath);
        CHECK_CANDIDATE("%s/%s.rook", basedir, modname);
        CHECK_CANDIDATE("%s/src/%s", basedir, incpath);
        CHECK_CANDIDATE("%s/src/%s.rook", basedir, modname);
        if (has_dot) {
            CHECK_CANDIDATE("%s/%s", basedir, slash_mod);
            CHECK_CANDIDATE("%s/%s.rook", basedir, slash_mod);
            CHECK_CANDIDATE("%s/src/%s", basedir, slash_mod);
            CHECK_CANDIDATE("%s/src/%s.rook", basedir, slash_mod);
        }
        CHECK_CANDIDATE("%s/%s/src/%s.rook", basedir, modname, modname);
        CHECK_CANDIDATE("%s/%s/src/lib.rook", basedir, modname);
        CHECK_CANDIDATE("%s/%s/lib.rook", basedir, modname);
        CHECK_CANDIDATE("%s/vendor/%s/src/%s.rook", basedir, modname, modname);
        CHECK_CANDIDATE("%s/vendor/%s/src/lib.rook", basedir, modname);
        CHECK_CANDIDATE("%s/vendor/%s/lib.rook", basedir, modname);
        CHECK_CANDIDATE("%s/vendor/%s/%s.rook", basedir, modname, modname);

        /* Walk up basedir to also probe ancestor directories (e.g. project root) */
        char cur_base[4096];
        snprintf(cur_base, sizeof(cur_base), "%s", basedir);
        while (cur_base[0]) {
            char* slash = strrchr(cur_base, '/');
#ifdef _WIN32
            char* bslash = strrchr(cur_base, '\\');
            if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
            if (!slash || slash == cur_base) break;
            *slash = '\0';
            CHECK_CANDIDATE("%s/%s", cur_base, incpath);
            CHECK_CANDIDATE("%s/%s.rook", cur_base, modname);
            CHECK_CANDIDATE("%s/src/%s", cur_base, incpath);
            CHECK_CANDIDATE("%s/src/%s.rook", cur_base, modname);
            if (has_dot) {
                CHECK_CANDIDATE("%s/%s", cur_base, slash_mod);
                CHECK_CANDIDATE("%s/%s.rook", cur_base, slash_mod);
                CHECK_CANDIDATE("%s/src/%s", cur_base, slash_mod);
                CHECK_CANDIDATE("%s/src/%s.rook", cur_base, slash_mod);
            }
            CHECK_CANDIDATE("%s/vendor/%s/src/%s.rook", cur_base, modname, modname);
            CHECK_CANDIDATE("%s/vendor/%s/src/lib.rook", cur_base, modname);
            CHECK_CANDIDATE("%s/vendor/%s/lib.rook", cur_base, modname);
            CHECK_CANDIDATE("%s/vendor/%s/%s.rook", cur_base, modname, modname);
        }
    }

    /* Check standard library directory */
    char std_dir[4096];
    if (rokade_get_std_dir(std_dir, sizeof(std_dir)) == 0) {
        if (strncmp(incpath, "std/", 4) == 0) {
            CHECK_CANDIDATE("%s/%s.rook", std_dir, modname + 4);
            CHECK_CANDIDATE("%s/%s", std_dir, incpath + 4);
        }
        if (has_dot && strncmp(slash_mod, "std/", 4) == 0) {
            CHECK_CANDIDATE("%s/%s.rook", std_dir, slash_mod + 4);
            CHECK_CANDIDATE("%s/%s", std_dir, slash_mod + 4);
        }
        CHECK_CANDIDATE("%s/%s.rook", std_dir, modname);
        CHECK_CANDIDATE("%s/%s", std_dir, incpath);
        if (has_dot) {
            CHECK_CANDIDATE("%s/%s.rook", std_dir, slash_mod);
            CHECK_CANDIDATE("%s/%s", std_dir, slash_mod);
        }
    }

    for (size_t i = 0; i < n_inc; i++) {
        if (!inc_dirs[i]) continue;
        CHECK_CANDIDATE("%s/%s", inc_dirs[i], incpath);
        CHECK_CANDIDATE("%s/%s.rook", inc_dirs[i], modname);
        if (has_dot) {
            CHECK_CANDIDATE("%s/%s", inc_dirs[i], slash_mod);
            CHECK_CANDIDATE("%s/%s.rook", inc_dirs[i], slash_mod);
        }

        /* Special std module alias: std/io.rook -> <std_dir>/io.rook */
        if (strncmp(incpath, "std/", 4) == 0) {
            CHECK_CANDIDATE("%s/%s", inc_dirs[i], incpath + 4);
            CHECK_CANDIDATE("%s/%s.rook", inc_dirs[i], modname + 4);
        }
        if (has_dot && strncmp(slash_mod, "std/", 4) == 0) {
            CHECK_CANDIDATE("%s/%s", inc_dirs[i], slash_mod + 4);
            CHECK_CANDIDATE("%s/%s.rook", inc_dirs[i], slash_mod + 4);
        }

        CHECK_CANDIDATE("%s/src/%s", inc_dirs[i], incpath);
        CHECK_CANDIDATE("%s/src/%s.rook", inc_dirs[i], modname);
        if (has_dot) {
            CHECK_CANDIDATE("%s/src/%s", inc_dirs[i], slash_mod);
            CHECK_CANDIDATE("%s/src/%s.rook", inc_dirs[i], slash_mod);
        }

        /* Check <inc_dir>/<modname>/src/lib.rook, src/<modname>.rook, lib.rook */
        CHECK_CANDIDATE("%s/%s/src/lib.rook", inc_dirs[i], modname);
        CHECK_CANDIDATE("%s/%s/src/%s.rook", inc_dirs[i], modname, modname);
        CHECK_CANDIDATE("%s/%s/lib.rook", inc_dirs[i], modname);
        CHECK_CANDIDATE("%s/%s/%s.rook", inc_dirs[i], modname, modname);

        /* If inc_dirs[i] basename matches modname, check its entrypoints */
        const char* slash = strrchr(inc_dirs[i], '/');
        const char* slash2 = strrchr(inc_dirs[i], '\\');
        if (slash2 && (!slash || slash2 > slash)) slash = slash2;
        const char* dir_base = slash ? slash + 1 : inc_dirs[i];
        if (strcmp(dir_base, modname) == 0) {
            CHECK_CANDIDATE("%s/src/lib.rook", inc_dirs[i]);
            CHECK_CANDIDATE("%s/src/%s.rook", inc_dirs[i], modname);
            CHECK_CANDIDATE("%s/src/main.rook", inc_dirs[i]);
            CHECK_CANDIDATE("%s/lib.rook", inc_dirs[i]);
        }
    }
    return NULL;
}
#undef CHECK_CANDIDATE

char* resolve_c_header_path(const char* header, const char* basedir,
                            const char** inc_dirs, size_t n_inc) {
    if (!header || !header[0]) return NULL;

    char clean_h[512];
    const char* p = header;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '<' || *p == '"') p++;
    size_t hlen = 0;
    while (*p && *p != '>' && *p != '"' && *p != '\r' && *p != '\n' && hlen + 1 < sizeof(clean_h)) {
        clean_h[hlen++] = *p++;
    }
    while (hlen > 0 && (clean_h[hlen - 1] == ' ' || clean_h[hlen - 1] == '\t')) hlen--;
    clean_h[hlen] = '\0';
    if (!clean_h[0]) return NULL;

    char candidate[4096];

    /* 1. Direct candidate or relative to basedir */
    if (try_candidate(candidate, sizeof(candidate), "%s", clean_h)) return strdup(candidate);
    if (basedir) {
        if (try_candidate(candidate, sizeof(candidate), "%s/%s", basedir, clean_h)) return strdup(candidate);
        if (try_candidate(candidate, sizeof(candidate), "%s/include/%s", basedir, clean_h)) return strdup(candidate);
        if (try_candidate(candidate, sizeof(candidate), "%s/../%s", basedir, clean_h)) return strdup(candidate);
        if (try_candidate(candidate, sizeof(candidate), "%s/../include/%s", basedir, clean_h)) return strdup(candidate);
    }

    /* 2. Explicit include directories */
    for (size_t i = 0; i < n_inc; i++) {
        if (!inc_dirs[i]) continue;
        if (try_candidate(candidate, sizeof(candidate), "%s/%s", inc_dirs[i], clean_h)) return strdup(candidate);
    }

    /* 3. Standard system include directories */
#ifdef _WIN32
    const char* win_paths[] = {
        "C:/msys64/ucrt64/include",
        "C:/msys64/mingw64/include",
        "C:/msys64/clang64/include",
        "/ucrt64/include",
        "/mingw64/include",
        "/clang64/include",
        "C:/MinGW/include",
        NULL
    };
    for (int i = 0; win_paths[i]; i++) {
        if (try_candidate(candidate, sizeof(candidate), "%s/%s", win_paths[i], clean_h)) {
            return strdup(candidate);
        }
    }
#else
    const char* unix_paths[] = {
        "/usr/include",
        "/usr/include/x86_64-linux-gnu",
        "/usr/include/aarch64-linux-gnu",
        "/usr/include/arm-linux-gnueabihf",
        "/usr/local/include",
        "/usr/lib/clang/22/include",
        "/usr/lib/clang/21/include",
        "/usr/lib/clang/20/include",
        "/usr/lib/clang/19/include",
        "/usr/lib/clang/18/include",
        "/usr/lib/clang/17/include",
        "/usr/lib/clang/16/include",
        "/usr/lib/clang/15/include",
        NULL
    };
    for (int i = 0; unix_paths[i]; i++) {
        if (try_candidate(candidate, sizeof(candidate), "%s/%s", unix_paths[i], clean_h)) {
            return strdup(candidate);
        }
    }
#endif

    return NULL;
}

typedef struct VisitedInc {
    char path[4096];
    struct VisitedInc* next;
} VisitedInc;

static const char* rk_find_last_slash(const char* path) {
    const char* s = strrchr(path, '/');
#ifdef _WIN32
    const char* bs = strrchr(path, '\\');
    if (!s || (bs && bs > s)) s = bs;
#endif
    return s;
}

static int match_majinc_include(const char* line, char* out_mod, size_t modcap) {
    const char* s = line;
    while (*s == ' ' || *s == '\t') s++;
    if (memcmp(s, "include", 7) != 0) return 0;
    s += 7;
    if (*s != ' ' && *s != '\t') return 0;
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
    if (*s != '\0') return 0;
    memcpy(out_mod, id0, idlen);
    out_mod[idlen] = '\0';
    return 1;
}

char* rook_rewrites_includes(const char* src, int src_len) {
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
            char alias[128] = "";
            if (quote && *after == quote) {
                after++;
            }
            while (*after == ' ' || *after == '\t') after++;
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
            if (idlen > 0 && idlen < sizeof mod) {
                memcpy(mod, id0, idlen);
                mod[idlen] = '\0';
                if (idlen > 5 && strcmp(mod + idlen - 5, ".rook") == 0) {
                    mod[idlen - 5] = '\0';
                }
                if (!quote) {
                    for (char* cp = mod; *cp; cp++) {
                        if (*cp == '.') *cp = '/';
                    }
                }
                const char* indent = p;
                while (indent < end && (*indent == ' ' || *indent == '\t')) indent++;
                sb_appendn(&out, p, (int)(indent - p));
                sb_append(&out, "#include \"");
                sb_append(&out, mod);
                sb_append(&out, ".rook\"");
                if (alias[0]) {
                    sb_appendf(&out, " as %s", alias);
                }
                sb_append(&out, "\n");
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
            sb_appendn(&out, p, line_len);
        }
        p = nl ? nl + 1 : end;
    }
    return sb_strdup(&out);
}

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

    int cur_orig_line = 1;
    int stamp = (depth == 0 && current_file && current_file[0]);

    while (p < end) {
        const char* hash = p;
        while (hash < end && (*hash == ' ' || *hash == '\t')) hash++;
        if (hash < end && *hash == '#') {
            const char* nl = memchr(hash + 1, '\n', end - hash - 1);
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

                        char mod_alias[256] = "";
                        const char* aptr = cend + 1;
                        while (*aptr == ' ' || *aptr == '\t') aptr++;
                        if (strncmp(aptr, "as", 2) == 0 && (aptr[2] == ' ' || aptr[2] == '\t')) {
                            aptr += 2;
                            while (*aptr == ' ' || *aptr == '\t') aptr++;
                            const char* astart = aptr;
                            while (*aptr && *aptr != ' ' && *aptr != '\t' && *aptr != ';' && *aptr != '\r' && *aptr != '\n') aptr++;
                            size_t alen = (size_t)(aptr - astart);
                            if (alen > 0 && alen < sizeof(mod_alias)) {
                                memcpy(mod_alias, astart, alen);
                                mod_alias[alen] = '\0';
                            }
                        }

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

                            char canon[4096];
                            const char* track_path = rk_realpath(resolved, canon) ? canon : resolved;
                            char track_key[4096];
                            snprintf(track_key, sizeof(track_key), "%s#%s", track_path, mod_alias);
                            int seen = 0;
                            for (VisitedInc* v = *visited; v; v = v->next) {
                                if (strcmp(v->path, track_key) == 0) { seen = 1; break; }
                            }
                            if (seen) {
                                free(resolved);
                                p = nl ? nl + 1 : end;
                                cur_orig_line++;
                                continue;
                            }
                            VisitedInc* vi = malloc(sizeof *vi);
                            if (vi) {
                                snprintf(vi->path, sizeof(vi->path), "%s", track_key);
                                vi->next = *visited;
                                *visited = vi;
                            }

                            int ilen = 0;
                            char* isrc = util_read_file(resolved, &ilen);
                            if (!isrc) {
                                fprintf(stderr, "error: cannot read included file\n");
                                sb_free(&out);
                                free(resolved);
                                free(work);
                                return NULL;
                            }
                            const char* slash = rk_find_last_slash(resolved);
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
                            if (mod_alias[0]) {
                                sb_appendf(&out, "#pragma rook module %s\n", mod_alias);
                            }
                            sb_append(&out, expanded);
                            if (out.len == 0 || out.data[out.len - 1] != '\n')
                                sb_append(&out, "\n");
                            if (mod_alias[0]) {
                                sb_appendf(&out, "#pragma rook module end\n");
                            }
                            free(expanded);
                            p = nl ? nl + 1 : end;
                            cur_orig_line++;
                            continue;
                        }
                    }
                }
            }
            if (stamp) sb_appendf(&out, "// @rk:src %s:%d\n", current_file, cur_orig_line);
            sb_appendn(&out, p, line_len);
            p = nl ? nl + 1 : end;
            cur_orig_line++;
        } else {
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

char* resolve_includes(const char* src, int src_len, const char* basedir,
                       const char** inc_dirs, size_t n_inc, int depth,
                       const char* current_file) {
    size_t alloc_cap = n_inc + 4;
    const char** all_dirs = (const char**)malloc(alloc_cap * sizeof(const char*));
    size_t total_inc = 0;
    if (all_dirs && inc_dirs) {
        for (size_t i = 0; i < n_inc; i++) {
            all_dirs[total_inc++] = inc_dirs[i];
        }
    }
    char std_path[4096];
    char* allocated_std = NULL;
    if (all_dirs && rokade_get_std_dir(std_path, sizeof std_path) == 0) {
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
    if (all_dirs) free(all_dirs);
    return r;
}
