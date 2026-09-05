#ifndef RK_UTIL_H
#define RK_UTIL_H

#include <stddef.h>

typedef struct SB {
    char* data;
    int len;
    int cap;
} SB;

void sb_init(SB* sb);
void sb_free(SB* sb);
void sb_reset(SB* sb);
void sb_append(SB* sb, const char* s);
void sb_appendn(SB* sb, const char* s, int n);
void sb_appendf(SB* sb, const char* fmt, ...);
char* sb_strdup(SB* sb);

char* util_read_file(const char* path, int* out_len);
int util_endswith(const char* s, const char* suffix);

/* Dynamic argument vector for shell-free execution */
typedef struct ArgVec {
    char** args;
    size_t count;
    size_t cap;
} ArgVec;

void argvec_init(ArgVec* v);
void argvec_add(ArgVec* v, const char* arg);
void argvec_split_and_add(ArgVec* v, const char* str);
void argvec_free(ArgVec* v);

/* Direct process execution without shell. Returns process exit code. */
int util_exec(const char* const* argv);

/* Direct process execution with captured stdout/stderr. Returns exit code. */
int util_exec_capture(const char* const* argv, char* out_buf, size_t out_cap);

/* Portable recursive directory deletion (replaces shell "rm -rf") */
int util_rm_rf(const char* path);

#endif
