//! `rook-lsp` — the Rook language server (JSON-RPC over stdio).
//!
//! Implements the user-facing requirements:
//!   * **Knowledges C** — libc is described by a commandlist shipped with rokade,
//!     embedded here at build time. The server offers libc C completions and
//!     hover signatures with NO libclang dependency. Project C-header awareness
//!     (extra C types/fields/macros) is gated behind the `c-headers` cargo
//!     feature (optional; degrades gracefully when absent).
//!   * **Reports Rook diagnostics per line** — on open/change, runs
//!     `rokade --diagnostics` and publishes the results as LSP ranges.
//!
//! Env (read once at startup):
//!   ROKADE          path to the `rokade` binary (default: `rokade` on $PATH)
//!   ROKADE_DATA_DIR directory containing `commandlist.json` (fallback if rokade
//!                   can't find it itself)
//!
//! rokade emits 1-based line/character in its JSON; LSP ranges are 0-based, so
//! the server subtracts 1 from each.

use std::io::{self, BufRead, Write};
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};

use lsp_types::{
     CompletionItem, CompletionItemKind, CompletionList, CompletionOptions,
     CompletionParams, DeclarationCapability, DidChangeTextDocumentParams,
     DidCloseTextDocumentParams, DidOpenTextDocumentParams, Diagnostic,
     DiagnosticSeverity, Hover, HoverParams, HoverProviderCapability, Location,
     MarkupContent, MarkupKind, OneOf, Position, PublishDiagnosticsParams, Range,
     SaveOptions, TextDocumentSyncCapability, TextDocumentSyncKind,
     TextDocumentSyncOptions, TextDocumentSyncSaveOptions, InitializeResult,
     ServerCapabilities,
};
use serde_json::Value;

const COMMANDLIST_JSON: &str = include_str!("../../src/libc/commandlist.json");
static DOC_SEQ: AtomicU64 = AtomicU64::new(0);

#[derive(Debug, Clone)]
struct CFunc {
    name: String,
    ret: String,
    params: Vec<String>,
    variadic: bool,
}

struct ServerState {
    docs: std::collections::HashMap<String, (String, i32)>,
    cfuncs: Vec<CFunc>,
    rokade: String,
}

fn parse_commandlist(text: &str) -> Vec<CFunc> {
    let v: Value = match serde_json::from_str(text) { Ok(v) => v, Err(_) => return Vec::new() };
    let mut out = Vec::new();
    let arr = match v.as_array() { Some(a) => a, None => return out };
    for f in arr {
        let obj = match f.as_object() { Some(o) => o, None => continue };
        let name = obj.get("name").and_then(|x| x.as_str()).unwrap_or("").to_string();
        let ret = obj.get("ret").and_then(|x| x.as_str()).unwrap_or("void").to_string();
        let mut params = Vec::new();
        let mut variadic = false;
        if let Some(ps) = obj.get("params").and_then(|x| x.as_array()) {
            for p in ps {
                if let Some(t) = p.get("type").and_then(|x| x.as_str()) {
                    if t == "..." { variadic = true; } else { params.push(t.to_string()); }
                }
            }
        }
        if name.is_empty() { continue; }
        out.push(CFunc { name, ret, params, variadic });
    }
    out
}

fn sig(cf: &CFunc) -> String {
    let mut ps = cf.params.join(", ");
    if cf.variadic {
        if !ps.is_empty() { ps.push_str(", "); }
        ps.push_str("...");
    }
    if ps.is_empty() { format!("{} {}()", cf.ret, cf.name) }
    else { format!("{} {}({})", cf.ret, cf.name, ps) }
}

// ─── JSON-RPC 2.0 framing over stdio ───────────────────────────────────

struct Frame {
    method: Option<String>,
    id: Option<Value>,
    params: Option<Value>,
}

fn write_message<W: Write>(w: &mut W, body: &str) -> io::Result<()> {
    let bytes = body.as_bytes();
    write!(w, "Content-Length: {}\r\n\r\n", bytes.len())?;
    w.write_all(bytes)?;
    w.flush()
}

