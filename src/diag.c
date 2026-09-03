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

/* ── Marker-based source location resolution ──────────────────────────────
 * During #comprise expansion, resolve_includes_rec injects comment markers:
 *     // @rk:src /path/to/file.rook:LINE\n
 * immediately before each verbatim source line.  diag_render scans backward
 * from the error offset to find the nearest such marker, then counts the
 * remaining newlines between the marker line and the error position to get
 * the exact file + line inside the user's original source file.
 * ──────────────────────────────────────────────────────────────────────── */

#define RKSRC_MARKER  "// @rk:src "
#define RKSRC_MLEN    (sizeof(RKSRC_MARKER) - 1)

/* Resolve `offset` in the expanded buffer back to the original file/line.
 * Returns 1 on success (out_* set), 0 if no marker found (caller falls back
 * to raw line counting). */
static int resolve_src_marker(const char* src, int offset,
                               const char** out_file, int* out_line,
                               int* out_col,  int* out_line_start) {
    if (!src || offset < 0) return 0;

    /* Walk backward line by line from offset to find the most recent marker */
    int scan = offset;
    while (scan > 0 && src[scan - 1] != '\n') scan--;

    while (scan >= 0) {
        if (strncmp(src + scan, RKSRC_MARKER, RKSRC_MLEN) == 0) {
            const char* rest = src + scan + RKSRC_MLEN;
            const char* nl = strchr(rest, '\n');
            if (!nl) nl = rest + strlen(rest);
            /* Find last colon before newline — separates path from line number */
            const char* last_colon = NULL;
            for (const char* q = rest; q < nl; q++) {
                if (*q == ':') last_colon = q;
            }
            if (!last_colon || last_colon == rest) goto next_line;

            int orig_line = atoi(last_colon + 1);
            if (orig_line <= 0) goto next_line;

            static char fbuf[1024];
            size_t flen = (size_t)(last_colon - rest);
            if (flen >= sizeof(fbuf)) flen = sizeof(fbuf) - 1;
            memcpy(fbuf, rest, flen);
            fbuf[flen] = '\0';

            /* Count lines from end-of-marker-line to offset */
            int marker_end = (int)(nl - src) + 1;
            int extra_lines = 0;
            int last_ls = marker_end;
            for (int k = marker_end; k < offset; k++) {
                if (src[k] == '\n') { extra_lines++; last_ls = k + 1; }
            }

            if (out_file)       *out_file = fbuf;
            if (out_line)       *out_line = orig_line + extra_lines;
            if (out_col)        *out_col  = (offset >= last_ls) ? (offset - last_ls + 1) : 1;
            if (out_line_start) *out_line_start = last_ls;
            return 1;
        }

next_line:
        if (scan == 0) break;
        scan--;
        while (scan > 0 && src[scan - 1] != '\n') scan--;
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

    if (!resolve_src_marker(src, offset, &resolved_file, &line_no, &col, &line_start)) {
        /* No marker — raw line count */
        for (int i = 0; i < offset && src[i]; i++) {
            if (src[i] == '\n') { line_no++; line_start = i + 1; }
        }
        col = offset - line_start + 1;
    }

    /* Find the source line to display. If line_start is a marker line itself,
     * advance to the next line (the actual code line). */
    if (strncmp(src + line_start, RKSRC_MARKER, RKSRC_MLEN) == 0) {
        while (src[line_start] && src[line_start] != '\n') line_start++;
        if (src[line_start] == '\n') line_start++;
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
    if (marker > (int)ln && ln > 0) marker = (int)ln;

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
