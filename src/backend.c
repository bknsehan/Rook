#include <stddef.h>
#include <string.h>
#include "backend.h"
#include "util.h"

Backend* backend_create(const char* name) {
    if (name && strcmp(name, "c") == 0)
        return c_backend_create();
    if (name && strcmp(name, "llvm") == 0)
        return llvm_backend_create();
    return NULL;
}

void backend_destroy(Backend* b) {
    if (b && b->destroy) b->destroy(b);
}