fn read_message<R: BufRead>(r: &mut R) -> Option<Frame> {
    let mut content_length: Option<usize> = None;
    loop {
        let mut line = String::new();
        if r.read_line(&mut line).ok()? == 0 { return None; } // EOF
        let line = line.trim_end_matches(|c| c == '\r' || c == '\n');
        if line.is_empty() { break; }
        if let Some((k, v)) = line.split_once(": ") {
            if k.eq_ignore_ascii_case("content-length") {
                content_length = v.parse().ok();
            }
        }
    }
    let len = content_length?;
    let mut buf = vec![0u8; len];
    if r.read_exact(&mut buf).is_err() { return None; }
    let txt = String::from_utf8_lossy(&buf);
    let v: Value = serde_json::from_str(&txt).ok()?;
    if v.get("jsonrpc").and_then(|x| x.as_str())? != "2.0" {
        return Some(Frame { method: None, id: None, params: None });
    }
    Some(Frame {
        method: v.get("method").and_then(|x| x.as_str()).map(str::to_string),
        id: v.get("id").cloned(),
        params: v.get("params").cloned(),
    })
}

fn send_response<W: Write>(w: &mut W, id: Option<Value>, result: Value) {
    if let Some(id) = id {
        let r = serde_json::json!({ "jsonrpc": "2.0", "id": id, "result": result });
        if let Ok(s) = serde_json::to_string(&r) { let _ = write_message(w, &s); }
    }
}

fn send_notification<W: Write>(w: &mut W, method: &str, params: Value) {
    let n = serde_json::json!({ "jsonrpc": "2.0", "method": method, "params": params });
    if let Ok(s) = serde_json::to_string(&n) { let _ = write_message(w, &s); }
}

// ─── Diagnostics: drive `rokade --diagnostics` ─────────────────────────

#[derive(Debug)]
struct RDiag { line: u32, character: u32, severity: u8, message: String }

fn rook_diagnostics(rokade: &str, commandlist_dir: &str, content: &str) -> Vec<RDiag> {
    let mut tmp = std::env::temp_dir();
    tmp.push(format!("rook_lsp_{}.rook", DOC_SEQ.fetch_add(1, Ordering::Relaxed)));
    let _ = std::fs::write(&tmp, content);
    let out = Command::new(rokade)
        .arg("--diagnostics")
        .arg(&tmp)
        .env("ROKADE_DATA_DIR", commandlist_dir)
        .output();
    let _ = std::fs::remove_file(&tmp);
    let out = match out {
        Ok(o) => o,
        Err(e) => return vec![RDiag {
            line: 1, character: 1, severity: 1,
            message: format!("rook-lsp: cannot run `{rokade}`: {e}"),
        }],
    };
    parse_rokade_json(&String::from_utf8_lossy(&out.stdout))
}

// ─── Goto definition: drive `rokade --def-at` ──────────────────────────

fn rook_def_at(rokade: &str, commandlist_dir: &str, content: &str, doc_uri: &str,
               line: u32, character: u32) -> Option<Location> {
    // commandlist itself, but we keep ROKADE_DATA_DIR set for parity with the
    // diagnostics path and for builds where the commandlist isn't installed.
    let mut tmp = std::env::temp_dir();
    tmp.push(format!("rook_lsp_def_{}.rook", DOC_SEQ.fetch_add(1, Ordering::Relaxed)));
    let _ = std::fs::write(&tmp, content);
    let out = Command::new(rokade)
        .arg("--def-at")
        .arg(&tmp)
        .arg(line.to_string())
        .arg(character.to_string())
        .env("ROKADE_DATA_DIR", commandlist_dir)
        .output();
    let _ = std::fs::remove_file(&tmp);
    let out = out.ok()?;
    if !out.status.success() { return None; }
    let s = String::from_utf8_lossy(&out.stdout);
    let v: Value = serde_json::from_str(&s).ok()?;
    if v.is_null() { return None; }
    let loc: Location = serde_json::from_value(v).ok()?;
    // rokade reported a location in the temp file's coordinates; the ranges are
    // identical to the on-screen buffer, but the URI must be the document's URI.
    let uri: lsp_types::Uri = doc_uri.parse().ok()?;
    Some(Location { uri, range: loc.range })
}

// ─── Document symbols (outline): drive `rokade --symbols` ──────────────────

