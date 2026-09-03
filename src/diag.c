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

    int line_no = 1;
    int line_start = 0;
    int i;
    for (i = 0; i < offset && src[i]; i++) {
        if (src[i] == '\n') {
            line_no++;
            line_start = i + 1;
        }
    }
    int line_end = line_start;
    while (src[line_end] && src[line_end] != '\n') line_end++;

    int col = offset - line_start + 1;

    char linebuf[256];
    size_t ln = (size_t)(line_end - line_start);
    if (ln >= sizeof(linebuf)) ln = sizeof(linebuf) - 1;
    for (i = 0; (size_t)i < ln; i++) {
        char c = src[line_start + i];
        linebuf[i] = (c == '\t') ? ' ' : c;
    }
    linebuf[ln] = '\0';

    int marker = width;
    if (marker < 1) marker = 1;
    if (marker > (int)ln) marker = (int)ln;

    int used = snprintf(buf, bufz, "%d:%d: %s%s:%s %s\n%s\n",
                        line_no, col,
                        diag_red(), kind ? kind : "error", diag_reset(),
                        msg ? msg : "", linebuf);
    if (used < 0) { buf[0] = '\0'; return; }
    if ((size_t)used >= bufz) return;

    char* care = buf + used;
    size_t care_room = bufz - (size_t)used;
    int pad = col - 1;
    int k = 0;
    int room = (int)care_room - 1; /* keep last NUL */
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
