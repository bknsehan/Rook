#include "toolchain.h"
#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
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

#ifdef _WIN32
#define DEV_NULL "NUL"
#else
#define DEV_NULL "/dev/null"
#endif

/* Capture the first line of `<cc> --version`. */
static void probe_version(Toolchain* tc) {
    const char* exe = tc->cc_path ? tc->cc_path : "cc";
    const char* args[] = { exe, "--version", NULL };
    char out[4096];
    if (util_exec_capture(args, out, sizeof(out)) != 0 && !out[0]) return;
    char* nl = strchr(out, '\n');
    if (nl) *nl = '\0';
    char* cr = strchr(out, '\r');
    if (cr) *cr = '\0';
    if (out[0]) {
        tc->cc_version = strdup(out);
        if (strstr(out, "Apple") && strstr(out, "clang")) tc->cc_vendor = strdup("clang");
        else if (strstr(out, "clang"))                          tc->cc_vendor = strdup("clang");
        else if (strstr(out, "gcc") || strstr(out, "GCC") ||
                 strstr(out, "Free Software Foundation"))       tc->cc_vendor = strdup("gcc");
        else if (strstr(out, "tcc") || strstr(out, "TinyCC"))  tc->cc_vendor = strdup("tcc");
        else                                                   tc->cc_vendor = strdup("unknown");
    }
}

/* For gcc, capture the numeric version via `-dumpversion` (e.g. "11.4.0"). */
static char* probe_dumpversion(const char* cc_path) {
    const char* exe = cc_path ? cc_path : "cc";
    const char* args[] = { exe, "-dumpversion", NULL };
    char out[2048];
    if (util_exec_capture(args, out, sizeof(out)) != 0 && !out[0]) return NULL;
    char* nl = strchr(out, '\n');
    if (nl) *nl = '\0';
    char* cr = strchr(out, '\r');
    if (cr) *cr = '\0';
    return out[0] ? strdup(out) : NULL;
}

static const char* get_tmp_dir(void) {
    const char* t = getenv("TMPDIR");
    if (!t || !*t) t = getenv("TEMP");
    if (!t || !*t) t = getenv("TMP");
    if (!t || !*t) t = "/tmp";
    return (access(t, W_OK) == 0) ? t : ".";
}

/* Try to compile `int main(){return 0;}` with `-std=<std>`; return 1 if the
   compiler accepts it. Uses secure mkstemps and direct execution without shell. */
static int probe_std(const char* cc, const char* std) {
    char path[4096];
    snprintf(path, sizeof path, "%s/rook_probe_XXXXXX.c", get_tmp_dir());
#ifdef _WIN32
    if (_mktemp_s(path, sizeof(path)) != 0) return 0;
    FILE* f = fopen(path, "w");
    if (!f) return 0;
    fputs("int main(void){return 0;}\n", f);
    fclose(f);
#else
    int fd = mkstemps(path, 2);
    if (fd < 0) return 0;
    const char* content = "int main(void){return 0;}\n";
    if (write(fd, content, strlen(content)) < 0) {
        close(fd);
        unlink(path);
        return 0;
    }
    close(fd);
#endif

    char std_arg[64];
    snprintf(std_arg, sizeof(std_arg), "-std=%s", std);
    const char* exe = cc ? cc : "cc";
    const char* args[] = { exe, std_arg, "-c", "-o", DEV_NULL, path, NULL };
    int rc = util_exec(args);
    unlink(path);
    return rc == 0;
}