#[allow(deprecated)]
fn rook_symbols(rokade: &str, commandlist_dir: &str, content: &str, doc_uri: &str)
    -> Vec<lsp_types::SymbolInformation> {
    let mut tmp = std::env::temp_dir();
    tmp.push(format!("rook_lsp_sym_{}.rook", DOC_SEQ.fetch_add(1, Ordering::Relaxed)));
    let _ = std::fs::write(&tmp, content);
    let out = Command::new(rokade)
        .arg("--symbols")
        .arg(&tmp)
        .env("ROKADE_DATA_DIR", commandlist_dir)
        .output();
    let _ = std::fs::remove_file(&tmp);
    let out = match out.ok() {
        Some(o) if o.status.success() => o,
        _ => return Vec::new(),
    };
    let s = String::from_utf8_lossy(&out.stdout);
    let v: Value = match serde_json::from_str(&s) { Ok(v) => v, Err(_) => return Vec::new() };
    let arr = match v.as_array() { Some(a) => a, None => return Vec::new() };
    let uri: lsp_types::Uri = match doc_uri.parse() { Ok(u) => u, Err(_) => return Vec::new() };
    arr.iter().filter_map(|e| {
        let name = e.get("name").and_then(|x| x.as_str())?;
        let kind = e.get("kind").and_then(|k| k.as_str())?;
        let line = e.get("line").and_then(|x| x.as_u64()).unwrap_or(1) as u32;
        let col = e.get("col").and_then(|x| x.as_u64()).unwrap_or(1) as u32;
        let sym_kind = match kind {
            "fn" => lsp_types::SymbolKind::FUNCTION,
            "struct" => lsp_types::SymbolKind::STRUCT,
            "enum" => lsp_types::SymbolKind::ENUM,
            "impl" => lsp_types::SymbolKind::NAMESPACE,
            _ => return None,
        };
        // rokade emits 1-based line/col; LSP ranges are 0-based.
        let pos = lsp_types::Position { line: line.saturating_sub(1), character: col.saturating_sub(1) };
        Some(lsp_types::SymbolInformation {
            name: name.to_string(),
            kind: sym_kind,
            tags: None,
            deprecated: None,
            location: lsp_types::Location { uri: uri.clone(), range: lsp_types::Range { start: pos, end: pos } },
            container_name: None,
        })
    }).collect()
}

fn parse_rokade_json(json: &str) -> Vec<RDiag> {
    let v: Value = match serde_json::from_str(json) { Ok(v) => v, Err(_) => return Vec::new() };
    let mut diags = Vec::new();
    let arr = match v.as_array() { Some(a) => a, None => return diags };
    for d in arr {
        let line = d.get("line").and_then(|x| x.as_u64()).unwrap_or(1) as u32;
        let character = d.get("character").and_then(|x| x.as_u64()).unwrap_or(1) as u32;
        let message = d.get("message").and_then(|x| x.as_str()).unwrap_or("").to_string();
        let sev = match d.get("severity").and_then(|x| x.as_str()).unwrap_or("error") {
            "warning" => 2, "info" => 3, "hint" => 4, _ => 1,
        };
        diags.push(RDiag { line, character, severity: sev, message });
    }
    diags
}

fn publish_diagnostics<W: Write>(w: &mut W, uri: &str, diags: &[RDiag]) {
    let items: Vec<Diagnostic> = diags.iter().map(|d| {
        // rokade is 1-based; LSP is 0-based.
        let start = Position {
            line: (d.line.saturating_sub(1)) as u32,
            character: (d.character.saturating_sub(1)) as u32,
        };
        let end = Position {
            line: start.line,
            character: (d.character.saturating_sub(1)) as u32 + 1,
        };
        Diagnostic {
            range: Range { start, end },
            severity: Some(match d.severity {
                2 => DiagnosticSeverity::WARNING,
                3 => DiagnosticSeverity::INFORMATION,
                4 => DiagnosticSeverity::HINT,
                _ => DiagnosticSeverity::ERROR,
            }),
            code: None,
            code_description: None,
            source: Some("rook".to_string()),
            message: d.message.clone(),
            related_information: None,
            tags: None,
            data: None,
        }
    }).collect();
    let uri_parsed: lsp_types::Uri =
        uri.parse().unwrap_or_else(|_| "file:///rook".parse().unwrap());
    let params = PublishDiagnosticsParams {
        uri: uri_parsed,
        diagnostics: items,
        version: None,
    };
    send_notification(w, "textDocument/publishDiagnostics", serde_json::to_value(params).unwrap());
}

// ─── Completion + hover (libc — no libclang needed) ────────────────────

fn word_at(text: &str, position: Position) -> Option<String> {
    let lines: Vec<&str> = text.lines().collect();
    let l = position.line as usize;
    if l >= lines.len() { return None; }
    let chars: Vec<char> = lines[l].chars().collect();
    let mut idx = (position.character as usize).min(chars.len());
    // Back up to the start of the identifier at/under the cursor.
    while idx > 0 && (chars[idx - 1].is_alphanumeric() || chars[idx - 1] == '_') { idx -= 1; }
    let start = idx;
    let mut end = idx;
    while end < chars.len() && (chars[end].is_alphanumeric() || chars[end] == '_') { end += 1; }
    if start >= end { return None; }
    Some(chars[start..end].iter().collect())
}

