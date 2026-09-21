#ifndef RK_LINT_H
#define RK_LINT_H

#include "ast.h"
#include "sema.h"

/* AST-only warning passes for the analyzer/LSP.
   Appends severity 2..4 diagnostics to sema->diags (never fails the build).
   `n_local_items`: only check the first N top-level items (the current
   document; excludes comprised imports). Pass <= 0 to check everything.
   `src`: expanded source for marker remapping (CLI); pass NULL for raw
   document text (LSP), where AST coordinates are already correct. */
void lint_run(Program* prog, int n_local_items, Sema* sema);
void lint_run_with_src(Program* prog, int n_local_items, Sema* sema,
                       const char* src);

#endif
