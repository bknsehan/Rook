#ifndef RK_LSP_WORKSPACE_H
#define RK_LSP_WORKSPACE_H

#include <stddef.h>
#include "../util.h"
#include "lsp_doc.h"

typedef struct WorkspaceSymbol {
    char name[128];
    int kind;            /* 3=Function, 22=Struct, 13=Enum, 6=Variable, 20=EnumMember */
    char type_or_ret[128];
    char signature[256];
    char doc[512];
    char file_path[4096];
    char uri[4096];
    int line;            /* 0-based */
    int character;       /* 0-based */
    char mod_name[128];
    int is_std;
} WorkspaceSymbol;

/* Initialize workspace index for root directory (and stdlib) */
void lsp_workspace_init(const char* root_dir);

/* Index or re-index a single file */
void lsp_workspace_index_file(const char* file_path, const char* content, int content_len);

/* Remove a file from the index */
void lsp_workspace_remove_file(const char* file_path);

/* Find definition for symbol name across the workspace.
   Returns 1 if found, with out_uri, out_line, out_col populated. */
int lsp_workspace_find_definition(const char* name, char* out_uri, size_t uri_cap, int* out_line, int* out_col);

/* Lookup a symbol by exact name */
const WorkspaceSymbol* lsp_workspace_lookup(const char* name);

/* Append completions from workspace to completion list */
void lsp_workspace_complete(const char* prefix, SB* res, int* count, const char* current_file);

/* Search workspace symbols for workspace/symbol request */
void lsp_workspace_search_symbols(const char* query, SB* res);

/* Get all indexed module names (for #comprise autocompletion) */
void lsp_workspace_complete_modules(const char* prefix, SB* res, int* count);

/* Check if workspace has been initialized with symbols */
int lsp_workspace_is_initialized(void);

/* Populate raw names for semantic analysis */
void lsp_workspace_populate_raw_names(void);

/* Free all workspace index resources */
void lsp_workspace_free_all(void);

#endif
