#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <sys/wait.h>
#endif

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

void argvec_init(ArgVec* v) {
    v->args = NULL;
    v->count = 0;
    v->cap = 0;
}

void argvec_add(ArgVec* v, const char* arg) {
    if (!arg) return;
    if (v->count + 1 >= v->cap) {
        size_t ncap = v->cap == 0 ? 16 : v->cap * 2;
        char** tmp = realloc(v->args, ncap * sizeof(char*));
        if (!tmp) exit(1);
        v->args = tmp;
        v->cap = ncap;
    }
    v->args[v->count++] = strdup(arg);
    v->args[v->count] = NULL;
}

void argvec_split_and_add(ArgVec* v, const char* str) {
    if (!str) return;
    const char* p = str;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        char buf[8192];
        size_t b = 0;
        char quote = 0;
        while (*p && b + 1 < sizeof(buf)) {
            if (quote) {
                if (*p == quote) {
                    quote = 0;
                    p++;
                } else if (*p == '\\' && *(p + 1)) {
                    p++;
                    buf[b++] = *p++;
                } else {
                    buf[b++] = *p++;
                }
            } else {
                if (*p == '\'' || *p == '"') {
                    quote = *p++;
                } else if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
                    break;
                } else if (*p == '\\' && *(p + 1)) {
                    p++;
                    buf[b++] = *p++;
                } else {
                    buf[b++] = *p++;
                }
            }
        }
        buf[b] = '\0';
        argvec_add(v, buf);
    }
}

void argvec_free(ArgVec* v) {
    if (!v) return;
    for (size_t i = 0; i < v->count; i++) free(v->args[i]);
    free(v->args);
    v->args = NULL;
    v->count = 0;
    v->cap = 0;
}

int util_exec(const char* const* argv) {
    if (!argv || !argv[0]) return -1;
#ifdef _WIN32
    intptr_t ret = _spawnvp(_P_WAIT, argv[0], (const char* const*)argv);
    return (int)ret;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], (char* const*)argv);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
#endif
}

int util_exec_capture(const char* const* argv, char* out_buf, size_t out_cap) {
    if (!argv || !argv[0] || !out_buf || out_cap == 0) return -1;
    out_buf[0] = '\0';
#ifdef _WIN32
    // Windows pipe capture using _pipe and _spawnvp or CreateProcess
    // Fallback: spawn and read
    return -1;
#else
    int pfd[2];
    if (pipe(pfd) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        dup2(pfd[1], STDERR_FILENO);
        close(pfd[1]);
        execvp(argv[0], (char* const*)argv);
        _exit(127);
    }
    close(pfd[1]);
    size_t total = 0;
    ssize_t n;
    while ((n = read(pfd[0], out_buf + total, out_cap - 1 - total)) > 0) {
        total += (size_t)n;
        if (total >= out_cap - 1) break;
    }
    out_buf[total] = '\0';
    close(pfd[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
#endif
}

int util_rm_rf(const char* path) {
    if (!path || !*path) return -1;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) return unlink(path);

    DIR* d = opendir(path);
    if (!d) return -1;
    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        char sub[4096];
        snprintf(sub, sizeof(sub), "%s/%s", path, de->d_name);
        util_rm_rf(sub);
    }
    closedir(d);
    return rmdir(path);
}