fn completion_items(state: &ServerState) -> Vec<CompletionItem> {
    state.cfuncs.iter().map(|cf| CompletionItem {
        label: cf.name.clone(),
        kind: Some(CompletionItemKind::FUNCTION),
        detail: Some(sig(cf)),
        ..Default::default()
    }).collect()
}

fn do_hover(state: &ServerState, params: &HoverParams, content: Option<&str>) -> Option<Hover> {
    let p = &params.text_document_position_params;
    let text = content?;
    let word = word_at(text, p.position)?;
    state.cfuncs.iter().find(|c| c.name == word).map(|cf| {
        let mk = MarkupContent {
            kind: MarkupKind::Markdown,
            value: format!("```c\n{}\n```", sig(cf)),
        };
        Hover { contents: lsp_types::HoverContents::Markup(mk), range: None }
    })
}

// ─── Request dispatch ──────────────────────────────────────────────────

fn handle_request<W: Write>(w: &mut W, state: &mut ServerState, cmdir: &str, frame: &Frame) {
    let params = frame.params.clone().unwrap_or(Value::Null);
    match frame.method.as_deref() {
        Some("initialize") => {
            let result = InitializeResult {
                capabilities: ServerCapabilities {
                    text_document_sync: Some(TextDocumentSyncCapability::Options(TextDocumentSyncOptions {
                        open_close: Some(true),
                        change: Some(TextDocumentSyncKind::FULL),
                        save: Some(TextDocumentSyncSaveOptions::from(SaveOptions {
                            include_text: Some(false),
                        })),
                        ..Default::default()
                    })),
                    hover_provider: Some(HoverProviderCapability::Simple(true)),
                    definition_provider: Some(OneOf::Left(true)),
                    declaration_provider: Some(DeclarationCapability::Simple(true)),
                    document_symbol_provider: Some(OneOf::Left(true)),
                    completion_provider: Some(CompletionOptions {
                        trigger_characters: Some(vec!["".to_string()]),
                        ..Default::default()
                    }),
                    ..Default::default()
                },
                ..Default::default()
            };
            send_response(w, frame.id.clone(), serde_json::to_value(result).unwrap());
        }
        Some("textDocument/didOpen") => {
            if let Ok(p) = serde_json::from_value::<DidOpenTextDocumentParams>(params.clone()) {
                let uri = p.text_document.uri.to_string();
                let content = p.text_document.text;
                let ver = p.text_document.version;
                state.docs.insert(uri.clone(), (content.clone(), ver));
                let diags = rook_diagnostics(&state.rokade, cmdir, &content);
                publish_diagnostics(w, &uri, &diags);
            }
        }
        Some("textDocument/didChange") => {
            if let Ok(p) = serde_json::from_value::<DidChangeTextDocumentParams>(params) {                let uri = p.text_document.uri.to_string();
                let content = p.content_changes.into_iter()
                    .next().map(|c| c.text).unwrap_or_default();
                let ver = p.text_document.version;
                state.docs.insert(uri.clone(), (content.clone(), ver));
                let diags = rook_diagnostics(&state.rokade, cmdir, &content);
                publish_diagnostics(w, &uri, &diags);
            }
        }
        Some("textDocument/didClose") => {
            if let Ok(p) = serde_json::from_value::<DidCloseTextDocumentParams>(params) {
                state.docs.remove(&p.text_document.uri.to_string());
                let params = PublishDiagnosticsParams {
                    uri: p.text_document.uri,
                    diagnostics: Vec::new(),
                    version: None,
                };
                send_notification(w, "textDocument/publishDiagnostics", serde_json::to_value(params).unwrap());
            }
        }
        Some("textDocument/completion") => {
            if serde_json::from_value::<CompletionParams>(params).is_ok() {
                let items = completion_items(state);
                let list = CompletionList { is_incomplete: false, items };
                send_response(w, frame.id.clone(), serde_json::to_value(list).unwrap());
            }
        }
        Some("textDocument/hover") => {
            if let Ok(params) = serde_json::from_value::<HoverParams>(params) {
                let uri = params.text_document_position_params.text_document.uri.to_string();
                let content = state.docs.get(&uri).map(|(c, _)| c.as_str());
                let result = do_hover(state, &params, content).map(|h| serde_json::to_value(h).unwrap());
                send_response(w, frame.id.clone(), result.unwrap_or(Value::Null));
            }
        }
        Some("textDocument/declaration" | "textDocument/definition") => {
            let uri = params.get("textDocument").and_then(|t| t.get("uri"))
                .and_then(|u| u.as_str()).unwrap_or("").to_string();
            let pos = params.get("position").and_then(|p| p.as_object());
            let line = pos.and_then(|p| p.get("line")).and_then(|x| x.as_u64()).unwrap_or(0) as u32;
            let character = pos.and_then(|p| p.get("character"))
                .and_then(|x| x.as_u64())
                .unwrap_or(0) as u32;
            let content = state.docs.get(&uri).map(|(c, _)| c.as_str()).unwrap_or("");
            let result = rook_def_at(&state.rokade, cmdir, content, &uri, line, character);
            send_response(w, frame.id.clone(),
                result.map(|l| serde_json::to_value(l).unwrap()).unwrap_or(Value::Null));
        }
        Some("textDocument/documentSymbol") => {
            let uri = params.get("textDocument").and_then(|t| t.get("uri"))
                .and_then(|u| u.as_str()).unwrap_or("").to_string();
            let content = state.docs.get(&uri).map(|(c, _)| c.as_str()).unwrap_or("");
            let symbols = rook_symbols(&state.rokade, cmdir, content, &uri);
            send_response(w, frame.id.clone(), serde_json::to_value(symbols).unwrap());
        }
        Some("shutdown") => send_response(w, frame.id.clone(), Value::Null),
        Some("exit") => std::process::exit(0),
        _ => { send_response(w, frame.id.clone(), Value::Null); }
    }
}

