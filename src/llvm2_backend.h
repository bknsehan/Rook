#ifndef RK_LLVM2_BACKEND_H
#define RK_LLVM2_BACKEND_H

#include "backend.h"

/* Creates the next-generation LLVM2 backend instance. Returns NULL if ROKADE_HAS_LLVM is not enabled. */
Backend* llvm2_backend_create(void);

/* Direct native object file emission (.o). Returns 0 on success, non-zero on error. */
int llvm2_backend_emit_obj(Sema* sema, Program* prog, const char* obj_path, int opt_level);
int llvm2_backend_emit_obj_target(Sema* sema, Program* prog, const char* obj_path, int opt_level, const char* target_triple);
char* llvm2_backend_emit_program_target(Sema* sema, Program* prog, int* out_len, int bounds_check, const char* target_triple);

/* In-memory JIT execution of program. Returns main's exit code, or -1 on failure. */
int llvm2_backend_jit_run(Sema* sema, Program* prog, int argc, char** argv);

/* Compile an LLVM IR file (.ll) directly to an object file (.o) using LLVM TargetMachine. Returns 0 on success. */
int llvm2_backend_compile_ll_to_obj(const char* ll_path, const char* obj_path, int opt_level);
int llvm2_backend_compile_ll_to_obj_target(const char* ll_path, const char* obj_path, int opt_level, const char* target_triple);

#endif
