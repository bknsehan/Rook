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
} Toolchain;

/* Detect the C toolchain, honoring config overrides (cc/ar). Returns 0 on
   success (a C compiler was located); non-zero if none could be found. */
int  toolchain_detect(Toolchain* tc);

void toolchain_free(Toolchain* tc);

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

#endif
