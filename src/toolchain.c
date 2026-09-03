#include "toolchain.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Extra directories searched (in addition to $PATH) when the compiler/archiver
   is not found on PATH. Covers common out-of-PATH installs. */
static const char* EXTRA_DIRS[] = {
    "/usr/local/bin",
    "/opt/homebrew/bin",
    "/opt/local/bin",
    "/mingw64/bin",
    "/usr/bin",
    "/bin",
    NULL
};

/* Search `dir` (or PATH) for an executable named `name` and return its path
   (malloc'd), or NULL if not found. */
static char* find_in_path(const char* name) {
    const char* path = getenv("PATH");
    if (!path || !*path) path = "/usr/local/bin:/usr/bin:/bin";
    char* dup = strdup(path);
    if (!dup) return NULL;
    char* save = NULL;
    char* dir = strtok_r(dup, ":", &save);
    char* found = NULL;
    while (dir && !found) {
        char buf[4096];
        snprintf(buf, sizeof buf, "%s/%s", dir, name);
        if (access(buf, X_OK) == 0) found = strdup(buf);
        dir = strtok_r(NULL, ":", &save);
    }
    free(dup);
    if (found) return found;

    /* Fall back to a few well-known prefixes not always on PATH. */
    for (int i = 0; EXTRA_DIRS[i]; i++) {
        char buf[4096];
        snprintf(buf, sizeof buf, "%s/%s", EXTRA_DIRS[i], name);
        if (access(buf, X_OK) == 0) return strdup(buf);
    }
    return NULL;
}

/* Resolve a configured compiler/archiver name to an executable path:
   absolute names are checked directly; bare names are searched on PATH /
   well-known prefixes. Returns malloc'd path or NULL. */
static char* resolve_exe(const char* name) {
    if (!name || !name[0]) return NULL;
    if (strchr(name, '/')) return access(name, X_OK) == 0 ? strdup(name) : NULL;
    return find_in_path(name);
}