fn ensure_commandlist_dir() -> String {
    // Materialize the embedded commandlist into a temp dir so that the `rokade`
    // subprocess always sees the same libc signatures we serve, regardless of
    // ROKADE_DATA_DIR / install state. Cheap and idempotent.
    let mut dir = std::env::temp_dir();
    dir.push("rook_lsp_data");
    let _ = std::fs::create_dir_all(&dir);
    let _ = std::fs::write(dir.join("commandlist.json"), COMMANDLIST_JSON);
    dir.to_string_lossy().into_owned()
}

fn main() {
    let stdin = io::stdin();
    let mut stdin = io::BufReader::new(stdin.lock());
    let stdout = io::stdout();
    let mut stdout = io::BufWriter::new(stdout.lock());

    let mut state = ServerState {
        docs: std::collections::HashMap::new(),
        cfuncs: parse_commandlist(COMMANDLIST_JSON),
        rokade: std::env::var("ROKADE").unwrap_or_else(|_| "rokade".to_string()),
    };
    let cmdir = ensure_commandlist_dir();

    while let Some(frame) = read_message(&mut stdin) {
        if frame.method.is_some() || frame.id.is_some() {
            handle_request(&mut stdout, &mut state, &cmdir, &frame);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_commandlist_and_signatures() {
        let fns = parse_commandlist(COMMANDLIST_JSON);
        assert!(fns.len() > 50, "expected a full libc commandlist, got {}", fns.len());
        let strlen = fns.iter().find(|c| c.name == "strlen").expect("strlen present");
        assert_eq!(strlen.ret, "size_t");
        assert_eq!(strlen.params, vec!["const char*".to_string()]);
        assert!(!strlen.variadic);
        let printf = fns.iter().find(|c| c.name == "printf").expect("printf present");
        assert_eq!(printf.ret, "int");
        assert!(printf.variadic);
        assert_eq!(sig(strlen), "size_t strlen(const char*)");
        assert_eq!(sig(printf), "int printf(const char*, ...)");
    }

    #[test]
    fn parses_rokade_diagnostics_json() {
        let json = r#"[{"file":"a.rook","line":3,"character":10,"severity":"error","message":"boom"}]"#;
        let d = parse_rokade_json(json);
        assert_eq!(d.len(), 1);
        assert_eq!(d[0].line, 3);
        assert_eq!(d[0].character, 10);
        assert_eq!(d[0].severity, 1);
        assert_eq!(d[0].message, "boom");
    }

    #[test]
    fn handles_empty_diagnostic_json() {
        assert!(parse_rokade_json("[]").is_empty());
        assert!(parse_rokade_json("not json").is_empty());
    }

    #[test]
    fn word_at_extracts_identifier() {
        let t = "int main(){ strlen(x); }\n";
        // cursor at 's' of strlen (index 12)
        assert_eq!(word_at(t, Position { line: 0, character: 12 }), Some("strlen".to_string()));
        // cursor inside the '(', should back up to 'strlen'
        assert_eq!(word_at(t, Position { line: 0, character: 18 }), Some("strlen".to_string()));
    }
}
