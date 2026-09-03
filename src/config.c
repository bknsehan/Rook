#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef enum { K_STR, K_BOOL } KeyType;

static const struct {
    const char* name;
    KeyType     type;
    const char* def;
} KMAP[] = {
    { "cc",       K_STR,  "gcc" },
    { "ar",       K_STR,  "ar" },
    { "cflags",   K_STR,  "-O2 -Wall" },
    { "standard", K_STR,  "c11" },
    { "out_dir",  K_STR,  "build" },
    { "verbose",  K_BOOL, "false" },
    { "color",    K_STR,  "auto" },
};
#define NKEYS (int)(sizeof(KMAP) / sizeof(KMAP[0]))

static const char* STD_OK[] = {
    "c89", "c99", "c11", "c17", "c23",
    "gnu89", "gnu99", "gnu11", "gnu17", "gnu23",
    NULL
};

static int key_index(const char* name) {
    for (int i = 0; i < NKEYS; i++)
        if (strcmp(KMAP[i].name, name) == 0) return i;
    return -1;
}

const char* config_keys_help(void) {
    return "cc ar cflags standard out_dir verbose color";
}

int config_is_key(const char* key) {
    return key_index(key) >= 0;
}

/* Trim whitespace and strip one layer of surrounding double quotes. */
static void trim_copy(const char* s, char* out, size_t outsz) {
    while (*s == ' ' || *s == '\t') s++;
    size_t L = strlen(s);
    while (L > 0 && (s[L - 1] == ' ' || s[L - 1] == '\t' ||
                     s[L - 1] == '\r' || s[L - 1] == '\n')) L--;
    if (L >= 2 && s[0] == '"' && s[L - 1] == '"') { s++; L -= 2; }
    if (L >= outsz) L = outsz - 1;
    memcpy(out, s, L);
    out[L] = '\0';
}

/* Validate + canonicalize a raw value for key `ki` into `out`.
   Returns 1 if valid, 0 otherwise. */
static int normalize_value(int ki, const char* raw, char* out, size_t outsz) {
    trim_copy(raw, out, outsz);
    if (KMAP[ki].type == K_BOOL) {
        char low[64];
        size_t m = strlen(out);
        if (m >= sizeof low) m = sizeof low - 1;
        for (size_t i = 0; i < m; i++) low[i] = (char)tolower((unsigned char)out[i]);
        low[m] = '\0';
        if (strcmp(low, "true") == 0 || strcmp(low, "1") == 0 ||
            strcmp(low, "yes") == 0 || strcmp(low, "on") == 0) {
            strcpy(out, "true"); return 1;
        }
        if (strcmp(low, "false") == 0 || strcmp(low, "0") == 0 ||
            strcmp(low, "no") == 0 || strcmp(low, "off") == 0) {
            strcpy(out, "false"); return 1;
        }
        return 0;
    }
    if (strcmp(KMAP[ki].name, "standard") == 0) {
        for (int i = 0; STD_OK[i]; i++)
            if (strcmp(out, STD_OK[i]) == 0) return 1;
        return 0;
    }
    return out[0] != '\0'; /* string values must be non-empty */
}

/* Read `key = value` pairs from the [rokade] section of `path` into vals
   (caller-zeroed char vals[NKEYS][256]). Other sections are ignored. */
static void parse_section(const char* path, char vals[NKEYS][256]) {
    FILE* f = fopen(path, "r");
    if (!f) return;
    char buf[8192];
    int in_sec = 0;
    while (fgets(buf, sizeof buf, f)) {
        if (buf[0] == '[') {
            char sec[256];
            if (sscanf(buf, "[%255[^]]]", sec) == 1)
                in_sec = (strcmp(sec, "rokade") == 0);
            continue;
        }
        if (!in_sec) continue;
        char* eq = strchr(buf, '=');
        if (!eq) continue;
        char k[256];
        size_t kl = (size_t)(eq - buf);
        while (kl > 0 && (buf[kl - 1] == ' ' || buf[kl - 1] == '\t')) kl--;
        if (kl >= sizeof k) kl = sizeof k - 1;
        memcpy(k, buf, kl);
        k[kl] = '\0';
        int idx = key_index(k);
        if (idx < 0) continue;
        char v[256];
        size_t vl = strlen(eq + 1);
        if (vl >= sizeof v) vl = sizeof v - 1;
        memcpy(v, eq + 1, vl);
        v[vl] = '\0';
        trim_copy(v, vals[idx], sizeof vals[idx]);
    }
    fclose(f);
}

