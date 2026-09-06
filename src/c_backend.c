#include <stdlib.h>
#include "backend.h"
#include "codegen.h"
#include "util.h"

static void c_backend_destroy(Backend* b) {
    free(b);
}

static char* c_backend_emit_program_target(Sema* sema, Program* prog, int* out_len, int bounds_check, const char* target_triple) {
    (void)target_triple;
    return codegen_program(sema, prog, out_len, bounds_check);
}

Backend* c_backend_create(void) {
    Backend* b = calloc(1, sizeof(Backend));
    if (!b) return NULL;
    b->name = "c";
    b->emit_program = codegen_program;
    b->emit_program_target = c_backend_emit_program_target;
    b->destroy = c_backend_destroy;
    return b;
}
