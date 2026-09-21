#ifndef RK_RESOLVE_H
#define RK_RESOLVE_H

#include <stddef.h>

/* Locate the Rook installation directory and standard library directory. */
int rokade_get_install_root(char* buf, size_t cap);
int rokade_get_std_dir(char* out_std, size_t cap);

/* Resolve an include path, searching basedir first, then include dirs.
   Also checks common package entrypoints (src/<name>.rook, src/lib.rook, lib.rook, main.rook).
   Returns a malloc'd path or NULL if not found or outside jail. */
char* resolve_include_path(const char* incpath, const char* basedir,
                           const char** inc_dirs, size_t n_inc);

/* Rewrite #comprise and comprise directives into canonical #include "..." directives.
   Caller frees returned string. */
char* rook_rewrites_includes(const char* src, int src_len);

/* Recursively resolve #include "file.rook" and #comprise directives in source text.
   C includes (#include <header.h>) are passed through verbatim.
   Returns a malloc'd string with includes expanded, or NULL on error. */
char* resolve_includes(const char* src, int src_len, const char* basedir,
                       const char** inc_dirs, size_t n_inc, int depth,
                       const char* current_file);

/* Resolve a C header include path (e.g. "stdio.h" or "sys/stat.h" or "myheader.h").
   Searches basedir, inc_dirs, and standard system include directories.
   Returns a malloc'd path or NULL if not found. */
char* resolve_c_header_path(const char* header, const char* basedir,
                            const char** inc_dirs, size_t n_inc);

#endif

