#ifndef RK_LLVM_BACKEND_H
#define RK_LLVM_BACKEND_H

#include "backend.h"

/* Creates an LLVM backend instance. Returns NULL if ROKADE_HAS_LLVM is not enabled. */
Backend* llvm_backend_create(void);

/* Direct native object file emission (.o). Returns 0 on success, non-zero on error. */
int llvm_backend_emit_obj(Sema* sema, Program* prog, const char* obj_path, int opt_level);

/* In-memory JIT execution of program. Returns main's exit code, or -1 on failure. */
int llvm_backend_jit_run(Sema* sema, Program* prog, int argc, char** argv);

/* Compile an LLVM IR file (.ll) directly to an object file (.o) using LLVM TargetMachine. Returns 0 on success. */
int llvm_backend_compile_ll_to_obj(const char* ll_path, const char* obj_path, int opt_level);

#endif
