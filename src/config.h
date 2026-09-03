#ifndef RK_CONFIG_H
#define RK_CONFIG_H

#include <stddef.h>

/* Rookal's own configuration (distinct from a project's [package]/[build]
   tables). Values are merged global -> local; each field records where it
   came from (0 default, 1 global file, 2 project file). */
typedef struct {
    char* cc;        /* C compiler name/path (default "gcc") */
    char* ar;        /* archiver (default "ar") */
    char* cflags;    /* extra C flags (default "-O2 -Wall") */
    char* standard;  /* emitted C standard (default "c11") */
    char* out_dir;   /* transpile/build output dir (default "build") */
    int   verbose;   /* 0/1 (default 0) */
    char* color;     /* "auto" | "always" | "never" (default "auto") */

    int src_cc, src_ar, src_cflags, src_standard, src_out_dir, src_verbose, src_color;
} RookConfig;

/* Load the merged config (global file, then project rokade.toml [rokade]).
   All owned strings are strdup'd; free with config_free. Returns 0. */
int  config_load(RookConfig* cfg);
void config_free(RookConfig* cfg);

/* Pretty-print the effective config with a per-key origin annotation. */
void config_print(const RookConfig* cfg);

/* Effective string value for a (valid) key; booleans render as "true"/"false". */
const char* config_get_str(const RookConfig* cfg, const char* key);

/* True if `key` is a known config key. */
int  config_is_key(const char* key);

/* Source of `key`'s effective value: 0 = default, 1 = global file,
   2 = project-local file. 0 if the key is unknown. Useful for deciding
   whether a value was explicitly overridden by the user. */
int  config_key_source(const char* key);

/* Set `key`=`value`. is_local => write project rokade.toml, else the global
   config file. Validates key + value; prints a diagnostic and returns
   non-zero on error. */
int  config_set(const char* key, const char* value, int is_local);

/* Space-separated known keys, for help text. */
const char* config_keys_help(void);

#endif
