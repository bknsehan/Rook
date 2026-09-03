#ifndef RK_TOOLCHAIN_H
#define RK_TOOLCHAIN_H

#include <stddef.h>

/* Resolved C toolchain. All string fields are malloc'd (or NULL) and freed
   with toolchain_free. */
typedef struct {
    char* cc_path;     /* resolved C compiler path (malloc'd) */
    char* cc_name;     /* base name, e.g. "gcc" */
    char* cc_vendor;   /* "gcc" | "clang" | "tcc" | "unknown" */
    char* cc_version;  /* first line of `<cc> --version` (malloc'd) */
    int   supports_c11;
    int   supports_c17;
    int   supports_c23;   /* the emitted Rook->C code requires C23 (uses `auto`) */
    char* ar_path;     /* resolved archiver path (malloc'd or NULL) */
    int   from_config; /* 1 if cc came from an explicit config override */
    char* target_triple; /* e.g. "aarch64-linux-android24", "x86_64-w64-mingw32" */
} Toolchain;

typedef struct {
    char target_os[32];     /* "linux", "android", "windows" (or empty for host) */
    char target_arch[32];   /* "x86_64", "arm64-v8a", "armeabi-v7a", "x86" */
    int  android_api;       /* e.g. 21, 24, 33 (default 24) */
    char ndk_path[4096];    /* explicit NDK path override */
    char custom_cc[4096];   /* explicit compiler path override */
    char custom_ar[4096];   /* explicit archiver path override */
    char cflags[4096];      /* extra target flags */
    char standard[16];      /* C standard */
    char build_kind[32];    /* "exe", "shared-lib", "static-lib" */
} TargetSpec;

/* Detect the C toolchain for the host platform, honoring config overrides. */
int  toolchain_detect(Toolchain* tc);

/* Detect toolchain targeting a specific platform spec. */
int  toolchain_detect_target(Toolchain* tc, const TargetSpec* spec);

void toolchain_free(Toolchain* tc);

/* Auto-discover Android NDK root path if installed. Caller frees returned string. */
char* toolchain_find_ndk(const char* explicit_path);

/* Discover Windows cross-compiler on Linux (e.g. x86_64-w64-mingw32-gcc). Caller frees returned string. */
char* toolchain_find_mingw(void);

/* Resolve the C compiler command to invoke, honoring config and falling back
   to auto-detection. Never returns NULL (falls back to "gcc" so callers stay
   runnable). Caller frees the result. */
char* toolchain_cc(void);

/* Compile a single .c file into an executable, honoring the resolved compiler
   and the user's `cflags` config. Always links `-lm`. Returns the system()
   status (0 on success). */
int toolchain_compile_exe(const char* out_exe, const char* c_file);

/* Compile a single .c file into a .o object file. */
int toolchain_compile_obj(const char* out_obj, const char* c_file, const char** inc_dirs, size_t n_inc, const char* extra_cflags);

/* Link object files into an executable. */
int toolchain_link_exe(const char* out_exe, const char** obj_files, size_t n_objs, const char** libs, size_t n_libs, const char* extra_cflags);

/* Link object files into a static or shared library. */
int toolchain_link_lib(const char* out_lib, const char** obj_files, size_t n_objs, int is_shared, const char* extra_cflags);

/* Target-aware object compilation. */
int toolchain_compile_obj_target(const TargetSpec* spec, const Toolchain* tc, const char* out_obj, const char* c_file, const char** inc_dirs, size_t n_inc, const char* extra_cflags);

/* Target-aware linking (handles exe, shared-lib, static-lib, extensions, -fPIC, -shared). */
int toolchain_link_target(const TargetSpec* spec, const Toolchain* tc, const char* out_bin, const char** obj_files, size_t n_objs, const char** libs, size_t n_libs, const char* extra_cflags);

#endif
