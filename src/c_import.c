#include "c_import.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ROKADE_HAS_LIBCLANG
#include <clang-c/Index.h>

static char g_imported_headers[512][256];
static size_t g_n_imported = 0;

void c_import_init(void) {
    g_n_imported = 0;
}

static int is_header_imported(const char* header) {
    for (size_t i = 0; i < g_n_imported; i++) {
        if (strcmp(g_imported_headers[i], header) == 0) return 1;
    }
    return 0;
}

static void mark_header_imported(const char* header) {
    if (g_n_imported < 512) {
        snprintf(g_imported_headers[g_n_imported++], 256, "%s", header);
    }
}

static void clean_c_type_str(char* buf) {
    /* Remove redundant spaces, e.g. "char *" -> "char*" */
    char tmp[256];
    size_t j = 0;
    size_t len = strlen(buf);
    for (size_t i = 0; i < len && j + 1 < sizeof(tmp); i++) {
        if (buf[i] == ' ' && i + 1 < len && buf[i + 1] == '*') continue;
        tmp[j++] = buf[i];
    }
    tmp[j] = '\0';
    snprintf(buf, 256, "%s", tmp);
}

static AstType* parse_c_type_to_ast(const char* raw) {
    if (!raw || !raw[0]) return sema_mk_type("", "void", 0);
    const char* p = raw;
    while (*p == ' ') p++;
    const char* qual = "";
    if (strncmp(p, "const ", 6) == 0) {
        qual = "const ";
        p += 6;
    }
    while (*p == ' ') p++;
    if (strncmp(p, "struct ", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "enum ", 5) == 0) {
        p += 5;
    }
    while (*p == ' ') p++;

    /* Count trailing pointers */
    int ptrs = 0;
    char name[128];
    size_t ni = 0;
    while (*p && *p != '*' && *p != ' ' && ni + 1 < sizeof(name)) {
        name[ni++] = *p++;
    }
    name[ni] = '\0';

    while (*p) {
        if (*p == '*') ptrs++;
        p++;
    }
    return sema_mk_type(qual, name[0] ? name : "void", ptrs);
}

typedef struct {
    StructField* fields;
    int nfields;
    int cap;
} StructFieldCollector;

static enum CXChildVisitResult field_collector_cb(CXCursor cursor, CXCursor parent, CXClientData client_data) {
    (void)parent;
    StructFieldCollector* sfc = (StructFieldCollector*)client_data;
    if (clang_getCursorKind(cursor) == CXCursor_FieldDecl) {
        CXString fname = clang_getCursorSpelling(cursor);
        const char* fn_str = clang_getCString(fname);
        if (fn_str && fn_str[0]) {
            if (sfc->nfields >= sfc->cap) {
                sfc->cap = sfc->cap ? sfc->cap * 2 : 8;
                sfc->fields = realloc(sfc->fields, sfc->cap * sizeof(StructField));
            }
            CXType ftype = clang_getCursorType(cursor);
            CXString ftname = clang_getTypeSpelling(ftype);
            const char* ft_str = clang_getCString(ftname);

            sfc->fields[sfc->nfields].name = strdup(fn_str);
            sfc->fields[sfc->nfields].type = parse_c_type_to_ast(ft_str);
            sfc->fields[sfc->nfields].dim = NULL;
            sfc->fields[sfc->nfields].style = 0;
            sfc->nfields++;

            clang_disposeString(ftname);
        }
        clang_disposeString(fname);
    }
    return CXChildVisit_Continue;
}

typedef struct {
    Sema* sema;
} ImportContext;

static enum CXChildVisitResult tu_visitor(CXCursor cursor, CXCursor parent, CXClientData client_data) {
    (void)parent;
    ImportContext* ctx = (ImportContext*)client_data;
    enum CXCursorKind kind = clang_getCursorKind(cursor);

