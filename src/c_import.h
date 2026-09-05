#ifndef RK_C_IMPORT_H
#define RK_C_IMPORT_H

#include "sema.h"
#include "ast.h"
#include <stddef.h>

void c_import_init(void);

/* Imports declarations from a C code string using libclang. */
int c_import_code(Sema* sema, const char* code, const char** inc_dirs, size_t n_inc);

/* Imports declarations from a C header file using libclang.
   is_system: 1 for <header>, 0 for "header". */
int c_import_header(Sema* sema, const char* header, int is_system, const char** inc_dirs, size_t n_inc);

/* Scans the source text for C header #include directives and imports them. */
int c_import_scan_and_load(Sema* sema, const char* src, int len, const char* basedir, const char** inc_dirs, size_t n_inc);

/* Extracts all TOP_RAW slices from the parsed program and imports their C declarations. */
int c_import_program_raw(Sema* sema, Program* prog, const char** inc_dirs, size_t n_inc);

#endif