/* Auto-discover Android NDK root path if installed. Caller frees returned string. */
char* toolchain_find_ndk(const char* explicit_path) {
    if (explicit_path && explicit_path[0]) {
        char probe[4096];
        snprintf(probe, sizeof probe, "%s/toolchains/llvm/prebuilt", explicit_path);
        if (access(probe, X_OK) == 0) return strdup(explicit_path);
    }
    const char* env_ndk = getenv("ANDROID_NDK_HOME");
    if (!env_ndk || !*env_ndk) env_ndk = getenv("ANDROID_NDK_ROOT");
    if (env_ndk && *env_ndk) {
        char probe[4096];
        snprintf(probe, sizeof probe, "%s/toolchains/llvm/prebuilt", env_ndk);
        if (access(probe, X_OK) == 0) return strdup(env_ndk);
    }
    const char* env_sdk = getenv("ANDROID_HOME");
    if (!env_sdk || !*env_sdk) env_sdk = getenv("ANDROID_SDK_ROOT");
    if (env_sdk && *env_sdk) {
        /* Check $ANDROID_HOME/ndk/<version> */
        char ndk_dir[4096];
        snprintf(ndk_dir, sizeof ndk_dir, "%s/ndk", env_sdk);
        DIR* d = opendir(ndk_dir);
        if (d) {
            struct dirent* de;
            char best[4096] = "";
            while ((de = readdir(d)) != NULL) {
                if (de->d_name[0] == '.') continue;
                char cand[4096];
                snprintf(cand, sizeof cand, "%.3800s/%s", ndk_dir, de->d_name);
                char probe[4096];
                snprintf(probe, sizeof probe, "%.3800s/toolchains/llvm/prebuilt", cand);
                if (access(probe, X_OK) == 0 && strcmp(cand, best) > 0) {
                    snprintf(best, sizeof best, "%s", cand);
                }
            }
            closedir(d);
            if (best[0]) return strdup(best);
        }
        char probe[4096];
        snprintf(probe, sizeof probe, "%.3800s/toolchains/llvm/prebuilt", env_sdk);
        if (access(probe, X_OK) == 0) return strdup(env_sdk);
    }

    const char* standard_paths[] = {
        "/opt/android-ndk",
        "/opt/android-sdk/ndk",
        NULL
    };
    for (int i = 0; standard_paths[i]; i++) {
        char probe[4096];
        snprintf(probe, sizeof probe, "%.3800s/toolchains/llvm/prebuilt", standard_paths[i]);
        if (access(probe, X_OK) == 0) return strdup(standard_paths[i]);
        DIR* d = opendir(standard_paths[i]);
        if (d) {
            struct dirent* de;
            char best[4096] = "";
            while ((de = readdir(d)) != NULL) {
                if (de->d_name[0] == '.') continue;
                char cand[4096];
                snprintf(cand, sizeof cand, "%.3800s/%s", standard_paths[i], de->d_name);
                snprintf(probe, sizeof probe, "%.3800s/toolchains/llvm/prebuilt", cand);
                if (access(probe, X_OK) == 0 && strcmp(cand, best) > 0) {
                    snprintf(best, sizeof best, "%s", cand);
                }
            }
            closedir(d);
            if (best[0]) return strdup(best);
        }
    }
    const char* home = getenv("HOME");
    if (home) {
        char user_sdk_ndk[4096];
        snprintf(user_sdk_ndk, sizeof user_sdk_ndk, "%.3800s/Android/Sdk/ndk", home);
        DIR* d = opendir(user_sdk_ndk);
        if (d) {
            struct dirent* de;
            char best[4096] = "";
            while ((de = readdir(d)) != NULL) {
                if (de->d_name[0] == '.') continue;
                char cand[4096];
                snprintf(cand, sizeof cand, "%.3800s/%s", user_sdk_ndk, de->d_name);
                char probe[4096];
                snprintf(probe, sizeof probe, "%.3800s/toolchains/llvm/prebuilt", cand);
                if (access(probe, X_OK) == 0 && strcmp(cand, best) > 0) {
                    snprintf(best, sizeof best, "%s", cand);
                }
            }
            closedir(d);
            if (best[0]) return strdup(best);
        }
    }
    return NULL;
}

/* Discover Windows cross-compiler on Linux (e.g. x86_64-w64-mingw32-gcc). Caller frees returned string. */
char* toolchain_find_mingw(void) {
    char* p = find_in_path("x86_64-w64-mingw32-gcc");
    if (p) return p;
    p = find_in_path("x86_64-w64-mingw32-clang");
    if (p) return p;
    return NULL;
}

static const char* CC_CANDS[] = { "gcc", "clang", "cc", "tcc", "musl-gcc", NULL };
static const char* AR_CANDS[] = { "ar", "llvm-ar", "gcc-ar", "mingw-ar", NULL };

