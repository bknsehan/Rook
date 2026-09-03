#ifndef RK_UTIL_H
#define RK_UTIL_H

typedef struct SB {
    char* data;
    int len;
    int cap;
} SB;

void sb_init(SB* sb);
void sb_free(SB* sb);
void sb_reset(SB* sb);
void sb_append(SB* sb, const char* s);
void sb_appendn(SB* sb, const char* s, int n);
void sb_appendf(SB* sb, const char* fmt, ...);
char* sb_strdup(SB* sb);

char* util_read_file(const char* path, int* out_len);
int util_endswith(const char* s, const char* suffix);

#endif
