#ifndef RK_LSP_DOC_H
#define RK_LSP_DOC_H

#include <stddef.h>
#include "../ast.h"
#include "../lexer.h"
#include "../sema.h"

typedef struct LspDiagnostic {
    int line;        /* 0-based */
    int character;   /* 0-based */
    int end_line;    /* 0-based */
    int end_character; /* 0-based exclusive */
    int severity;    /* 1: Error, 2: Warning, 3: Info, 4: Hint */
    char code[16];   /* e.g. "E0001", "W2001" (may be "") */
    int tags;        /* bit 0 (1) = Unnecessary, bit 1 (2) = Deprecated */
    char* message;
} LspDiagnostic;

typedef struct LspDoc {
    char* uri;
    char* path;
    char* text;
    int len;
    int version;

    Token* toks;
    int ntoks;
    Program* prog;
    int local_items_count; /* Number of AST items belonging strictly to this document (excluding imports) */
    Sema* sema;

    LspDiagnostic* diags;
    int ndiags;

    char** extra_buffers;
    int nextra_buffers;
    int capextra_buffers;

    struct LspDoc* next;
} LspDoc;

void lsp_doc_init(void);
LspDoc* lsp_doc_open(const char* uri, const char* text, int version);
LspDoc* lsp_doc_update(const char* uri, const char* text, int version);
void lsp_doc_close(const char* uri);
LspDoc* lsp_doc_get(const char* uri);
void lsp_doc_free_all(void);

/* Analysis / Type-check update for a document */
void lsp_doc_analyze(LspDoc* doc);

/* Position and URI conversion helpers */
int lsp_uri_to_path(const char* uri, char* out_path, size_t cap);
void lsp_path_to_uri(const char* path, char* out_uri, size_t cap);

int lsp_pos_to_offset(const char* text, int len, int line, int character);
void lsp_offset_to_pos(const char* text, int len, int offset, int* out_line, int* out_char);
const char* lsp_get_line_prefix(const char* text, int len, int line, int character, int* out_prefix_len);

/* Workspace root discovery and management */
void lsp_set_workspace_root(const char* root);
const char* lsp_get_workspace_root(void);
const char* lsp_find_project_root(const char* doc_path);
int lsp_parse_rokade_toml_inc_dirs(const char* toml_path, char** inc_dirs, int max_inc_dirs, int* n_inc_dirs);

#endif