    if (kind == CXCursor_FunctionDecl) {
        CXString cname = clang_getCursorSpelling(cursor);
        const char* name = clang_getCString(cname);
        /* Skip internal compiler builtins starting with "__" */
        if (name && name[0] && (name[0] != '_' || name[1] != '_')) {
            CXType fn_type = clang_getCursorType(cursor);
            CXType ret_type = clang_getResultType(fn_type);
            CXString ret_str = clang_getTypeSpelling(ret_type);
            const char* ret_cstr = clang_getCString(ret_str);

            char ret_buf[128];
            snprintf(ret_buf, sizeof(ret_buf), "%s", ret_cstr ? ret_cstr : "void");
            clean_c_type_str(ret_buf);

            int num_args = clang_Cursor_getNumArguments(cursor);
            char params_buf[512] = "";
            for (int i = 0; i < num_args; i++) {
                CXCursor arg_cur = clang_Cursor_getArgument(cursor, i);
                CXType arg_type = clang_getCursorType(arg_cur);
                CXString atname = clang_getTypeSpelling(arg_type);
                const char* at_str = clang_getCString(atname);

                char single_param[128];
                snprintf(single_param, sizeof(single_param), "%s", at_str ? at_str : "int");
                clean_c_type_str(single_param);

                if (i > 0) strncat(params_buf, "\x1f", sizeof(params_buf) - strlen(params_buf) - 1);
                strncat(params_buf, single_param, sizeof(params_buf) - strlen(params_buf) - 1);

                clang_disposeString(atname);
            }

            int is_variadic = clang_isFunctionTypeVariadic(fn_type);
            sema_register_cfunc(name, ret_buf, params_buf, num_args, is_variadic);
            clang_disposeString(ret_str);
        }
        clang_disposeString(cname);
    } else if (kind == CXCursor_StructDecl) {
        if (clang_isCursorDefinition(cursor)) {
            CXString sname = clang_getCursorSpelling(cursor);
            const char* name = clang_getCString(sname);
            if (name && name[0]) {
                const char* actual_name = name;
                if (strncmp(actual_name, "struct ", 7) == 0) actual_name += 7;

                StructFieldCollector sfc = {0};
                clang_visitChildren(cursor, field_collector_cb, &sfc);
                if (sfc.nfields > 0) {
                    sema_register_cstruct(ctx->sema, actual_name, sfc.fields, sfc.nfields);
                }
            }
            clang_disposeString(sname);
        }
    } else if (kind == CXCursor_TypedefDecl) {
        CXString tname = clang_getCursorSpelling(cursor);
        const char* name = clang_getCString(tname);
        if (name && name[0]) {
            CXType utype = clang_getTypedefDeclUnderlyingType(cursor);
            CXString utstr = clang_getTypeSpelling(utype);
            const char* ut_cstr = clang_getCString(utstr);

            /* Check if typedef is an alias for a struct definition */
            if (utype.kind == CXType_Record || utype.kind == CXType_Elaborated) {
                StructFieldCollector sfc = {0};
                clang_visitChildren(cursor, field_collector_cb, &sfc);
                if (sfc.nfields > 0) {
                    sema_register_cstruct(ctx->sema, name, sfc.fields, sfc.nfields);
                }
            }

            AstType* ast_t = parse_c_type_to_ast(ut_cstr);
            sema_register_ctypedef(ctx->sema, name, ast_t);

            clang_disposeString(utstr);
        }
        clang_disposeString(tname);
    } else if (kind == CXCursor_EnumConstantDecl) {
        CXString ename = clang_getCursorSpelling(cursor);
        const char* name = clang_getCString(ename);
        if (name && name[0]) {
            sema_register_cvar(ctx->sema, name, sema_mk_type("", "int", 0));
        }
        clang_disposeString(ename);
    } else if (kind == CXCursor_VarDecl) {
        CXString vname = clang_getCursorSpelling(cursor);
        const char* name = clang_getCString(vname);
        if (name && name[0] && (name[0] != '_' || name[1] != '_')) {
            CXType vtype = clang_getCursorType(cursor);
            CXString vtstr = clang_getTypeSpelling(vtype);
            const char* vt_cstr = clang_getCString(vtstr);
            AstType* ast_t = parse_c_type_to_ast(vt_cstr);
            sema_register_cvar(ctx->sema, name, ast_t);
            clang_disposeString(vtstr);
        }
        clang_disposeString(vname);
    }

    return CXChildVisit_Continue;
}

int c_import_code(Sema* sema, const char* code, const char** inc_dirs, size_t n_inc) {
    if (!sema || !code || !code[0]) return 0;

    CXIndex index = clang_createIndex(0, 0);
    struct CXUnsavedFile unsaved = {
        .Filename = "import_input.c",
        .Contents = code,
        .Length = strlen(code)
    };

    size_t args_cap = n_inc + 16;
    const char** args = (const char**)malloc(args_cap * sizeof(const char*));
    char** inc_bufs = n_inc > 0 ? (char**)malloc(n_inc * sizeof(char*)) : NULL;
    int n_args = 0;
    if (args) {
        args[n_args++] = "-std=c23";
        args[n_args++] = "-w";
        args[n_args++] = "-D_GNU_SOURCE";
        args[n_args++] = "-D_DEFAULT_SOURCE";
        args[n_args++] = "-D_POSIX_C_SOURCE=200809L";

        if (inc_bufs) {
            for (size_t i = 0; i < n_inc; i++) {
                size_t blen = strlen(inc_dirs[i]) + 4;
                inc_bufs[i] = (char*)malloc(blen);
                if (inc_bufs[i]) {
                    snprintf(inc_bufs[i], blen, "-I%s", inc_dirs[i]);
                    args[n_args++] = inc_bufs[i];
                }
            }
        }
    }

    CXTranslationUnit tu = clang_parseTranslationUnit(
        index, "import_input.c", args, n_args, &unsaved, 1,
        CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_DetailedPreprocessingRecord
    );

    if (inc_bufs) {
        for (size_t i = 0; i < n_inc; i++) {
            if (inc_bufs[i]) free(inc_bufs[i]);
        }
        free(inc_bufs);
    }
    if (args) free(args);

    if (!tu) {
        clang_disposeIndex(index);
        return 0;
    }

    ImportContext ctx = { .sema = sema };
    CXCursor root = clang_getTranslationUnitCursor(tu);
    clang_visitChildren(root, tu_visitor, &ctx);

    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
    return 1;
}

