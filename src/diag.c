#include "diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int     g_color = 0;

void diag_init(DiagColorMode mode) {
    int want = 0;
    if (mode == DIAG_ALWAYS) want = 1;
    else if (mode == DIAG_NEVER) want = 0;
    else { /* DIAG_AUTO */
        if (getenv("NO_COLOR")) want = 0;
        else if (isatty(fileno(stderr))) want = 1;
        else want = 0;
    }
    g_color = want;
}

const char* diag_red(void)    { return g_color ? "\033[31m" : ""; }
const char* diag_yellow(void) { return g_color ? "\033[33m" : ""; }
const char* diag_blue(void)   { return g_color ? "\033[34m" : ""; }
const char* diag_bold(void)   { return g_color ? "\033[1m"  : ""; }
const char* diag_dim(void)    { return g_color ? "\033[2m"  : ""; }
const char* diag_reset(void)  { return g_color ? "\033[0m"  : ""; }

typedef struct {
    int exp_start;
    int exp_end;
    char file[512];
    int orig_line;
} SourceMapEntry;

static SourceMapEntry* g_sm_entries = NULL;
static int g_sm_count = 0;
static int g_sm_cap = 0;

void sourcemap_clear(void) {
    g_sm_count = 0;
}

void sourcemap_add(int exp_start, int exp_len, const char* file, int orig_line) {
    if (exp_len <= 0) return;
    if (g_sm_count == g_sm_cap) {
        g_sm_cap = g_sm_cap ? g_sm_cap * 2 : 32;
        g_sm_entries = realloc(g_sm_entries, g_sm_cap * sizeof *g_sm_entries);
        if (!g_sm_entries) exit(1);
    }
    SourceMapEntry* e = &g_sm_entries[g_sm_count++];
    e->exp_start = exp_start;
    e->exp_end = exp_start + exp_len;
    snprintf(e->file, sizeof(e->file), "%s", file ? file : "");
    e->orig_line = orig_line;
}

int sourcemap_resolve_src(const char* src, int offset, const char** out_file, int* out_line, int* out_col, int* out_line_start) {
    if (!g_sm_entries || g_sm_count == 0) return 0;
    for (int i = 0; i < g_sm_count; i++) {
        SourceMapEntry* e = &g_sm_entries[i];
        if (offset >= e->exp_start && offset < e->exp_end) {
            int lines_in_chunk = 0;
            int last_ls = e->exp_start;
            for (int k = e->exp_start; k < offset; k++) {
                if (src[k] == '\n') {
                    lines_in_chunk++;
                    last_ls = k + 1;
                }
            }
            if (out_file) *out_file = e->file;
            if (out_line) *out_line = e->orig_line + lines_in_chunk;
            if (out_col) *out_col = offset - last_ls + 1;
            if (out_line_start) *out_line_start = last_ls;
            return 1;
        }
    }
    return 0;
}

void diag_render(const char* src, int offset, int width,
                 const char* kind, const char* msg,
                 char* buf, size_t bufz) {
    if (!buf || bufz == 0) return;
    if (!src || offset < 0) {
        int used = snprintf(buf, bufz, "%s%s:%s %s\n",
                            diag_red(), kind ? kind : "error", diag_reset(),
                            msg ? msg : "");
        (void)used;
        return;
    }

    const char* resolved_file = NULL;
    int line_no = 1;
    int line_start = 0;
    int col = 1;

    if (!sourcemap_resolve_src(src, offset, &resolved_file, &line_no, &col, &line_start)) {
        for (int i = 0; i < offset && src[i]; i++) {
            if (src[i] == '\n') {
                line_no++;
                line_start = i + 1;
            }
        }
        col = offset - line_start + 1;
    }

    int line_end = line_start;
    while (src[line_end] && src[line_end] != '\n') line_end++;

    char linebuf[256];
    size_t ln = (size_t)(line_end - line_start);
    if (ln >= sizeof(linebuf)) ln = sizeof(linebuf) - 1;
    for (int i = 0; (size_t)i < ln; i++) {
        char c = src[line_start + i];
        linebuf[i] = (c == '\t') ? ' ' : c;
    }
    linebuf[ln] = '\0';

    int marker = width;
    if (marker < 1) marker = 1;
    if (marker > (int)ln) marker = (int)ln;

    int used;
    if (resolved_file && resolved_file[0]) {
        used = snprintf(buf, bufz, "%s:%d:%d: %s%s:%s %s\n%s\n",
                        resolved_file, line_no, col,
                        diag_red(), kind ? kind : "error", diag_reset(),
                        msg ? msg : "", linebuf);
    } else {
        used = snprintf(buf, bufz, "%d:%d: %s%s:%s %s\n%s\n",
                        line_no, col,
                        diag_red(), kind ? kind : "error", diag_reset(),
                        msg ? msg : "", linebuf);
    }
    if (used < 0) { buf[0] = '\0'; return; }
    if ((size_t)used >= bufz) return;

    char* care = buf + used;
    size_t care_room = bufz - (size_t)used;
    int pad = col - 1;
    int k = 0;
    int room = (int)care_room - 1; /* keep last NUL */
    int i;
    for (i = 0; i < pad && k < room; i++) { care[k++] = ' '; }
    for (i = 0; i < marker && k < room; i++) { care[k++] = '^'; }
    care[k] = '\0';
    /* ensure a trailing newline after the caret */
    if ((size_t)k < care_room - 1) { care[k++] = '\n'; care[k] = '\0'; }
}

void diag_emit(const char* file, int line, int col, const char* kind, const char* msg) {
    const char* kc = diag_red();
    if (kind && (strcmp(kind, "warning") == 0)) kc = diag_yellow();
    if (file) {
        fprintf(stderr, "%s%s:%d:%d:%s %s%s: %s%s\n",
                diag_dim(), file, line, col, diag_reset(),
                kc, kind ? kind : "error", diag_reset(), msg ? msg : "");
    } else {
        fprintf(stderr, "%s%s:%s %s%s\n",
                kc, kind ? kind : "error", diag_reset(), msg ? msg : "");
    }
}

void diag_error(const char* fmt, ...) {
    char msg[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    diag_emit(NULL, 0, 0, "error", msg);
}

void diag_warning(const char* fmt, ...) {
    char msg[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    diag_emit(NULL, 0, 0, "warning", msg);
}

void diag_note(const char* fmt, ...) {
    char msg[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s%s%s\n", diag_blue(), msg, diag_reset());
}

void diag_stage(const char* stage) {
    fprintf(stderr, "  %s›%s %s\n", diag_blue(), diag_reset(), stage);
}