static char* config_global_path(void) {
    const char* xdg = getenv("XDG_CONFIG_HOME");
    char homecfg[4096];
    const char* base;
    if (xdg) {
        base = xdg;
    } else {
        const char* home = getenv("HOME");
        if (!home) home = ".";
        snprintf(homecfg, sizeof homecfg, "%s/.config", home);
        base = homecfg;
    }
    size_t need = strlen(base) + strlen("/rokade/config.toml") + 1;
    char* p = malloc(need);
    if (p) snprintf(p, need, "%s/rokade/config.toml", base);
    return p;
}

static char* config_local_path(void) {
    return strdup("rokade.toml");
}

int config_key_source(const char* key) {
    int i = key_index(key);
    if (i < 0) return 0;
    char gvals[NKEYS][256];
    char lvals[NKEYS][256];
    memset(gvals, 0, sizeof gvals);
    memset(lvals, 0, sizeof lvals);
    char* gp = config_global_path();
    parse_section(gp, gvals);
    free(gp);
    char* lp = config_local_path();
    if (access(lp, F_OK) == 0) parse_section(lp, lvals);
    free(lp);
    if (lvals[i][0]) return 2;
    if (gvals[i][0]) return 1;
    return 0;
}

static void ensure_parent_dir(const char* path) {
    char tmp[4096];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof tmp) return;
    memcpy(tmp, path, n);
    tmp[n] = '\0';
    char* slash = strrchr(tmp, '/');
    if (!slash) return;
    *slash = '\0';
    mkdir(tmp, 0755); /* ignore failure (dir may already exist) */
}

static void set_field(RookConfig* cfg, int i, const char* v, int src) {
    switch (i) {
        case 0: cfg->cc = strdup(v);        cfg->src_cc = src; break;
        case 1: cfg->ar = strdup(v);        cfg->src_ar = src; break;
        case 2: cfg->cflags = strdup(v);    cfg->src_cflags = src; break;
        case 3: cfg->standard = strdup(v);  cfg->src_standard = src; break;
        case 4: cfg->out_dir = strdup(v);   cfg->src_out_dir = src; break;
        case 5: cfg->verbose = (strcmp(v, "true") == 0); cfg->src_verbose = src; break;
        case 6: cfg->color = strdup(v);     cfg->src_color = src; break;
    }
}

int config_load(RookConfig* cfg) {
    memset(cfg, 0, sizeof *cfg);
    char gvals[NKEYS][256];
    char lvals[NKEYS][256];
    memset(gvals, 0, sizeof gvals);
    memset(lvals, 0, sizeof lvals);

    char* gp = config_global_path();
    parse_section(gp, gvals);
    free(gp);

    char* lp = config_local_path();
    if (access(lp, F_OK) == 0) parse_section(lp, lvals);
    free(lp);

    for (int i = 0; i < NKEYS; i++) {
        const char* v = lvals[i][0] ? lvals[i]
                      : (gvals[i][0] ? gvals[i] : KMAP[i].def);
        int src = lvals[i][0] ? 2 : (gvals[i][0] ? 1 : 0);
        set_field(cfg, i, v, src);
    }
    return 0;
}

void config_free(RookConfig* cfg) {
    free(cfg->cc);
    free(cfg->ar);
    free(cfg->cflags);
    free(cfg->standard);
    free(cfg->out_dir);
    free(cfg->color);
}

const char* config_get_str(const RookConfig* cfg, const char* key) {
    int i = key_index(key);
    if (i < 0) return NULL;
    switch (i) {
        case 0: return cfg->cc;
        case 1: return cfg->ar;
        case 2: return cfg->cflags;
        case 3: return cfg->standard;
        case 4: return cfg->out_dir;
        case 5: return cfg->verbose ? "true" : "false";
        case 6: return cfg->color;
    }
    return NULL;
}

static int src_for(const RookConfig* cfg, int i) {
    switch (i) {
        case 0: return cfg->src_cc;
        case 1: return cfg->src_ar;
        case 2: return cfg->src_cflags;
        case 3: return cfg->src_standard;
        case 4: return cfg->src_out_dir;
        case 5: return cfg->src_verbose;
        case 6: return cfg->src_color;
    }
    return 0;
}

