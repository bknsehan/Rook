#include "lsp_server.h"
#include "lsp_json.h"
#include "lsp_doc.h"
#include "lsp_workspace.h"
#include "lsp_features.h"
#include "../sema.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

static void send_rpc_payload(const char* payload, int len) {
    if (len < 0) len = (int)strlen(payload);
    fprintf(stdout, "Content-Length: %d\r\n\r\n", len);
    fwrite(payload, 1, (size_t)len, stdout);
    fflush(stdout);
}

static void send_rpc_response(JsonVal* id, const char* result_json) {
    SB sb;
    sb_init(&sb);
    sb_append(&sb, "{\"jsonrpc\":\"2.0\",");
    if (id) {
        if (id->type == JSON_NUMBER) {
            sb_appendf(&sb, "\"id\":%d,", (int)id->u.num_val);
        } else if (id->type == JSON_STRING) {
            sb_append(&sb, "\"id\":");
            json_emit_escaped_str(&sb, id->u.str_val);
            sb_append(&sb, ",");
        } else {
            sb_append(&sb, "\"id\":null,");
        }
    } else {
        sb_append(&sb, "\"id\":null,");
    }
    sb_append(&sb, "\"result\":");
    sb_append(&sb, result_json ? result_json : "null");
    sb_append(&sb, "}");

    char* payload = sb_strdup(&sb);
    send_rpc_payload(payload, sb.len);
    free(payload);
    sb_free(&sb);
}

static void publish_diagnostics(LspDoc* doc) {
    if (!doc) return;
    SB sb;
    sb_init(&sb);
    sb_append(&sb, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":");
    json_emit_escaped_str(&sb, doc->uri);
    sb_appendf(&sb, ",\"version\":%d,\"diagnostics\":[", doc->version);

    for (int i = 0; i < doc->ndiags; i++) {
        if (i > 0) sb_append(&sb, ",");
        LspDiagnostic* d = &doc->diags[i];
        sb_appendf(&sb, "{\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}},\"severity\":%d,\"source\":\"rook\",",
                   d->line, d->character,
                   d->end_line, d->end_character,
                   d->severity);
        if (d->code[0]) {
            sb_append(&sb, "\"code\":");
            json_emit_escaped_str(&sb, d->code);
            sb_append(&sb, ",");
        }
        if (d->tags) {
            sb_append(&sb, "\"tags\":[");
            int first = 1;
            if (d->tags & 1) { sb_append(&sb, "1"); first = 0; }
            if (d->tags & 2) { if (!first) sb_append(&sb, ","); sb_append(&sb, "2"); }
            sb_append(&sb, "],");
        }
        sb_append(&sb, "\"message\":");
        json_emit_escaped_str(&sb, d->message);
        sb_append(&sb, "}");
    }

    sb_append(&sb, "]}}");
    char* payload = sb_strdup(&sb);
    send_rpc_payload(payload, sb.len);
    free(payload);
    sb_free(&sb);
}