int c_import_header(Sema* sema, const char* header, int is_system, const char** inc_dirs, size_t n_inc) {
    if (!sema || !header || !header[0]) return 0;
    if (is_header_imported(header)) return 1;
    mark_header_imported(header);

    char code[1024];
    if (is_system) {
        snprintf(code, sizeof(code), "#include <%s>\n", header);
    } else {
        snprintf(code, sizeof(code), "#include \"%s\"\n", header);
    }
    return c_import_code(sema, code, inc_dirs, n_inc);
}

int c_import_program_raw(Sema* sema, Program* prog, const char** inc_dirs, size_t n_inc) {
    if (!sema || !prog) return 0;
    size_t total_len = 0;
    for (int i = 0; i < prog->nitems; i++) {
        if (prog->items[i]->kind == TOP_RAW && prog->items[i]->raw_len > 0) {
            total_len += (size_t)prog->items[i]->raw_len + 2;
        }
    }
    if (total_len == 0) return 1;
    char* buf = malloc(total_len + 1);
    if (!buf) return 0;
    char* cur = buf;
    for (int i = 0; i < prog->nitems; i++) {
        if (prog->items[i]->kind == TOP_RAW && prog->items[i]->raw_len > 0) {
            memcpy(cur, prog->items[i]->raw, (size_t)prog->items[i]->raw_len);
            cur += prog->items[i]->raw_len;
            *cur++ = '\n';
        }
    }
    *cur = '\0';
    int ret = c_import_code(sema, buf, inc_dirs, n_inc);
    free(buf);
    return ret;
}

#else

void c_import_init(void) {}
int c_import_code(Sema* sema, const char* code, const char** inc_dirs, size_t n_inc) {
    (void)sema; (void)code; (void)inc_dirs; (void)n_inc;
    return 0;
}
int c_import_header(Sema* sema, const char* header, int is_system, const char** inc_dirs, size_t n_inc) {
    (void)sema; (void)header; (void)is_system; (void)inc_dirs; (void)n_inc;
    return 0;
}
int c_import_program_raw(Sema* sema, Program* prog, const char** inc_dirs, size_t n_inc) {
    (void)sema; (void)prog; (void)inc_dirs; (void)n_inc;
    return 0;
}

#endif

int c_import_scan_and_load(Sema* sema, const char* src, int len, const char* basedir, const char** inc_dirs, size_t n_inc) {
    if (!sema || !src || len <= 0) return 0;
    const char* p = src;
    const char* end = src + len;

    while (p < end) {
        if (*p == '#') {
            const char* nl = memchr(p + 1, '\n', end - p - 1);
            int line_len = nl ? (int)(nl - p + 1) : (int)(end - p);
            char line[1024];
            int copy_len = line_len < (int)sizeof(line) ? line_len : (int)sizeof(line) - 1;
            memcpy(line, p, copy_len);
            line[copy_len] = '\0';

            /* Check for include or comprise */
            if (strstr(line, "include") || strstr(line, "comprise")) {
                const char* c1 = strchr(line, '<');
                int is_sys = 1;
                if (!c1) {
                    c1 = strchr(line, '"');
                    is_sys = 0;
                }
                if (c1) {
                    const char* cend = is_sys ? strchr(c1 + 1, '>') : strchr(c1 + 1, '"');
                    if (cend) {
                        size_t hlen = (size_t)(cend - c1 - 1);
                        char hname[256];
                        if (hlen < sizeof(hname)) {
                            memcpy(hname, c1 + 1, hlen);
                            hname[hlen] = '\0';
                            /* Only import C headers (not .rook files) */
                            if (hlen < 5 || strcmp(hname + hlen - 5, ".rook") != 0) {
                                size_t total_cap = n_inc + 4;
                                const char** all_inc = (const char**)malloc(total_cap * sizeof(const char*));
                                size_t total_inc = 0;
                                if (all_inc) {
                                    if (basedir && basedir[0]) all_inc[total_inc++] = basedir;
                                    for (size_t i = 0; i < n_inc; i++) {
                                        all_inc[total_inc++] = inc_dirs[i];
                                    }
                                    c_import_header(sema, hname, is_sys, all_inc, total_inc);
                                    free(all_inc);
                                }
                            }
                        }
                    }
                }
            }
            p = nl ? nl + 1 : end;
        } else {
            const char* nl = memchr(p, '\n', end - p);
            p = nl ? nl + 1 : end;
        }
    }
    return 1;
}
