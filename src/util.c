#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sb_init(SB* sb) {
    sb->cap = 256;
    sb->len = 0;
    sb->data = malloc(sb->cap);
    if (!sb->data) exit(1);
    sb->data[0] = '\0';
}

void sb_free(SB* sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

void sb_reset(SB* sb) {
    sb->len = 0;
    if (sb->data) sb->data[0] = '\0';
}

static void sb_grow(SB* sb, int need) {
    if (sb->len + need + 1 <= sb->cap) return;
    while (sb->len + need + 1 > sb->cap) sb->cap *= 2;
    sb->data = realloc(sb->data, sb->cap);
    if (!sb->data) exit(1);
}

void sb_appendn(SB* sb, const char* s, int n) {
    if (n <= 0) return;
    sb_grow(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_append(SB* sb, const char* s) {
    sb_appendn(sb, s, (int)strlen(s));
}

void sb_appendf(SB* sb, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n < (int)sizeof buf) {
        sb_appendn(sb, buf, n);
        return;
    }
    char* big = malloc(n + 1);
    if (!big) exit(1);
    va_start(ap, fmt);
    vsnprintf(big, n + 1, fmt, ap);
    va_end(ap);
    sb_appendn(sb, big, n);
    free(big);
}

char* sb_strdup(SB* sb) {
    char* p = malloc(sb->len + 1);
    if (!p) exit(1);
    memcpy(p, sb->data, sb->len + 1);
    return p;
}

char* util_read_file(const char* path, int* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len) *out_len = (int)got;
    return buf;
}

int util_endswith(const char* s, const char* suffix) {
    size_t ls = strlen(s), lf = strlen(suffix);
    return ls >= lf && memcmp(s + ls - lf, suffix, lf) == 0;
}
