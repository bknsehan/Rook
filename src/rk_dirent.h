#ifndef RK_DIRENT_H
#define RK_DIRENT_H

#if defined(_WIN32) && !defined(__MINGW32__)
/* Lightweight Win32 emulation of POSIX dirent for MSVC / native Clang-CL */
#include <windows.h>
#include <stdlib.h>
#include <string.h>

struct dirent {
    char d_name[MAX_PATH];
};

typedef struct DIR {
    HANDLE hFind;
    WIN32_FIND_DATAA findData;
    struct dirent entry;
    int first;
} DIR;

static inline DIR* opendir(const char* name) {
    if (!name || !name[0]) return NULL;
    char pattern[MAX_PATH];
    size_t len = strlen(name);
    if (len + 3 >= MAX_PATH) return NULL;
    if (name[len - 1] == '/' || name[len - 1] == '\\') {
        snprintf(pattern, sizeof(pattern), "%s*", name);
    } else {
        snprintf(pattern, sizeof(pattern), "%s/*", name);
    }

    DIR* dir = (DIR*)malloc(sizeof(DIR));
    if (!dir) return NULL;
    dir->hFind = FindFirstFileA(pattern, &dir->findData);
    if (dir->hFind == INVALID_HANDLE_VALUE) {
        free(dir);
        return NULL;
    }
    dir->first = 1;
    return dir;
}

static inline struct dirent* readdir(DIR* dir) {
    if (!dir || dir->hFind == INVALID_HANDLE_VALUE) return NULL;
    if (dir->first) {
        dir->first = 0;
        strncpy(dir->entry.d_name, dir->findData.cFileName, MAX_PATH - 1);
        dir->entry.d_name[MAX_PATH - 1] = '\0';
        return &dir->entry;
    }
    if (!FindNextFileA(dir->hFind, &dir->findData)) {
        return NULL;
    }
    strncpy(dir->entry.d_name, dir->findData.cFileName, MAX_PATH - 1);
    dir->entry.d_name[MAX_PATH - 1] = '\0';
    return &dir->entry;
}

static inline int closedir(DIR* dir) {
    if (!dir) return -1;
    if (dir->hFind != INVALID_HANDLE_VALUE) {
        FindClose(dir->hFind);
    }
    free(dir);
    return 0;
}

#else
#include <dirent.h>
#endif

#endif /* RK_DIRENT_H */