/* Read / write toolchain cache to avoid re-detecting on every compile. */
static char* toolchain_cache_path(void) {
    const char* xdg = getenv("XDG_CACHE_HOME");
    char base[4096];
    if (xdg && *xdg) {
        snprintf(base, sizeof base, "%s/rokade", xdg);
    } else {
        const char* home = getenv("HOME");
        if (!home) return NULL;
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

int toolchain_detect_target(Toolchain* tc, const TargetSpec* spec) {
    memset(tc, 0, sizeof *tc);
    const char* tos = (spec && spec->target_os[0]) ? spec->target_os : "host";

    /* 1. Explicit compiler override in spec */
    if (spec && spec->custom_cc[0]) {
        char* resolved = resolve_exe(spec->custom_cc);
        if (resolved) {
            tc->cc_path = resolved;
            tc->from_config = 1;
        }
    }

    /* 2. Target OS: Android */
    if (!tc->cc_path && strcmp(tos, "android") == 0) {
        char* ndk = toolchain_find_ndk(spec ? spec->ndk_path : NULL);
        if (!ndk) return 1; /* NDK not found */

        const char* hosts[] = { "linux-x86_64", "windows-x86_64", "darwin-x86_64", "darwin-arm64", NULL };
        char prebuilt_bin[4096] = "";
        for (int i = 0; hosts[i]; i++) {
            char pdir[4096];
            snprintf(pdir, sizeof pdir, "%s/toolchains/llvm/prebuilt/%s/bin", ndk, hosts[i]);
            if (access(pdir, X_OK) == 0) {
                snprintf(prebuilt_bin, sizeof prebuilt_bin, "%s", pdir);
                break;
            }
        }
        if (!prebuilt_bin[0]) { free(ndk); return 1; }

        const char* arch = (spec && spec->target_arch[0]) ? spec->target_arch : "arm64-v8a";
        const char* triple_prefix = "aarch64-linux-android";
        if (strcmp(arch, "x86_64") == 0) {
            triple_prefix = "x86_64-linux-android";
        } else if (strcmp(arch, "armeabi-v7a") == 0 || strcmp(arch, "armv7") == 0 || strcmp(arch, "arm") == 0) {
            triple_prefix = "armv7a-linux-androideabi";
        } else if (strcmp(arch, "x86") == 0 || strcmp(arch, "i686") == 0) {
            triple_prefix = "i686-linux-android";
        }

        int api = (spec && spec->android_api > 0) ? spec->android_api : 24;
        char cc_bin[4096];
        snprintf(cc_bin, sizeof cc_bin, "%s/%s%d-clang", prebuilt_bin, triple_prefix, api);
        if (access(cc_bin, X_OK) != 0) {
            snprintf(cc_bin, sizeof cc_bin, "%s/clang", prebuilt_bin);
        }
        tc->cc_path = strdup(cc_bin);

        char ar_bin[4096];
        snprintf(ar_bin, sizeof ar_bin, "%s/llvm-ar", prebuilt_bin);
        if (access(ar_bin, X_OK) == 0) tc->ar_path = strdup(ar_bin);

        char trip_buf[128];
        snprintf(trip_buf, sizeof trip_buf, "%s%d", triple_prefix, api);
        tc->target_triple = strdup(trip_buf);
        tc->cc_vendor = strdup("clang");
        free(ndk);
    }

    /* 3. Target OS: Windows */
    if (!tc->cc_path && strcmp(tos, "windows") == 0) {
#if defined(_WIN32)
        const char* win_cands[] = { "gcc", "clang", "cl", NULL };
        for (int i = 0; win_cands[i]; i++) {
            char* q = find_in_path(win_cands[i]);
            if (q) { tc->cc_path = q; break; }
        }
        tc->target_triple = strdup("x86_64-w64-windows");
#else
        char* mingw_cc = toolchain_find_mingw();
        if (mingw_cc) {
            tc->cc_path = mingw_cc;
            tc->target_triple = strdup("x86_64-w64-mingw32");
            char* mingw_ar = find_in_path("x86_64-w64-mingw32-ar");
            if (mingw_ar) tc->ar_path = mingw_ar;
        }
#endif
    }

    /* 4. Host OS / Linux / fallback */
    if (!tc->cc_path) {
        const char* env_cc = getenv("ROKADE_CC");
        if (!env_cc || !*env_cc) env_cc = getenv("CC");
        if (env_cc && *env_cc) {
            tc->cc_path = resolve_exe(env_cc);
        }
        if (!tc->cc_path) {
            RookConfig cfg;
            config_load(&cfg);
            int cc_overridden = config_key_source("cc") != 0;
            if (cc_overridden) {
                tc->cc_path = resolve_exe(cfg.cc);
                tc->from_config = 1;
            } else if (read_cache(tc) && tc->cc_path) {
                /* reused from cache */
            } else {
                for (int i = 0; CC_CANDS[i]; i++) {
                    char* q = find_in_path(CC_CANDS[i]);
                    if (q) { tc->cc_path = q; break; }
                }
            }
            config_free(&cfg);
        }
    }

    if (!tc->cc_path) return 1;

    const char* b = strrchr(tc->cc_path, '/');
    const char* b2 = strrchr(tc->cc_path, '\\');
    if (b2 && (!b || b2 > b)) b = b2;
    b = b ? b + 1 : tc->cc_path;
    tc->cc_name = strdup(b);

    if (!tc->cc_vendor) probe_version(tc);
    if (tc->cc_vendor && strcmp(tc->cc_vendor, "gcc") == 0 && !tc->cc_version) {
        char* dv = probe_dumpversion(tc->cc_path);
        if (dv) { tc->cc_version = dv; }
    }
    if (!tc->cc_version) probe_version(tc);
    if (!tc->supports_c11) tc->supports_c11 = probe_std(tc->cc_path, "c11");
    if (!tc->supports_c17) tc->supports_c17 = probe_std(tc->cc_path, "c17");
    if (!tc->supports_c23) tc->supports_c23 = probe_std(tc->cc_path, "c23");

    /* Archiver */
    if (!tc->ar_path) {
        if (spec && spec->custom_ar[0]) {
            tc->ar_path = resolve_exe(spec->custom_ar);
        }
        if (!tc->ar_path) {
            const char* env_ar = getenv("ROKADE_AR");
            if (!env_ar || !*env_ar) env_ar = getenv("AR");
            if (env_ar && *env_ar) tc->ar_path = resolve_exe(env_ar);
        }
        if (!tc->ar_path) {
            for (int i = 0; AR_CANDS[i]; i++) {
                char* q = find_in_path(AR_CANDS[i]);
                if (q) { tc->ar_path = q; break; }
            }
        }
    }

    write_cache(tc);
    return 0;
}

int toolchain_detect(Toolchain* tc) {
    return toolchain_detect_target(tc, NULL);
}

void toolchain_free(Toolchain* tc) {
    if (!tc) return;
    free(tc->cc_path);
    free(tc->cc_name);
    free(tc->cc_vendor);
    free(tc->cc_version);
    free(tc->ar_path);
    free(tc->target_triple);
    memset(tc, 0, sizeof *tc);
}

char* toolchain_cc(void) {
    Toolchain tc;
    if (toolchain_detect(&tc) == 0) {
        char* r = tc.cc_path ? strdup(tc.cc_path) : strdup("gcc");
        toolchain_free(&tc);
        return r;
    }
    return strdup("gcc");
}

int toolchain_compile_exe(const char* out_exe, const char* c_file) {
    Toolchain tc;
    toolchain_detect(&tc);
    TargetSpec spec;
    memset(&spec, 0, sizeof spec);
    snprintf(spec.build_kind, sizeof spec.build_kind, "exe");
    const char* objs[1] = { c_file };
    int rc = toolchain_link_target(&spec, &tc, out_exe, objs, 1, NULL, 0, NULL);
    toolchain_free(&tc);
    return rc;
}

int toolchain_compile_obj_target(const TargetSpec* spec, const Toolchain* tc, const char* out_obj, const char* c_file, const char** inc_dirs, size_t n_inc, const char* extra_cflags) {
    const char* cc = tc && tc->cc_path ? tc->cc_path : "gcc";
    const char* std = (spec && spec->standard[0]) ? spec->standard : "c2x";

    ArgVec av;
    argvec_init(&av);
    argvec_add(&av, cc);
    if (std && std[0]) {
        char std_buf[64];
        snprintf(std_buf, sizeof(std_buf), "-std=%s", std);
        argvec_add(&av, std_buf);
    }
    if (spec && (strcmp(spec->target_os, "android") == 0 || strcmp(spec->build_kind, "shared-lib") == 0)) {
        argvec_add(&av, "-fPIC");
    }
    if (spec && spec->cflags[0]) {
        argvec_split_and_add(&av, spec->cflags);
    }
    if (extra_cflags && extra_cflags[0]) {
        argvec_split_and_add(&av, extra_cflags);
    }
    for (size_t i = 0; i < n_inc; i++) {
        char inc_buf[4096];
        snprintf(inc_buf, sizeof(inc_buf), "-I%s", inc_dirs[i]);
        argvec_add(&av, inc_buf);
    }
    argvec_add(&av, "-c");
    argvec_add(&av, "-o");
    argvec_add(&av, out_obj ? out_obj : "out.o");
    argvec_add(&av, c_file ? c_file : "in.c");

    int ret = util_exec((const char* const*)av.args);
    argvec_free(&av);
    return ret;
}

int toolchain_compile_obj(const char* out_obj, const char* c_file, const char** inc_dirs, size_t n_inc, const char* extra_cflags) {
    Toolchain tc;
    toolchain_detect(&tc);
    int rc = toolchain_compile_obj_target(NULL, &tc, out_obj, c_file, inc_dirs, n_inc, extra_cflags);
    toolchain_free(&tc);
    return rc;
}

int toolchain_link_target(const TargetSpec* spec, const Toolchain* tc, const char* out_bin, const char** obj_files, size_t n_objs, const char** libs, size_t n_libs, const char* extra_cflags) {
    const char* kind = (spec && spec->build_kind[0]) ? spec->build_kind : "exe";
    const char* tos = (spec && spec->target_os[0]) ? spec->target_os : "linux";

    if (out_bin) unlink(out_bin);

    ArgVec av;
    argvec_init(&av);

    if (strcmp(kind, "static-lib") == 0 || strcmp(kind, "lib") == 0 || strcmp(kind, "library") == 0) {
        const char* ar = tc && tc->ar_path ? tc->ar_path : "ar";
        argvec_add(&av, ar);
        argvec_add(&av, "rcs");
        argvec_add(&av, out_bin ? out_bin : "lib.a");
        for (size_t i = 0; i < n_objs; i++) {
            argvec_add(&av, obj_files[i]);
        }
        int ret = util_exec((const char* const*)av.args);
        argvec_free(&av);
        if (ret == 0 && out_bin && access(out_bin, F_OK) != 0) ret = 1;
        return ret;
    }

    const char* cc = tc && tc->cc_path ? tc->cc_path : "gcc";
    argvec_add(&av, cc);

    if (strcmp(kind, "shared-lib") == 0) {
        argvec_add(&av, "-shared");
        if (strcmp(tos, "windows") != 0) {
            argvec_add(&av, "-fPIC");
        }
    }

    argvec_add(&av, "-o");
    argvec_add(&av, out_bin ? out_bin : "a.out");
    for (size_t i = 0; i < n_objs; i++) {
        argvec_add(&av, obj_files[i]);
    }
    if (spec && spec->cflags[0]) {
        argvec_split_and_add(&av, spec->cflags);
    }
    if (extra_cflags && extra_cflags[0]) {
        argvec_split_and_add(&av, extra_cflags);
    }
    if (strcmp(tos, "windows") != 0 && (!tc || !tc->cc_vendor || strcmp(tc->cc_vendor, "cl") != 0)) {
        argvec_add(&av, "-lm");
    }
    for (size_t i = 0; i < n_libs; i++) {
        char lib_buf[512];
        snprintf(lib_buf, sizeof(lib_buf), "-l%s", libs[i]);
        argvec_add(&av, lib_buf);
    }

    int ret = util_exec((const char* const*)av.args);
    argvec_free(&av);
    if (ret == 0 && out_bin && access(out_bin, F_OK) != 0) ret = 1;
    return ret;
}

int toolchain_link_exe(const char* out_exe, const char** obj_files, size_t n_objs, const char** libs, size_t n_libs, const char* extra_cflags) {
    Toolchain tc;
    toolchain_detect(&tc);
    int rc = toolchain_link_target(NULL, &tc, out_exe, obj_files, n_objs, libs, n_libs, extra_cflags);
    toolchain_free(&tc);
    return rc;
}

int toolchain_link_lib(const char* out_lib, const char** obj_files, size_t n_objs, int is_shared, const char* extra_cflags) {
    Toolchain tc;
    toolchain_detect(&tc);
    TargetSpec spec;
    memset(&spec, 0, sizeof spec);
    snprintf(spec.build_kind, sizeof spec.build_kind, "%s", is_shared ? "shared-lib" : "static-lib");
    int rc = toolchain_link_target(&spec, &tc, out_lib, obj_files, n_objs, NULL, 0, extra_cflags);
    toolchain_free(&tc);
    return rc;
}
