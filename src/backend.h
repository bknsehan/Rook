#ifndef RK_BACKEND_H
#define RK_BACKEND_H

#include "ast.h"
#include "sema.h"

/* A Backend turns a checked Rook program into target code. The C backend is
   the only implementation today; an LLVM/native backend can be added later by
   implementing the same interface without touching the parser or semantic
   analyzer. */
typedef struct Backend {
    const char* name;
    /* Emit target code for `prog` (already semantically checked). Returns a
       malloc'd string and writes its length to *out_len. */
    char* (*emit_program)(Sema* sema, Program* prog, int* out_len, int bounds_check);
    /* Direct native object file emission (.o). Returns 0 on success. (Optional, can be NULL) */
    int (*emit_obj)(Sema* sema, Program* prog, const char* obj_path, int opt_level);
    /* In-memory JIT execution. Returns main's exit code or negative on failure. (Optional, can be NULL) */
    int (*jit_run)(Sema* sema, Program* prog, int argc, char** argv);
    void (*destroy)(struct Backend* b);
} Backend;

/* Create a backend by name ("c" today). Returns NULL if unknown. */
Backend* backend_create(const char* name);

/* Create the C backend directly. */
Backend* c_backend_create(void);

/* Create the LLVM backend directly. */
Backend* llvm_backend_create(void);

void backend_destroy(Backend* b);

#endif
