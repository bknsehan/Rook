#include <stdlib.h>
#include "backend.h"
#include "codegen.h"
#include "util.h"

static void c_backend_destroy(Backend* b) {
    free(b);
}

Backend* c_backend_create(void) {
    Backend* b = calloc(1, sizeof(Backend));
    if (!b) return NULL;
    b->name = "c";
    b->emit_program = codegen_program;
    b->destroy = c_backend_destroy;
    return b;
}
