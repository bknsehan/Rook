#ifndef RK_DIAG_H
#define RK_DIAG_H

#include <stddef.h>
#include <stdio.h>

typedef enum { DIAG_AUTO, DIAG_ALWAYS, DIAG_NEVER } DiagColorMode;

/* Configure color output. DIAG_AUTO enables color only when stderr is a TTY
   and the NO_COLOR environment variable is unset. Safe to call once at start. */
void diag_init(DiagColorMode mode);

/* Render a formatted diagnostic (with source caret) into a caller-provided
   buffer. Produces a `line:col:`-prefixed message; the caller is expected to
   prepend the file path to form `file:line:col:`:
       error: expected ':'
         fn f(x = { }
              ^
   src:      full source the token points into (may be include-expanded).
   offset:   byte offset of the offending token (src[offset] is its first char).
   width:    how many columns the marker should span (>= 1).
   buf/bufz: destination buffer. */
void diag_render(const char* src, int offset, int width,
                 const char* kind, const char* msg,
                 char* buf, size_t bufz);

/* Live, colorized printing (no caret). These honor the configured color mode
   and emit to stderr. */
void diag_emit(const char* file, int line, int col, const char* kind, const char* msg);
void diag_error(const char* fmt, ...);
void diag_warning(const char* fmt, ...);
void diag_note(const char* fmt, ...);

/* Color accessors: return the ANSI sequence when color is enabled, "" otherwise. */
const char* diag_red(void);
const char* diag_yellow(void);
const char* diag_blue(void);
const char* diag_bold(void);
const char* diag_dim(void);
const char* diag_reset(void);

/* Resolve a byte offset in (possibly include-expanded) `src` back to the
   original file/line/col via `// @rk:src` markers. Returns 1 when a marker
   was found (out_file/out_line/out_col set), 0 otherwise (caller falls back
   to raw line counting). out_file may be NULL. */
int diag_resolve(const char* src, int offset,
                 char* out_file, size_t file_cap,
                 int* out_line, int* out_col);

/* Line-based variant for AST line/col (e.g. lint) in expanded coordinates.
   Converts (exp_line, exp_col) to an offset, then resolves via markers.
   Returns 1 on marker hit. */
int diag_resolve_linecol(const char* src,
                         int exp_line, int exp_col,
                         char* out_file, size_t file_cap,
                         int* out_line, int* out_col);

/* Print a concise, colorized stage-progress marker to stderr (no trailing
   newline). Used by build/run pipelines. */
void diag_stage(const char* stage);

#endif