void config_print(const RookConfig* cfg) {
    printf("rokade configuration (effective):\n");
    for (int i = 0; i < NKEYS; i++) {
        const char* v = config_get_str(cfg, KMAP[i].name);
        int src = src_for(cfg, i);
        const char* where = src == 2 ? "local"
                          : src == 1 ? "global"
                                     : "default";
        printf("  %-10s = %-12s (%s)\n", KMAP[i].name, v ? v : "", where);
    }
    char* gp = config_global_path();
    char* lp = config_local_path();
    printf("  global file: %s%s\n", gp, access(gp, F_OK) == 0 ? " (exists)" : " (not created)");
    printf("  local file : %s%s\n", lp, access(lp, F_OK) == 0 ? " (exists)" : " (not found)");
    free(gp);
    free(lp);
}

/* Write `key`=`value` (already canonicalized) into the [rokade] section of
   `path`, preserving all other content. Creates the file/section if needed. */
static int write_key(const char* path, int ki, const char* value) {
    int is_str = (KMAP[ki].type == K_STR);
    char valbuf[512];
    snprintf(valbuf, sizeof valbuf, is_str ? "\"%s\"" : "%s", value);

    char* lines[4096];
    int n = 0;
    int in_sec = 0, sec_found = 0, sec_idx = -1, replaced = 0;

    FILE* f = fopen(path, "r");
    if (f) {
        char buf[8192];
        while (fgets(buf, sizeof buf, f)) {
            size_t L = strlen(buf);
            while (L > 0 && (buf[L - 1] == '\n' || buf[L - 1] == '\r')) buf[--L] = '\0';
            if (buf[0] == '[') {
                char sec[256];
                if (sscanf(buf, "[%255[^]]]", sec) == 1) {
                    in_sec = (strcmp(sec, "rokade") == 0);
                    if (in_sec) sec_found = 1;
                }
                lines[n++] = strdup(buf);
                if (in_sec && sec_idx < 0) sec_idx = n - 1;
                continue;
            }
            char* emit = buf;
            if (in_sec) {
                char* eq = strchr(buf, '=');
                if (eq) {
                    size_t kl = (size_t)(eq - buf);
                    while (kl > 0 && (buf[kl - 1] == ' ' || buf[kl - 1] == '\t')) kl--;
                    if (kl > 0) {
                        char k[256];
                        size_t c = kl < sizeof k - 1 ? kl : sizeof k - 1;
                        memcpy(k, buf, c);
                        k[c] = '\0';
                        if (strcmp(k, KMAP[ki].name) == 0) {
                            char nl[2048];
                            snprintf(nl, sizeof nl, "%s = %s", KMAP[ki].name, valbuf);
                            emit = nl;
                            replaced = 1;
                        }
                    }
                }
            }
            lines[n++] = strdup(emit);
        }
        fclose(f);
    }

    char* out[4096];
    int on = 0;
    if (!sec_found) {
        for (int i = 0; i < n; i++) out[on++] = strdup(lines[i]);
        if (n > 0) out[on++] = strdup("");
        out[on++] = strdup("[rokade]");
        char nl[2048];
        snprintf(nl, sizeof nl, "%s = %s", KMAP[ki].name, valbuf);
        out[on++] = strdup(nl);
    } else if (!replaced) {
        for (int i = 0; i < n; i++) {
            out[on++] = strdup(lines[i]);
            if (i == sec_idx) {
                char nl[2048];
                snprintf(nl, sizeof nl, "%s = %s", KMAP[ki].name, valbuf);
                out[on++] = strdup(nl);
            }
        }
    } else {
        for (int i = 0; i < n; i++) out[on++] = strdup(lines[i]);
    }

    ensure_parent_dir(path);
    FILE* w = fopen(path, "w");
    if (!w) {
        fprintf(stderr, "rokade: cannot write %s\n", path);
        for (int i = 0; i < n; i++) free(lines[i]);
        for (int i = 0; i < on; i++) free(out[i]);
        return 1;
    }
    for (int i = 0; i < on; i++) {
        fprintf(w, "%s\n", out[i]);
        free(out[i]);
    }
    fclose(w);
    for (int i = 0; i < n; i++) free(lines[i]);

    printf("rokade: set %s = %s\n", KMAP[ki].name, value);
    return 0;
}

int config_set(const char* key, const char* value, int is_local) {
    int i = key_index(key);
    if (i < 0) {
        fprintf(stderr, "rokade: unknown config key '%s'\n", key);
        fprintf(stderr, "rokade: valid keys: %s\n", config_keys_help());
        return 1;
    }
    char norm[256];
    if (!normalize_value(i, value, norm, sizeof norm)) {
        fprintf(stderr, "rokade: invalid value for '%s': '%s'\n", key, value);
        return 1;
    }
    char* path = is_local ? config_local_path() : config_global_path();
    int rc = write_key(path, i, norm);
    free(path);
    return rc;
}