int lsp_server_run(void) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    lsp_doc_init();

    char header[256];
    while (fgets(header, sizeof(header), stdin)) {
        if (strncmp(header, "Content-Length:", 15) != 0) {
            continue;
        }
        int content_len = atoi(header + 15);
        if (content_len <= 0) continue;

        /* Skip remaining headers until \r\n */
        while (fgets(header, sizeof(header), stdin)) {
            if (strcmp(header, "\r\n") == 0 || strcmp(header, "\n") == 0) break;
        }

        /* Read content_len bytes */
        char* body = malloc(content_len + 1);
        if (!body) break;
        size_t total_read = 0;
        while ((int)total_read < content_len) {
            size_t n = fread(body + total_read, 1, (size_t)(content_len - (int)total_read), stdin);
            if (n <= 0) break;
            total_read += n;
        }
        body[total_read] = '\0';

        JsonVal* root = json_parse(body, (int)total_read);
        free(body);
        if (!root) continue;

        JsonVal* id = json_get(root, "id");
        const char* method = json_get_str(root, "method");
        JsonVal* params = json_get_obj(root, "params");

        if (!method) {
            json_free(root);
            continue;
        }

        if (strcmp(method, "initialize") == 0) {
            const char* root_uri = json_get_str(params, "rootUri");
            const char* root_path = json_get_str(params, "rootPath");
            if (!root_uri && !root_path) {
                JsonVal* wf = json_get_arr(params, "workspaceFolders");
                if (wf && wf->u.arr.count > 0) {
                    root_uri = json_get_str(wf->u.arr.items[0], "uri");
                }
            }
            char path_buf[4096];
            if (root_uri && lsp_uri_to_path(root_uri, path_buf, sizeof(path_buf))) {
                lsp_set_workspace_root(path_buf);
                lsp_workspace_init(path_buf);
            } else if (root_path && root_path[0]) {
                lsp_set_workspace_root(root_path);
                lsp_workspace_init(root_path);
            } else {
                lsp_workspace_init(".");
            }
            sema_set_raw_hook(lsp_workspace_populate_raw_names);

            const char* init_result =
                "{"
                  "\"capabilities\":{"
                    "\"textDocumentSync\":1,"
                    "\"completionProvider\":{\"triggerCharacters\":[\".\",\">\",\"/\",\"\\\"\",\"<\",\"#\",\":\"]},"
                    "\"hoverProvider\":true,"
                    "\"definitionProvider\":true,"
                    "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]},"
                    "\"documentSymbolProvider\":true,"
                    "\"workspaceSymbolProvider\":true,"
                    "\"documentFormattingProvider\":true,"
                    "\"semanticTokensProvider\":{"
                      "\"legend\":{"
                        "\"tokenTypes\":[\"type\",\"function\",\"variable\",\"parameter\",\"keyword\",\"comment\",\"string\",\"number\",\"operator\",\"enumMember\"],"
                        "\"tokenModifiers\":[\"declaration\",\"definition\",\"readonly\",\"defaultLibrary\"]"
                      "},"
                      "\"full\":true"
                    "}"
                  "},"
                  "\"serverInfo\":{\"name\":\"rook-lsp\",\"version\":\"0.7.0\"}"
                "}";
            send_rpc_response(id, init_result);
        } else if (strcmp(method, "initialized") == 0) {
            const char* ws_root = lsp_get_workspace_root();
            if (ws_root) {
                lsp_workspace_init(ws_root);
            }
        } else if (strcmp(method, "shutdown") == 0) {
            send_rpc_response(id, "null");
        } else if (strcmp(method, "exit") == 0) {
            json_free(root);
            lsp_doc_free_all();
            lsp_workspace_free_all();
            return 0;
        } else if (strcmp(method, "textDocument/didOpen") == 0) {
            JsonVal* td = json_get_obj(params, "textDocument");
            const char* uri = json_get_str(td, "uri");
            const char* text = json_get_str(td, "text");
            int version = json_get_int(td, "version", 1);
            if (uri && text) {
                LspDoc* doc = lsp_doc_open(uri, text, version);
                if (doc) {
                    lsp_workspace_index_file(doc->path, doc->text, doc->len);
                    publish_diagnostics(doc);
                }
            }
        } else if (strcmp(method, "textDocument/didChange") == 0) {
            JsonVal* td = json_get_obj(params, "textDocument");
            const char* uri = json_get_str(td, "uri");
            int version = json_get_int(td, "version", 1);
            JsonVal* changes = json_get_arr(params, "contentChanges");
            if (uri && changes && changes->u.arr.count > 0) {
                /* Full document sync: take latest text */
                JsonVal* first = changes->u.arr.items[changes->u.arr.count - 1];
                const char* new_text = json_get_str(first, "text");
                if (new_text) {
                    LspDoc* doc = lsp_doc_update(uri, new_text, version);
                    if (doc) {
                        lsp_workspace_index_file(doc->path, doc->text, doc->len);
                        publish_diagnostics(doc);
                    }
                }
            }
        } else if (strcmp(method, "textDocument/didClose") == 0) {
            JsonVal* td = json_get_obj(params, "textDocument");
            const char* uri = json_get_str(td, "uri");
            if (uri) lsp_doc_close(uri);
        } else if (strcmp(method, "textDocument/didSave") == 0) {
            /* Zed may be configured for save-only diagnostics; re-publish. */
            JsonVal* td = json_get_obj(params, "textDocument");
            const char* uri = json_get_str(td, "uri");
            LspDoc* doc = uri ? lsp_doc_get(uri) : NULL;
            if (doc) {
                lsp_workspace_index_file(doc->path, doc->text, doc->len);
                publish_diagnostics(doc);
            }
        } else if (strcmp(method, "workspace/symbol") == 0) {
            const char* query = json_get_str(params, "query");
            SB res;
            sb_init(&res);
            lsp_workspace_search_symbols(query ? query : "", &res);
            char* payload = sb_strdup(&res);
            send_rpc_response(id, payload);
            free(payload);
            sb_free(&res);
        } else if (strcmp(method, "textDocument/completion") == 0) {
            JsonVal* td = json_get_obj(params, "textDocument");
            const char* uri = json_get_str(td, "uri");
            JsonVal* pos = json_get_obj(params, "position");
            int line = json_get_int(pos, "line", 0);
            int ch = json_get_int(pos, "character", 0);
            LspDoc* doc = uri ? lsp_doc_get(uri) : NULL;
            SB res;
            sb_init(&res);
            if (doc) {
                lsp_handle_completion(doc, line, ch, &res);
            } else {
                sb_append(&res, "{\"isIncomplete\":false,\"items\":[]}");
            }
            char* payload = sb_strdup(&res);
            send_rpc_response(id, payload);
            free(payload);
            sb_free(&res);
        } else if (strcmp(method, "textDocument/hover") == 0) {
            JsonVal* td = json_get_obj(params, "textDocument");
            const char* uri = json_get_str(td, "uri");
            JsonVal* pos = json_get_obj(params, "position");
            int line = json_get_int(pos, "line", 0);
            int ch = json_get_int(pos, "character", 0);
            LspDoc* doc = uri ? lsp_doc_get(uri) : NULL;
            SB res;
            sb_init(&res);
            if (doc) {
                lsp_handle_hover(doc, line, ch, &res);
            } else {
                sb_append(&res, "null");
            }
            char* payload = sb_strdup(&res);
            send_rpc_response(id, payload);
            free(payload);
            sb_free(&res);
        } else if (strcmp(method, "textDocument/definition") == 0) {
            JsonVal* td = json_get_obj(params, "textDocument");
            const char* uri = json_get_str(td, "uri");
            JsonVal* pos = json_get_obj(params, "position");
            int line = json_get_int(pos, "line", 0);
            int ch = json_get_int(pos, "character", 0);
            LspDoc* doc = uri ? lsp_doc_get(uri) : NULL;
            SB res;
            sb_init(&res);
            if (doc) {
                lsp_handle_definition(doc, line, ch, &res);
            } else {
                sb_append(&res, "null");
            }
            char* payload = sb_strdup(&res);
            send_rpc_response(id, payload);
            free(payload);
            sb_free(&res);
        } else if (strcmp(method, "textDocument/signatureHelp") == 0) {
            JsonVal* td = json_get_obj(params, "textDocument");
            const char* uri = json_get_str(td, "uri");
            JsonVal* pos = json_get_obj(params, "position");
            int line = json_get_int(pos, "line", 0);
            int ch = json_get_int(pos, "character", 0);
            LspDoc* doc = uri ? lsp_doc_get(uri) : NULL;
            SB res;
            sb_init(&res);
            if (doc) {
                lsp_handle_signature_help(doc, line, ch, &res);
            } else {
                sb_append(&res, "null");
            }
            char* payload = sb_strdup(&res);
            send_rpc_response(id, payload);
            free(payload);
            sb_free(&res);
        } else if (strcmp(method, "textDocument/documentSymbol") == 0) {
            JsonVal* td = json_get_obj(params, "textDocument");
            const char* uri = json_get_str(td, "uri");
            LspDoc* doc = uri ? lsp_doc_get(uri) : NULL;
            SB res;
            sb_init(&res);
            if (doc) {
                lsp_handle_document_symbols(doc, &res);
            } else {
                sb_append(&res, "[]");
            }
            char* payload = sb_strdup(&res);
            send_rpc_response(id, payload);
            free(payload);
            sb_free(&res);
        } else if (strcmp(method, "textDocument/formatting") == 0) {
            JsonVal* td = json_get_obj(params, "textDocument");
            const char* uri = json_get_str(td, "uri");
            LspDoc* doc = uri ? lsp_doc_get(uri) : NULL;
            SB res;
            sb_init(&res);
            if (doc) {
                lsp_handle_formatting(doc, &res);
            } else {
                sb_append(&res, "[]");
            }
            char* payload = sb_strdup(&res);
            send_rpc_response(id, payload);
            free(payload);
            sb_free(&res);
        } else if (strcmp(method, "textDocument/semanticTokens/full") == 0) {
            JsonVal* td = json_get_obj(params, "textDocument");
            const char* uri = json_get_str(td, "uri");
            LspDoc* doc = uri ? lsp_doc_get(uri) : NULL;
            SB res;
            sb_init(&res);
            if (doc) {
                lsp_handle_semantic_tokens(doc, &res);
            } else {
                sb_append(&res, "{\"data\":[]}");
            }
            char* payload = sb_strdup(&res);
            send_rpc_response(id, payload);
            free(payload);
            sb_free(&res);
        } else {
            /* Unknown method with an ID: respond with null result */
            if (id) {
                send_rpc_response(id, "null");
            }
        }

        json_free(root);
    }

    lsp_doc_free_all();
    return 0;
}