/* Capture the first line of `<cc> --version`. */
static void probe_version(Toolchain* tc) {
    char buf[8192];
    snprintf(buf, sizeof buf, "%s --version 2>&1", tc->cc_path ? tc->cc_path : "cc");
    FILE* p = popen(buf, "r");
    if (!p) return;
    char line[2048];
    if (fgets(line, sizeof line, p)) {
        size_t L = strlen(line);
        while (L > 0 && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[--L] = '\0';
        tc->cc_version = strdup(line);
        if (strstr(line, "Apple") && strstr(line, "clang")) tc->cc_vendor = strdup("clang");
        else if (strstr(line, "clang"))                          tc->cc_vendor = strdup("clang");
        else if (strstr(line, "gcc") || strstr(line, "GCC") ||
                 strstr(line, "Free Software Foundation"))       tc->cc_vendor = strdup("gcc");
        else if (strstr(line, "tcc") || strstr(line, "TinyCC"))  tc->cc_vendor = strdup("tcc");
        else                                                   tc->cc_vendor = strdup("unknown");
    }
    pclose(p);
}

/* For gcc, capture the numeric version via `-dumpversion` (e.g. "11.4.0"). */
static char* probe_dumpversion(const char* cc_path) {
    char buf[8192];
    snprintf(buf, sizeof buf, "%s -dumpversion 2>/dev/null", cc_path ? cc_path : "cc");
    FILE* p = popen(buf, "r");
    if (!p) return NULL;
    char line[2048];
    char* r = NULL;
    if (fgets(line, sizeof line, p)) {
        size_t L = strlen(line);
        while (L > 0 && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[--L] = '\0';
        if (L) r = strdup(line);
    }
    pclose(p);
    return r;
}

/* Try to compile `int main(){return 0;}` with `-std=<std>`; return 1 if the
   compiler accepts it. */
static int probe_std(const char* cc, const char* std) {
    char path[4096];
    snprintf(path, sizeof path, "/tmp/rook_probe_%d.c", (int)getpid());
    FILE* f = fopen(path, "w");
    if (!f) return 0;
    fputs("int main(void){return 0;}\n", f);
    fclose(f);
    char cmd[8192];
    snprintf(cmd, sizeof cmd, "%s -std=%s -c -o /dev/null %s 2>/dev/null",
             cc ? cc : "cc", std, path);
    int rc = system(cmd);
    unlink(path);
    return rc == 0;
}

static const char* CC_CANDS[] = { "gcc", "clang", "cc", "tcc", "musl-gcc", NULL };
static const char* AR_CANDS[] = { "ar", "llvm-ar", "gcc-ar", "mingw-ar", NULL };

/* ---- detection cache --------------------------------------------------------
   To keep runs deterministic and fast, auto-detected results are cached in a
   dedicated cache file (NOT the user's config). The cache is only used when the
   user has not explicitly overridden cc/ar; it is invalidated automatically
   when the cached binary is missing. */
static char* toolchain_cache_path(void) {
    const char* xdg = getenv("XDG_CACHE_HOME");
    char base[4096];
    if (xdg) {
        snprintf(base, sizeof base, "%s/rokade", xdg);
    } else {
        const char* home = getenv("HOME");
        if (!home) home = ".";
        snprintf(base, sizeof base, "%s/.cache/rokade", home);
    }
    size_t need = strlen(base) + strlen("/toolchain.toml") + 1;
    char* p = malloc(need);
    if (p) snprintf(p, need, "%s/toolchain.toml", base);
    return p;
}

static void write_cache(const Toolchain* tc) {
    char* cp = toolchain_cache_path();
    if (!cp) return;
    char* dir = strdup(cp);
    char* slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; mkdir(dir, 0755); }
    free(dir);
    FILE* f = fopen(cp, "w");
    if (f) {
        fprintf(f, "# auto-generated by rokade; do not edit\n");
        if (tc->cc_path)            fprintf(f, "cc_path = \"%s\"\n", tc->cc_path);
        if (tc->cc_vendor)          fprintf(f, "cc_vendor = \"%s\"\n", tc->cc_vendor);
        if (tc->cc_version)         fprintf(f, "cc_version = \"%s\"\n", tc->cc_version);
        fprintf(f, "supports_c11 = %d\n", tc->supports_c11);
        fprintf(f, "supports_c17 = %d\n", tc->supports_c17);
        if (tc->ar_path)            fprintf(f, "ar_path = \"%s\"\n", tc->ar_path);
        fclose(f);
    }
    free(cp);
}

static int read_cache(Toolchain* tc) {
    char* cp = toolchain_cache_path();
    if (!cp) return 0;
    int ok = 0;
    if (access(cp, R_OK) == 0) {
        char buf[4096];
        snprintf(buf, sizeof buf, "%s/rook_cache_used", cp); (void)buf;
        FILE* f = fopen(cp, "r");
        if (f) {
            char line[8192];
            char cc_path[4096] = "", cc_vendor[256] = "", cc_version[8192] = "", ar_path[4096] = "";
            int c11 = 0, c17 = 0;
            while (fgets(line, sizeof line, f)) {
                char k[256], v[8192];
                if (sscanf(line, "%255[^=]=%8191[^\n]", k, v) != 2) continue;
                size_t kl = strlen(k); while (kl && (k[kl-1]==' '||k[kl-1]=='\t')) k[--kl]=0;
                size_t vl = strlen(v);
                while (vl && (v[0]==' '||v[0]=='\t')) { memmove(v, v+1, vl); vl--; v[vl]=0; }
                if (v[0]=='\"') { memmove(v, v+1, vl); vl--; if (vl && v[vl-1]=='\"') v[--vl]=0; }
                if      (strcmp(k, "cc_path")==0)    snprintf(cc_path, sizeof cc_path, "%s", v);
                else if (strcmp(k, "cc_vendor")==0)  snprintf(cc_vendor, sizeof cc_vendor, "%s", v);
                else if (strcmp(k, "cc_version")==0) snprintf(cc_version, sizeof cc_version, "%s", v);
                else if (strcmp(k, "ar_path")==0)    snprintf(ar_path, sizeof ar_path, "%s", v);
                else if (strcmp(k, "supports_c11")==0) c11 = atoi(v);
                else if (strcmp(k, "supports_c17")==0) c17 = atoi(v);
            }
            fclose(f);
            if (cc_path[0] && access(cc_path, X_OK) == 0) {
                tc->cc_path = strdup(cc_path);
                tc->cc_vendor = cc_vendor[0] ? strdup(cc_vendor) : NULL;
                tc->cc_version = cc_version[0] ? strdup(cc_version) : NULL;
                tc->supports_c11 = c11;
                tc->supports_c17 = c17;
                tc->ar_path = ar_path[0] ? strdup(ar_path) : NULL;
                ok = 1;
            }
        }
    }
    free(cp);
    return ok;
}

int toolchain_detect(Toolchain* tc) {
    memset(tc, 0, sizeof *tc);

    RookConfig cfg;
    config_load(&cfg);

    int cc_overridden = config_key_source("cc") != 0;
    int ar_overridden = config_key_source("ar") != 0;

    /* cc: explicit override wins; else cached detection; else fresh detect. */
    char* p = cc_overridden ? resolve_exe(cfg.cc) : NULL;
    if (p) {
        tc->cc_path = p;
        tc->from_config = 1;
    } else if (!cc_overridden && read_cache(tc) && tc->cc_path) {
        /* nothing: reused from cache */
    } else {
        for (int i = 0; CC_CANDS[i]; i++) {
            char* q = find_in_path(CC_CANDS[i]);
            if (q) { tc->cc_path = q; break; }
        }
    }

    if (!tc->cc_path) {
        config_free(&cfg);
        return 1;
    }

    const char* b = strrchr(tc->cc_path, '/');
    b = b ? b + 1 : tc->cc_path;
    tc->cc_name = strdup(b);

    if (!tc->cc_vendor) probe_version(tc);
    if (tc->cc_vendor && strcmp(tc->cc_vendor, "gcc") == 0 && !tc->cc_version) {
        char* dv = probe_dumpversion(tc->cc_path);
        if (dv) { tc->cc_version = dv; }
    }
    if (!tc->cc_version) probe_version(tc); /* ensure we have something */
    if (!tc->supports_c11) tc->supports_c11 = probe_std(tc->cc_path, "c11");
    if (!tc->supports_c17) tc->supports_c17 = probe_std(tc->cc_path, "c17");
    if (!tc->supports_c23) tc->supports_c23 = probe_std(tc->cc_path, "c23");

    /* ar */
    char* ap = ar_overridden ? resolve_exe(cfg.ar) : NULL;
    if (ap) {
        tc->ar_path = ap;
    } else if (!ar_overridden && tc->ar_path) {
        /* already loaded from cache */
    } else {
        for (int i = 0; AR_CANDS[i]; i++) {
            char* q = find_in_path(AR_CANDS[i]);
            if (q) { tc->ar_path = q; break; }
        }
    }

    /* Cache the freshly detected (non-overridden) result. */
    if (!cc_overridden && !ar_overridden) write_cache(tc);

    config_free(&cfg);
    return 0;
}

void toolchain_free(Toolchain* tc) {
    if (!tc) return;
    free(tc->cc_path);
    free(tc->cc_name);
    free(tc->cc_vendor);
    free(tc->cc_version);
    free(tc->ar_path);
    memset(tc, 0, sizeof *tc);
}

/* Return the C compiler binary to invoke, honoring config and falling back to
   auto-detection. Never returns NULL (falls back to "gcc" so callers stay
   runnable). Caller frees the result. */
char* toolchain_cc(void) {
    Toolchain tc;
    if (toolchain_detect(&tc) == 0) {
        char* r = tc.cc_path ? strdup(tc.cc_path) : strdup("gcc");
        toolchain_free(&tc);
        return r;
    }
    return strdup("gcc");
}

/* Compile a single .c file into an executable, honoring the resolved compiler
   and the user's `cflags` / `standard` config. Always links `-lm`. Returns the
   system() status (0 on success).

   The C code Rookal emits uses C23 features (`auto` type inference), so the
   effective standard must be C23-or-later. We honor an explicit `standard`
   override from config; otherwise we pick a C23-capable standard (preferring
   gnu23) when the toolchain supports it, so the output builds on both gcc and
   clang. */
int toolchain_compile_exe(const char* out_exe, const char* c_file) {
    RookConfig cfg;
    config_load(&cfg);

    Toolchain tc;
    toolchain_detect(&tc);
    char* cc = tc.cc_path ? strdup(tc.cc_path) : strdup("gcc");

    const char* std = cfg.standard && cfg.standard[0] ? cfg.standard : "c11";
    int user_std = config_key_source("standard") != 0;
    if (!user_std && tc.supports_c23) std = "gnu23";

    char cmd[16384];
    int n = snprintf(cmd, sizeof cmd, "%s", cc);
    if (std && std[0]) {
        n += snprintf(cmd + n, sizeof cmd - (size_t)n, " -std=%s", std);
    }
    if (cfg.cflags && cfg.cflags[0]) {
        n += snprintf(cmd + n, sizeof cmd - (size_t)n, " %s", cfg.cflags);
    }
    n += snprintf(cmd + n, sizeof cmd - (size_t)n, " -o %s %s -lm",
                  out_exe ? out_exe : "a.out", c_file ? c_file : "a.c");

    int rc = system(cmd);
    free(cc);
    toolchain_free(&tc);
    config_free(&cfg);
    return rc;
}

int toolchain_compile_obj(const char* out_obj, const char* c_file, const char** inc_dirs, size_t n_inc, const char* extra_cflags) {
    RookConfig cfg;
    config_load(&cfg);

    Toolchain tc;
    toolchain_detect(&tc);
    char* cc = tc.cc_path ? strdup(tc.cc_path) : strdup("gcc");

    const char* std = cfg.standard && cfg.standard[0] ? cfg.standard : "c11";
    int user_std = config_key_source("standard") != 0;
    if (!user_std && tc.supports_c23) std = "gnu23";

    char cmd[32768];
    int n = snprintf(cmd, sizeof cmd, "%s", cc);
    if (std && std[0]) {
        n += snprintf(cmd + n, sizeof cmd - (size_t)n, " -std=%s", std);
    }
    if (cfg.cflags && cfg.cflags[0]) {
        n += snprintf(cmd + n, sizeof cmd - (size_t)n, " %s", cfg.cflags);
    }
    if (extra_cflags && extra_cflags[0]) {
        n += snprintf(cmd + n, sizeof cmd - (size_t)n, " %s", extra_cflags);
    }
    for (size_t i = 0; i < n_inc; i++) {
        n += snprintf(cmd + n, sizeof cmd - (size_t)n, " -I\"%s\"", inc_dirs[i]);
    }
    n += snprintf(cmd + n, sizeof cmd - (size_t)n, " -c -o \"%s\" \"%s\"",
                  out_obj ? out_obj : "out.o", c_file ? c_file : "in.c");

    int rc = system(cmd);
    free(cc);
    toolchain_free(&tc);
    config_free(&cfg);
    return rc;
}

int toolchain_link_exe(const char* out_exe, const char** obj_files, size_t n_objs, const char** libs, size_t n_libs, const char* extra_cflags) {
    RookConfig cfg;
    config_load(&cfg);

    Toolchain tc;
    toolchain_detect(&tc);
    char* cc = tc.cc_path ? strdup(tc.cc_path) : strdup("gcc");

    char cmd[32768];
    int n = snprintf(cmd, sizeof cmd, "%s -o \"%s\"", cc, out_exe ? out_exe : "a.out");
    for (size_t i = 0; i < n_objs; i++) {
        n += snprintf(cmd + n, sizeof cmd - (size_t)n, " \"%s\"", obj_files[i]);
    }
    if (cfg.cflags && cfg.cflags[0]) {
        n += snprintf(cmd + n, sizeof cmd - (size_t)n, " %s", cfg.cflags);
    }
    if (extra_cflags && extra_cflags[0]) {
        n += snprintf(cmd + n, sizeof cmd - (size_t)n, " %s", extra_cflags);
    }
    n += snprintf(cmd + n, sizeof cmd - (size_t)n, " -lm");
    for (size_t i = 0; i < n_libs; i++) {
        n += snprintf(cmd + n, sizeof cmd - (size_t)n, " -l%s", libs[i]);
    }

    int rc = system(cmd);
    free(cc);
    toolchain_free(&tc);
    config_free(&cfg);
    return rc;
}

int toolchain_link_lib(const char* out_lib, const char** obj_files, size_t n_objs, int is_shared, const char* extra_cflags) {
    RookConfig cfg;
    config_load(&cfg);

    Toolchain tc;
    toolchain_detect(&tc);

    char cmd[32768];
    int rc = 0;
    if (is_shared) {
        char* cc = tc.cc_path ? strdup(tc.cc_path) : strdup("gcc");
        int n = snprintf(cmd, sizeof cmd, "%s -shared -o \"%s\"", cc, out_lib ? out_lib : "lib.so");
        for (size_t i = 0; i < n_objs; i++) {
            n += snprintf(cmd + n, sizeof cmd - (size_t)n, " \"%s\"", obj_files[i]);
        }
        if (extra_cflags && extra_cflags[0]) {
            n += snprintf(cmd + n, sizeof cmd - (size_t)n, " %s", extra_cflags);
        }
        rc = system(cmd);
        free(cc);
    } else {
        char* ar = tc.ar_path ? strdup(tc.ar_path) : strdup("ar");
        int n = snprintf(cmd, sizeof cmd, "%s rcs \"%s\"", ar, out_lib ? out_lib : "lib.a");
        for (size_t i = 0; i < n_objs; i++) {
            n += snprintf(cmd + n, sizeof cmd - (size_t)n, " \"%s\"", obj_files[i]);
        }
        rc = system(cmd);
        free(ar);
    }
    toolchain_free(&tc);
    config_free(&cfg);
    return rc;
}
