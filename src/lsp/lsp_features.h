#ifndef RK_LSP_FEATURES_H
#define RK_LSP_FEATURES_H

#include "lsp_doc.h"
#include "../util.h"

void lsp_handle_completion(LspDoc* doc, int line, int character, SB* res);
void lsp_handle_hover(LspDoc* doc, int line, int character, SB* res);
void lsp_handle_definition(LspDoc* doc, int line, int character, SB* res);
void lsp_handle_signature_help(LspDoc* doc, int line, int character, SB* res);
void lsp_handle_document_symbols(LspDoc* doc, SB* res);
void lsp_handle_formatting(LspDoc* doc, SB* res);
void lsp_handle_semantic_tokens(LspDoc* doc, SB* res);

#endif
