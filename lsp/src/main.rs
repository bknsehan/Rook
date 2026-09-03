//! `rook-lsp` — the Rook language server (JSON-RPC over stdio).
//!
//! Implements:
//!   * **Rook Syntax Autocompletion & Snippets** — keywords, types, object/impl/match snippets.
//!   * **Comprise Collaboration** — automatically indexes functions, objects, and methods
//!     from comprised Rook files (including standard library `<std/...>`).
//!   * **C Header Collaboration** — scans `#include` C headers (like `<raylib.h>`, `<stdio.h>`)
//!     and extracts functions, structs, typedefs, and `#define` constants with hover signatures.
//!   * **Per-line Diagnostics** — runs `rokade --diagnostics` and publishes LSP ranges.
//!   * **Goto Definition & Symbols** — supports buffer and cross-module definition jumping.

use std::collections::HashMap;
use std::fs;
use std::io::{self, BufRead, Write};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::SystemTime;

use lsp_types::{
    CompletionItem, CompletionItemKind, CompletionList, CompletionOptions,
    CompletionParams, DeclarationCapability, DidChangeTextDocumentParams,
    DidCloseTextDocumentParams, DidOpenTextDocumentParams, Diagnostic,
    DiagnosticSeverity, DocumentLink, DocumentLinkOptions, DocumentLinkParams,
    Hover, HoverParams, HoverProviderCapability,
    InsertTextFormat, Location, MarkupContent, MarkupKind, OneOf, Position,
    PublishDiagnosticsParams, Range, SaveOptions, TextDocumentSyncCapability,
    TextDocumentSyncKind, TextDocumentSyncOptions, TextDocumentSyncSaveOptions,
    InitializeResult, ServerCapabilities, Uri,
};
use serde_json::Value;

const COMMANDLIST_JSON: &str = include_str!("../../src/libc/commandlist.json");
static DOC_SEQ: AtomicU64 = AtomicU64::new(0);

// ─── Symbol Models ─────────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub enum CSymbolKind {
    Function { sig: String },
    Struct,
    Typedef { target: String },
    Constant { value: Option<String> },
}

#[derive(Debug, Clone)]
pub struct CSymbol {
    pub name: String,
    pub kind: CSymbolKind,
    pub header: String,
    pub file_path: Option<PathBuf>,
    pub line: u32,
}

#[derive(Debug, Clone)]
pub enum RookSymbolKind {
    Function { sig: String },
    Object { fields: Vec<(String, String)> },
    Method { obj: String, sig: String },
    Enum { variants: Vec<String> },
    Variable { type_name: Option<String> },
}

#[derive(Debug, Clone)]
pub struct RookSymbol {
    pub name: String,
    pub kind: RookSymbolKind,
    pub file_path: PathBuf,
    pub line: u32,
    pub col: u32,
}

#[derive(Debug, Clone)]
pub struct CFunc {
    pub name: String,
    pub ret: String,
    pub params: Vec<String>,
    pub variadic: bool,
}

pub struct ServerState {
    pub docs: HashMap<String, (String, i32)>,
    pub cfuncs: Vec<CFunc>,
    pub rokade: String,
    pub c_header_cache: HashMap<PathBuf, (SystemTime, Vec<CSymbol>)>,
    pub rook_module_cache: HashMap<PathBuf, (SystemTime, Vec<RookSymbol>)>,
    pub std_dir: Option<PathBuf>,
}

// ─── Path & Standard Library Resolution ────────────────────────────────

fn find_std_dir() -> Option<PathBuf> {
    if let Ok(p) = std::env::var("ROOK_HOME") {
        let pb = PathBuf::from(p).join("std");
        if pb.is_dir() { return Some(pb); }
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(bin_dir) = exe.parent() {
            if let Some(root) = bin_dir.parent() {
                let std_p = root.join("std");
                if std_p.is_dir() { return Some(std_p); }
            }
        }
    }
    let default_p = PathBuf::from("/home/bknsehan/bin/Rook/std");
    if default_p.is_dir() { return Some(default_p); }
    None
}

fn uri_to_path(uri_str: &str) -> Option<PathBuf> {
    if let Some(rest) = uri_str.strip_prefix("file://") {
        #[cfg(windows)]
        let rest = rest.trim_start_matches('/');
        return Some(PathBuf::from(rest));
    }
    None
}

// ─── Directives & Scanners ─────────────────────────────────────────────

pub fn extract_directives(text: &str) -> (Vec<String>, Vec<String>) {
    let mut c_headers = Vec::new();
    let mut rook_modules = Vec::new();

    for raw_line in text.lines() {
        let line = raw_line.trim();
        if line.starts_with("#include") {
            let rest = line["#include".len()..].trim();
            if let Some(start) = rest.find(['<', '"']) {
                let quote = rest.chars().nth(start).unwrap();
                let close = if quote == '<' { '>' } else { '"' };
                if let Some(end) = rest[start + 1..].find(close) {
                    let h = rest[start + 1..start + 1 + end].trim().to_string();
                    if !h.is_empty() { c_headers.push(h); }
                }
            }
        } else if line.starts_with("#comprise") || line.starts_with("comprise") {
            let kw_len = if line.starts_with('#') { "#comprise".len() } else { "comprise".len() };
            let rest = line[kw_len..].trim().trim_end_matches(';').trim();
            if let Some(start) = rest.find(['<', '"']) {
                let quote = rest.chars().nth(start).unwrap();
                let close = if quote == '<' { '>' } else { '"' };
                if let Some(end) = rest[start + 1..].find(close) {
                    let mut m = rest[start + 1..start + 1 + end].trim().to_string();
                    if m.contains('.') && !m.ends_with(".rook") {
                        m = m.replace('.', "/");
                    }
                    if !m.is_empty() { rook_modules.push(m); }
                }
            } else {
                let bare = rest.split_whitespace().next().unwrap_or("");
                let mut m = bare.to_string();
                if m.contains('.') && !m.ends_with(".rook") {
                    m = m.replace('.', "/");
                }
                if !m.is_empty() { rook_modules.push(m); }
            }
        }
    }
    (c_headers, rook_modules)
}

pub fn scan_rook_symbols(text: &str, file_path: &Path) -> Vec<RookSymbol> {
    let mut symbols = Vec::new();
    let mut current_impl: Option<String> = None;
    let mut in_object: Option<String> = None;
    let mut obj_fields: Vec<(String, String)> = Vec::new();

    for (idx, raw_line) in text.lines().enumerate() {
        let line_num = idx as u32 + 1;
        let line = if let Some(c) = raw_line.find("//") {
            &raw_line[..c]
        } else {
            raw_line
        }.trim();

        if line.is_empty() { continue; }

        if line.contains('}') {
            if let Some(obj_name) = in_object.take() {
                symbols.push(RookSymbol {
                    name: obj_name,
                    kind: RookSymbolKind::Object { fields: std::mem::take(&mut obj_fields) },
                    file_path: file_path.to_path_buf(),
                    line: line_num,
                    col: 1,
                });
            }
            if current_impl.is_some() && !line.contains('{') {
                current_impl = None;
            }
        }

        if let Some(rest) = line.strip_prefix("object ") {
            let name = rest.split(['{', ' ']).next().unwrap_or("").trim().to_string();
            if !name.is_empty() {
                in_object = Some(name);
                obj_fields.clear();
            }
            continue;
        }

        if in_object.is_some() {
            if let Some((fname, ftype)) = line.split_once(':') {
                let fname = fname.trim().to_string();
                let ftype = ftype.trim().trim_end_matches(',').trim().to_string();
                if !fname.is_empty() {
                    obj_fields.push((fname, ftype));
                }
            }
            continue;
        }

        if let Some(rest) = line.strip_prefix("impl ") {
            let name = rest.split(['{', ' ']).next().unwrap_or("").trim().to_string();
            if !name.is_empty() {
                current_impl = Some(name);
            }
            continue;
        }

        if line.starts_with("sum ") || line.starts_with("enum ") {
            let rest = if line.starts_with("sum ") { &line[4..] } else { &line[5..] };
            let name = rest.split(['{', ' ']).next().unwrap_or("").trim().to_string();
            if !name.is_empty() {
                symbols.push(RookSymbol {
                    name,
                    kind: RookSymbolKind::Enum { variants: Vec::new() },
                    file_path: file_path.to_path_buf(),
                    line: line_num,
                    col: 1,
                });
            }
            continue;
        }

        if let Some(rest) = line.strip_prefix("let ") {
            let mut parts = rest.split(['=', ';', ':']);
            let var_name = parts.next().unwrap_or("").trim().to_string();
            let var_type = if rest.contains(':') && (!rest.contains('=') || rest.find(':') < rest.find('=')) {
                rest.split(':').nth(1).and_then(|s| s.split('=').next()).map(|s| s.trim().to_string())
            } else {
                None
            };
            if !var_name.is_empty() && var_name.chars().all(|c| c.is_alphanumeric() || c == '_') {
                symbols.push(RookSymbol {
                    name: var_name,
                    kind: RookSymbolKind::Variable { type_name: var_type },
                    file_path: file_path.to_path_buf(),
                    line: line_num,
                    col: 5,
                });
            }
            continue;
        }

        // Functions and methods
        if let Some(paren) = line.find('(') {
            let pre_paren = line[..paren].trim();
            if pre_paren.contains(' ') {
                let mut words = pre_paren.split_whitespace();
                let ret_or_kw = words.next().unwrap_or("");
                let fn_name = words.next().unwrap_or("");
                if !fn_name.is_empty() && fn_name.chars().all(|c| c.is_alphanumeric() || c == '_') {
                    if ret_or_kw != "if" && ret_or_kw != "while" && ret_or_kw != "for" && ret_or_kw != "switch" {
                        let sig = if let Some(close) = line[paren..].find(')') {
                            format!("{} {}{}", ret_or_kw, fn_name, &line[paren..paren + close + 1])
                        } else {
                            format!("{} {}()", ret_or_kw, fn_name)
                        };

                        if let Some(ref obj) = current_impl {
                            symbols.push(RookSymbol {
                                name: fn_name.to_string(),
                                kind: RookSymbolKind::Method { obj: obj.clone(), sig },
                                file_path: file_path.to_path_buf(),
                                line: line_num,
                                col: 1,
                            });
                        } else {
                            symbols.push(RookSymbol {
                                name: fn_name.to_string(),
                                kind: RookSymbolKind::Function { sig },
                                file_path: file_path.to_path_buf(),
                                line: line_num,
                                col: 1,
                            });
                        }
                    }
                }
            }
        }
    }

    symbols
}

pub fn scan_c_header(path: &Path) -> Vec<CSymbol> {
    let text = match fs::read_to_string(path) {
        Ok(t) => t,
        Err(_) => return Vec::new(),
    };

    let header_name = path.file_name().and_then(|s| s.to_str()).unwrap_or("").to_string();
    let mut symbols = Vec::new();

    for (idx, raw_line) in text.lines().enumerate() {
        let line_num = idx as u32 + 1;
        let mut line = if let Some(c) = raw_line.find("//") {
            &raw_line[..c]
        } else {
            raw_line
        }.trim();
        if let Some(c) = line.find("/*") {
            line = line[..c].trim();
        }
        if line.is_empty() { continue; }

        if let Some(rest) = line.strip_prefix("#define ") {
            let mut parts = rest.split_whitespace();
            if let Some(name) = parts.next() {
                let clean_name = name.split('(').next().unwrap_or("").trim();
                if !clean_name.is_empty() && clean_name.chars().all(|c| c.is_alphanumeric() || c == '_') {
                    let val = parts.next().map(|s| s.to_string());
                    symbols.push(CSymbol {
                        name: clean_name.to_string(),
                        kind: CSymbolKind::Constant { value: val },
                        header: header_name.clone(),
                        file_path: Some(path.to_path_buf()),
                        line: line_num,
                    });
                }
            }
            continue;
        }

        if line.starts_with("typedef struct ") || line.starts_with("struct ") {
            let rest = if line.starts_with("typedef struct ") { &line[15..] } else { &line[7..] };
            let name = rest.split([' ', '{', ';']).next().unwrap_or("").trim();
            if !name.is_empty() && name.chars().all(|c| c.is_alphanumeric() || c == '_') {
                symbols.push(CSymbol {
                    name: name.to_string(),
                    kind: CSymbolKind::Struct,
                    header: header_name.clone(),
                    file_path: Some(path.to_path_buf()),
                    line: line_num,
                });
            }
            continue;
        }

        // C function prototypes ending in ';'
        if line.ends_with(';') && line.contains('(') && line.contains(')') {
            let paren = line.find('(').unwrap();
            let pre = line[..paren].trim();
            // Filter out typedefs or control statements
            if !pre.starts_with("typedef") && !pre.starts_with("return") {
                let parts: Vec<&str> = pre.split_whitespace().collect();
                if parts.len() >= 2 {
                    let fn_name = parts.last().unwrap().trim_start_matches('*');
                    if !fn_name.is_empty() && fn_name.chars().all(|c| c.is_alphanumeric() || c == '_') {
                        symbols.push(CSymbol {
                            name: fn_name.to_string(),
                            kind: CSymbolKind::Function { sig: line.trim_end_matches(';').trim().to_string() },
                            header: header_name.clone(),
                            file_path: Some(path.to_path_buf()),
                            line: line_num,
                        });
                    }
                }
            }
        }
    }

    symbols
}

impl ServerState {
    pub fn resolve_c_header(&mut self, header_name: &str, doc_dir: Option<&Path>) -> Option<PathBuf> {
        let mut candidates = Vec::new();
        if let Some(dir) = doc_dir {
            candidates.push(dir.join(header_name));
            candidates.push(dir.join("include").join(header_name));
            if let Some(parent) = dir.parent() {
                candidates.push(parent.join("include").join(header_name));
                candidates.push(parent.join("src").join(header_name));
            }
        }
        candidates.push(PathBuf::from("/usr/include").join(header_name));
        candidates.push(PathBuf::from("/usr/local/include").join(header_name));
        candidates.push(PathBuf::from("/usr/include/x86_64-linux-gnu").join(header_name));

        for p in candidates {
            if p.is_file() {
                return Some(p);
            }
        }
        None
    }

    pub fn get_c_header_symbols(&mut self, header_name: &str, doc_dir: Option<&Path>) -> Vec<CSymbol> {
        if let Some(path) = self.resolve_c_header(header_name, doc_dir) {
            let mtime = fs::metadata(&path).and_then(|m| m.modified()).unwrap_or(SystemTime::UNIX_EPOCH);
            if let Some((cached_time, ref syms)) = self.c_header_cache.get(&path) {
                if *cached_time == mtime {
                    return syms.clone();
                }
            }
            let syms = scan_c_header(&path);
            self.c_header_cache.insert(path, (mtime, syms.clone()));
            return syms;
        }
        Vec::new()
    }

    pub fn resolve_rook_module(&self, module: &str, doc_dir: Option<&Path>) -> Option<PathBuf> {
        if module == "std" {
            if let Some(ref std_dir) = self.std_dir {
                let p = std_dir.join("std.rook");
                if p.is_file() { return Some(p); }
            }
        }
        if module.starts_with("std/") {
            let sub = &module[4..];
            if let Some(ref std_dir) = self.std_dir {
                let mut p = std_dir.join(sub);
                if !p.to_string_lossy().ends_with(".rook") {
                    p.set_extension("rook");
                }
                if p.is_file() { return Some(p); }
            }
        }
        if let Some(dir) = doc_dir {
            let mut p = dir.join(module);
            if !p.to_string_lossy().ends_with(".rook") {
                p.set_extension("rook");
            }
            if p.is_file() { return Some(p); }
        }
        None
    }

    pub fn get_rook_module_symbols(&mut self, module: &str, doc_dir: Option<&Path>) -> Vec<RookSymbol> {
        if let Some(path) = self.resolve_rook_module(module, doc_dir) {
            let mtime = fs::metadata(&path).and_then(|m| m.modified()).unwrap_or(SystemTime::UNIX_EPOCH);
            if let Some((cached_time, ref syms)) = self.rook_module_cache.get(&path) {
                if *cached_time == mtime {
                    return syms.clone();
                }
            }
            if let Ok(content) = fs::read_to_string(&path) {
                let syms = scan_rook_symbols(&content, &path);
                self.rook_module_cache.insert(path, (mtime, syms.clone()));
                return syms;
            }
        }
        Vec::new()
    }
}

// ─── Snippets & Autocompletion ─────────────────────────────────────────

fn get_keyword_and_snippet_completions() -> Vec<CompletionItem> {
    vec![
        // Rook Keywords
        CompletionItem {
            label: "object".to_string(),
            kind: Some(CompletionItemKind::KEYWORD),
            detail: Some("Define a Rook object (OOP struct)".to_string()),
            insert_text: Some("object ${1:Name} {\n\t${0}\n}".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..Default::default()
        },
        CompletionItem {
            label: "impl".to_string(),
            kind: Some(CompletionItemKind::KEYWORD),
            detail: Some("Implement methods for a Rook object".to_string()),
            insert_text: Some("impl ${1:Name} {\n\t${0}\n}".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..Default::default()
        },
        CompletionItem {
            label: "sum".to_string(),
            kind: Some(CompletionItemKind::KEYWORD),
            detail: Some("Define a tagged union (sum type)".to_string()),
            insert_text: Some("sum ${1:Name} {\n\t${0}\n}".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..Default::default()
        },
        CompletionItem {
            label: "match".to_string(),
            kind: Some(CompletionItemKind::KEYWORD),
            detail: Some("Pattern match on a sum type or value".to_string()),
            insert_text: Some("match ${1:expr} {\n\t${2:pattern} => ${0}\n}".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..Default::default()
        },
        CompletionItem {
            label: "defer".to_string(),
            kind: Some(CompletionItemKind::KEYWORD),
            detail: Some("Execute statement when exiting scope".to_string()),
            insert_text: Some("defer ${0};".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..Default::default()
        },
        CompletionItem {
            label: "let".to_string(),
            kind: Some(CompletionItemKind::KEYWORD),
            detail: Some("Declare a variable".to_string()),
            insert_text: Some("let ${1:var} = ${0};".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..Default::default()
        },
        CompletionItem {
            label: "comprise".to_string(),
            kind: Some(CompletionItemKind::KEYWORD),
            detail: Some("Import a Rook module".to_string()),
            insert_text: Some("#comprise <${1:std/io}>".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..Default::default()
        },
        CompletionItem {
            label: "include".to_string(),
            kind: Some(CompletionItemKind::KEYWORD),
            detail: Some("Include a C header".to_string()),
            insert_text: Some("#include <${1:stdio.h}>".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..Default::default()
        },
        CompletionItem {
            label: "fn".to_string(),
            kind: Some(CompletionItemKind::KEYWORD),
            detail: Some("Function definition".to_string()),
            insert_text: Some("fn ${1:name}(${2}) -> ${3:void} {\n\t${0}\n}".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..Default::default()
        },
        CompletionItem {
            label: "forin".to_string(),
            kind: Some(CompletionItemKind::SNIPPET),
            detail: Some("For-in loop".to_string()),
            insert_text: Some("for ${1:item} in ${2:array} {\n\t${0}\n}".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..Default::default()
        },
        CompletionItem {
            label: "return".to_string(),
            kind: Some(CompletionItemKind::KEYWORD),
            ..Default::default()
        },
        CompletionItem {
            label: "if".to_string(),
            kind: Some(CompletionItemKind::KEYWORD),
            insert_text: Some("if (${1:condition}) {\n\t${0}\n}".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..Default::default()
        },
        CompletionItem {
            label: "else".to_string(),
            kind: Some(CompletionItemKind::KEYWORD),
            insert_text: Some("else {\n\t${0}\n}".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..Default::default()
        },
        CompletionItem {
            label: "while".to_string(),
            kind: Some(CompletionItemKind::KEYWORD),
            insert_text: Some("while (${1:condition}) {\n\t${0}\n}".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..Default::default()
        },
        CompletionItem {
            label: "struct".to_string(),
            kind: Some(CompletionItemKind::KEYWORD),
            insert_text: Some("struct ${1:Name} {\n\t${0}\n};".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..Default::default()
        },
        CompletionItem {
            label: "enum".to_string(),
            kind: Some(CompletionItemKind::KEYWORD),
            insert_text: Some("enum ${1:Name} {\n\t${0}\n};".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..Default::default()
        },
        // Common Primitive Types
        CompletionItem { label: "int".to_string(), kind: Some(CompletionItemKind::TYPE_PARAMETER), ..Default::default() },
        CompletionItem { label: "float".to_string(), kind: Some(CompletionItemKind::TYPE_PARAMETER), ..Default::default() },
        CompletionItem { label: "double".to_string(), kind: Some(CompletionItemKind::TYPE_PARAMETER), ..Default::default() },
        CompletionItem { label: "char".to_string(), kind: Some(CompletionItemKind::TYPE_PARAMETER), ..Default::default() },
        CompletionItem { label: "void".to_string(), kind: Some(CompletionItemKind::TYPE_PARAMETER), ..Default::default() },
        CompletionItem { label: "bool".to_string(), kind: Some(CompletionItemKind::TYPE_PARAMETER), ..Default::default() },
        CompletionItem { label: "size_t".to_string(), kind: Some(CompletionItemKind::TYPE_PARAMETER), ..Default::default() },
        CompletionItem { label: "int32_t".to_string(), kind: Some(CompletionItemKind::TYPE_PARAMETER), ..Default::default() },
        CompletionItem { label: "uint32_t".to_string(), kind: Some(CompletionItemKind::TYPE_PARAMETER), ..Default::default() },
        CompletionItem { label: "int64_t".to_string(), kind: Some(CompletionItemKind::TYPE_PARAMETER), ..Default::default() },
        CompletionItem { label: "uint64_t".to_string(), kind: Some(CompletionItemKind::TYPE_PARAMETER), ..Default::default() },
    ]
}

pub fn get_completions(state: &mut ServerState, uri: &str, content: &str) -> Vec<CompletionItem> {
    let mut items = get_keyword_and_snippet_completions();
    let mut seen: std::collections::HashSet<String> = items.iter().map(|i| i.label.clone()).collect();

    let doc_path = uri_to_path(uri);
    let doc_dir = doc_path.as_ref().and_then(|p| p.parent());

    // 1. Current buffer symbols
    let current_doc_path = doc_path.clone().unwrap_or_else(|| PathBuf::from("current.rook"));
    let cur_symbols = scan_rook_symbols(content, &current_doc_path);
    for sym in cur_symbols {
        if seen.insert(sym.name.clone()) {
            let (kind, detail) = match sym.kind {
                RookSymbolKind::Function { sig } => (CompletionItemKind::FUNCTION, sig),
                RookSymbolKind::Object { .. } => (CompletionItemKind::CLASS, format!("object {}", sym.name)),
                RookSymbolKind::Method { obj, sig } => (CompletionItemKind::METHOD, format!("{}.{}", obj, sig)),
                RookSymbolKind::Enum { .. } => (CompletionItemKind::ENUM, format!("enum {}", sym.name)),
                RookSymbolKind::Variable { type_name } => (
                    CompletionItemKind::VARIABLE,
                    type_name.unwrap_or_else(|| "let".to_string()),
                ),
            };
            items.push(CompletionItem {
                label: sym.name,
                kind: Some(kind),
                detail: Some(detail),
                ..Default::default()
            });
        }
    }

    // 2. Directives: Comprised Rook modules and Included C headers
    let (c_headers, rook_modules) = extract_directives(content);

    // Comprised Rook modules
    for module in rook_modules {
        let mod_syms = state.get_rook_module_symbols(&module, doc_dir);
        for sym in mod_syms {
            if seen.insert(sym.name.clone()) {
                let (kind, detail) = match sym.kind {
                    RookSymbolKind::Function { sig } => (CompletionItemKind::FUNCTION, format!("{} [{}]", sig, module)),
                    RookSymbolKind::Object { .. } => (CompletionItemKind::CLASS, format!("object {} [{}]", sym.name, module)),
                    RookSymbolKind::Method { obj, sig } => (CompletionItemKind::METHOD, format!("{}.{} [{}]", obj, sig, module)),
                    RookSymbolKind::Enum { .. } => (CompletionItemKind::ENUM, format!("enum {} [{}]", sym.name, module)),
                    RookSymbolKind::Variable { type_name } => (
                        CompletionItemKind::VARIABLE,
                        type_name.map(|t| format!("{}: {}", sym.name, t)).unwrap_or_else(|| sym.name.clone()),
                    ),
                };
                items.push(CompletionItem {
                    label: sym.name,
                    kind: Some(kind),
                    detail: Some(detail),
                    ..Default::default()
                });
            }
        }
    }

    // Included C headers
    for header in c_headers {
        let c_syms = state.get_c_header_symbols(&header, doc_dir);
        for sym in c_syms {
            if seen.insert(sym.name.clone()) {
                let (kind, detail) = match sym.kind {
                    CSymbolKind::Function { sig } => (CompletionItemKind::FUNCTION, format!("{} [{}]", sig, header)),
                    CSymbolKind::Struct => (CompletionItemKind::STRUCT, format!("struct {} [{}]", sym.name, header)),
                    CSymbolKind::Typedef { target } => (CompletionItemKind::TYPE_PARAMETER, format!("typedef {} [{}]", target, header)),
                    CSymbolKind::Constant { value } => (
                        CompletionItemKind::CONSTANT,
                        format!("{} = {} [{}]", sym.name, value.unwrap_or_default(), header),
                    ),
                };
                items.push(CompletionItem {
                    label: sym.name,
                    kind: Some(kind),
                    detail: Some(detail),
                    ..Default::default()
                });
            }
        }
    }

    // 3. Bundled Libc symbols fallback
    for cf in &state.cfuncs {
        if seen.insert(cf.name.clone()) {
            items.push(CompletionItem {
                label: cf.name.clone(),
                kind: Some(CompletionItemKind::FUNCTION),
                detail: Some(format!("{} [libc]", sig(cf))),
                ..Default::default()
            });
        }
    }

    items
}

// ─── Hover Documentation ───────────────────────────────────────────────

pub fn do_hover(state: &mut ServerState, params: &HoverParams, content: Option<&str>, uri: &str) -> Option<Hover> {
    let p = &params.text_document_position_params;
    let text = content?;
    let word = word_at(text, p.position)?;

    let doc_path = uri_to_path(uri);
    let doc_dir = doc_path.as_ref().and_then(|path| path.parent());

    // 1. Check current buffer symbols
    let cur_path = doc_path.clone().unwrap_or_else(|| PathBuf::from("current.rook"));
    let cur_symbols = scan_rook_symbols(text, &cur_path);
    if let Some(sym) = cur_symbols.iter().find(|s| s.name == word) {
        let desc = match &sym.kind {
            RookSymbolKind::Function { sig } => format!("```rook\n{}\n```\nDefined in current file.", sig),
            RookSymbolKind::Object { fields } => {
                let flds: Vec<String> = fields.iter().map(|(f, t)| format!("    {}: {}", f, t)).collect();
                format!("```rook\nobject {} {{\n{}\n}}\n```", sym.name, flds.join("\n"))
            }
            RookSymbolKind::Method { obj, sig } => format!("```rook\n// Method on {}\n{}\n```", obj, sig),
            RookSymbolKind::Enum { .. } => format!("```rook\nenum {}\n```", sym.name),
            RookSymbolKind::Variable { type_name } => format!("```rook\nlet {}{};\n```", sym.name, type_name.as_ref().map(|t| format!(": {}", t)).unwrap_or_default()),
        };
        let mk = MarkupContent { kind: MarkupKind::Markdown, value: desc };
        return Some(Hover { contents: lsp_types::HoverContents::Markup(mk), range: None });
    }

    // 2. Check comprised Rook modules
    let (c_headers, rook_modules) = extract_directives(text);
    for module in rook_modules {
        let mod_syms = state.get_rook_module_symbols(&module, doc_dir);
        if let Some(sym) = mod_syms.iter().find(|s| s.name == word) {
            let desc = match &sym.kind {
                RookSymbolKind::Function { sig } => format!("```rook\n{}\n```\n*Imported from module `{}`*", sig, module),
                RookSymbolKind::Object { fields } => {
                    let flds: Vec<String> = fields.iter().map(|(f, t)| format!("    {}: {}", f, t)).collect();
                    format!("```rook\nobject {} {{\n{}\n}}\n```\n*Imported from module `{}`*", sym.name, flds.join("\n"), module)
                }
                RookSymbolKind::Method { obj, sig } => format!("```rook\n// Method on {}\n{}\n```\n*Imported from module `{}`*", obj, sig, module),
                RookSymbolKind::Enum { .. } => format!("```rook\nenum {}\n```\n*Imported from module `{}`*", sym.name, module),
                RookSymbolKind::Variable { type_name } => format!("```rook\nlet {}{};\n```\n*Imported from module `{}`*", sym.name, type_name.as_ref().map(|t| format!(": {}", t)).unwrap_or_default(), module),
            };
            let mk = MarkupContent { kind: MarkupKind::Markdown, value: desc };
            return Some(Hover { contents: lsp_types::HoverContents::Markup(mk), range: None });
        }
    }

    // 3. Check included C headers
    for header in c_headers {
        let c_syms = state.get_c_header_symbols(&header, doc_dir);
        if let Some(sym) = c_syms.iter().find(|s| s.name == word) {
            let desc = match &sym.kind {
                CSymbolKind::Function { sig } => format!("```c\n{}\n```\n*Declared in C header `<{}>`*", sig, header),
                CSymbolKind::Struct => format!("```c\nstruct {};\n```\n*Declared in C header `<{}>`*", sym.name, header),
                CSymbolKind::Typedef { target } => format!("```c\ntypedef {} {};\n```\n*Declared in C header `<{}>`*", target, sym.name, header),
                CSymbolKind::Constant { value } => format!("```c\n#define {} {}\n```\n*Constant from C header `<{}>`*", sym.name, value.as_deref().unwrap_or(""), header),
            };
            let mk = MarkupContent { kind: MarkupKind::Markdown, value: desc };
            return Some(Hover { contents: lsp_types::HoverContents::Markup(mk), range: None });
        }
    }

    // 4. Check Libc functions
    if let Some(cf) = state.cfuncs.iter().find(|c| c.name == word) {
        let mk = MarkupContent {
            kind: MarkupKind::Markdown,
            value: format!("```c\n{}\n```\n*C standard library (libc)*", sig(cf)),
        };
        return Some(Hover { contents: lsp_types::HoverContents::Markup(mk), range: None });
    }

    // 5. Rook Keywords info
    let kw_doc = match word.as_str() {
        "defer" => Some("`defer <statement>`\n\nSchedules a statement to execute automatically when exiting the current enclosing block scope."),
        "object" => Some("`object <Name> { <fields> }`\n\nDeclares a Rook object with typed fields."),
        "impl" => Some("`impl <Name> { <methods> }`\n\nImplements static compile-time methods for a Rook object."),
        "sum" => Some("`sum <Name> { <variants> }`\n\nDeclares an algebraic sum type (tagged union)."),
        "match" => Some("`match <expr> { <pattern> => <stmt> }`\n\nPattern matches on a value or sum type."),
        "let" => Some("`let <name> [: <type>] = <expr>;`\n\nDeclares a local variable with optional type inference."),
        "comprise" | "#comprise" => Some("`#comprise <module>`\n\nImports a Rook module. Searches standard library `<std/...>` or relative directory."),
        _ => None,
    };

    kw_doc.map(|d| Hover {
        contents: lsp_types::HoverContents::Markup(MarkupContent { kind: MarkupKind::Markdown, value: d.to_string() }),
        range: None,
    })
}

// ─── Goto Definition ───────────────────────────────────────────────────

pub fn get_document_links(state: &mut ServerState, uri: &str, text: &str) -> Vec<DocumentLink> {
    let mut links = Vec::new();
    let doc_path = uri_to_path(uri);
    let doc_dir = doc_path.as_ref().and_then(|p| p.parent());

    for (idx, raw_line) in text.lines().enumerate() {
        let line_num = idx as u32;
        let line = raw_line.trim_end();
        if let Some(inc_idx) = line.find("#include") {
            let rest = &line[inc_idx + "#include".len()..];
            if let Some(start_rel) = rest.find(['<', '"']) {
                let quote = rest.chars().nth(start_rel).unwrap();
                let close = if quote == '<' { '>' } else { '"' };
                if let Some(end_rel) = rest[start_rel + 1..].find(close) {
                    let h = rest[start_rel + 1..start_rel + 1 + end_rel].trim();
                    if let Some(target_p) = state.resolve_c_header(h, doc_dir) {
                        if let Ok(target_uri) = format!("file://{}", target_p.display()).parse() {
                            let start_char = (inc_idx + "#include".len() + start_rel) as u32;
                            let end_char = (inc_idx + "#include".len() + start_rel + 1 + end_rel + 1) as u32;
                            links.push(DocumentLink {
                                range: Range {
                                    start: Position { line: line_num, character: start_char },
                                    end: Position { line: line_num, character: end_char },
                                },
                                target: Some(target_uri),
                                tooltip: Some(format!("Open {}", target_p.display())),
                                data: None,
                            });
                        }
                    }
                }
            }
        } else if let Some(comp_idx) = line.find("#comprise").or_else(|| line.find("comprise")) {
            let kw_len = if line[comp_idx..].starts_with('#') { "#comprise".len() } else { "comprise".len() };
            let rest = &line[comp_idx + kw_len..];
            if let Some(start_rel) = rest.find(['<', '"']) {
                let quote = rest.chars().nth(start_rel).unwrap();
                let close = if quote == '<' { '>' } else { '"' };
                if let Some(end_rel) = rest[start_rel + 1..].find(close) {
                    let mut m = rest[start_rel + 1..start_rel + 1 + end_rel].trim().to_string();
                    if m.contains('.') && !m.ends_with(".rook") {
                        m = m.replace('.', "/");
                    }
                    if let Some(target_p) = state.resolve_rook_module(&m, doc_dir) {
                        if let Ok(target_uri) = format!("file://{}", target_p.display()).parse() {
                            let start_char = (comp_idx + kw_len + start_rel) as u32;
                            let end_char = (comp_idx + kw_len + start_rel + 1 + end_rel + 1) as u32;
                            links.push(DocumentLink {
                                range: Range {
                                    start: Position { line: line_num, character: start_char },
                                    end: Position { line: line_num, character: end_char },
                                },
                                target: Some(target_uri),
                                tooltip: Some(format!("Open {}", target_p.display())),
                                data: None,
                            });
                        }
                    }
                }
            }
        }
    }
    links
}

pub fn find_definition(state: &mut ServerState, text: &str, uri: &str, position: Position) -> Option<Location> {
    let lines: Vec<&str> = text.lines().collect();
    let line_idx = position.line as usize;
    let doc_path = uri_to_path(uri);
    let doc_dir = doc_path.as_ref().and_then(|p| p.parent());

    // 0. Check if the line under cursor is an #include or #comprise line
    if let Some(&raw_line) = lines.get(line_idx) {
        let trimmed = raw_line.trim();
        if trimmed.starts_with("#include") {
            if let Some(start) = trimmed.find(['<', '"']) {
                let quote = trimmed.chars().nth(start).unwrap();
                let close = if quote == '<' { '>' } else { '"' };
                if let Some(end) = trimmed[start + 1..].find(close) {
                    let h = trimmed[start + 1..start + 1 + end].trim();
                    if let Some(target_p) = state.resolve_c_header(h, doc_dir) {
                        if let Ok(target_uri) = format!("file://{}", target_p.display()).parse() {
                            let pos = Position { line: 0, character: 0 };
                            return Some(Location { uri: target_uri, range: Range { start: pos, end: pos } });
                        }
                    }
                }
            }
        } else if trimmed.starts_with("#comprise") || trimmed.starts_with("comprise") {
            let kw_len = if trimmed.starts_with('#') { "#comprise".len() } else { "comprise".len() };
            let rest = trimmed[kw_len..].trim().trim_end_matches(';').trim();
            let mod_name = if let Some(start) = rest.find(['<', '"']) {
                let quote = rest.chars().nth(start).unwrap();
                let close = if quote == '<' { '>' } else { '"' };
                rest[start + 1..].find(close).map(|end| rest[start + 1..start + 1 + end].trim().to_string())
            } else {
                let bare = rest.split_whitespace().next().unwrap_or("");
                if !bare.is_empty() { Some(bare.to_string()) } else { None }
            };
            if let Some(mut m) = mod_name {
                if m.contains('.') && !m.ends_with(".rook") {
                    m = m.replace('.', "/");
                }
                if let Some(target_p) = state.resolve_rook_module(&m, doc_dir) {
                    if let Ok(target_uri) = format!("file://{}", target_p.display()).parse() {
                        let pos = Position { line: 0, character: 0 };
                        return Some(Location { uri: target_uri, range: Range { start: pos, end: pos } });
                    }
                }
            }
        }
    }

    let word = word_at(text, position)?;

    // 1. Current document
    let cur_path = doc_path.clone().unwrap_or_else(|| PathBuf::from("current.rook"));
    let cur_symbols = scan_rook_symbols(text, &cur_path);
    if let Some(sym) = cur_symbols.iter().find(|s| s.name == word) {
        let pos = Position { line: sym.line.saturating_sub(1), character: sym.col.saturating_sub(1) };
        if let Ok(target_uri) = uri.parse() {
            return Some(Location { uri: target_uri, range: Range { start: pos, end: pos } });
        }
    }

    // 2. Comprised modules
    let (_, rook_modules) = extract_directives(text);
    for module in rook_modules {
        let mod_syms = state.get_rook_module_symbols(&module, doc_dir);
        if let Some(sym) = mod_syms.iter().find(|s| s.name == word) {
            let pos = Position { line: sym.line.saturating_sub(1), character: sym.col.saturating_sub(1) };
            if let Ok(target_uri) = format!("file://{}", sym.file_path.display()).parse() {
                return Some(Location { uri: target_uri, range: Range { start: pos, end: pos } });
            }
        }
    }

    // 3. Included C headers
    let (c_headers, _) = extract_directives(text);
    for header in c_headers {
        let c_syms = state.get_c_header_symbols(&header, doc_dir);
        if let Some(sym) = c_syms.iter().find(|s| s.name == word) {
            if let Some(ref fp) = sym.file_path {
                let pos = Position { line: sym.line.saturating_sub(1), character: 0 };
                if let Ok(target_uri) = format!("file://{}", fp.display()).parse() {
                    return Some(Location { uri: target_uri, range: Range { start: pos, end: pos } });
                }
            }
        }
    }

    None
}

// ─── Helpers ───────────────────────────────────────────────────────────

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

fn word_at(text: &str, position: Position) -> Option<String> {
    let lines: Vec<&str> = text.lines().collect();
    let l = position.line as usize;
    if l >= lines.len() { return None; }
    let chars: Vec<char> = lines[l].chars().collect();
    let mut idx = (position.character as usize).min(chars.len());
    while idx > 0 && (chars[idx - 1].is_alphanumeric() || chars[idx - 1] == '_') { idx -= 1; }
    let start = idx;
    let mut end = idx;
    while end < chars.len() && (chars[end].is_alphanumeric() || chars[end] == '_') { end += 1; }
    if start >= end { return None; }
    Some(chars[start..end].iter().collect())
}

// ─── JSON-RPC 2.0 Framing ──────────────────────────────────────────────

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
        if r.read_line(&mut line).ok()? == 0 { return None; }
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

// ─── Diagnostics via `rokade --diagnostics` ────────────────────────────

#[derive(Debug)]
struct RDiag { line: u32, character: u32, severity: u8, message: String }

fn strip_ansi(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut chars = s.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '\x1b' {
            if chars.peek() == Some(&'[') {
                chars.next();
                for sc in chars.by_ref() {
                    if sc == 'm' { break; }
                }
            }
            continue;
        }
        out.push(c);
    }
    out
}

fn rook_diagnostics(rokade: &str, commandlist_dir: &str, content: &str, doc_dir: Option<&Path>) -> Vec<RDiag> {
    let mut tmp = std::env::temp_dir();
    tmp.push(format!("rook_lsp_{}.rook", DOC_SEQ.fetch_add(1, Ordering::Relaxed)));
    let _ = std::fs::write(&tmp, content);
    let mut cmd = Command::new(rokade);
    cmd.arg("--diagnostics").arg(&tmp);
    cmd.env("ROKADE_DATA_DIR", commandlist_dir);
    if let Some(dir) = doc_dir {
        cmd.env("ROKADE_BASE_DIR", dir);
    }
    let out = cmd.output();
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

fn parse_rokade_json(json: &str) -> Vec<RDiag> {
    let v: Value = match serde_json::from_str(json) { Ok(v) => v, Err(_) => return Vec::new() };
    let mut diags = Vec::new();
    let arr = match v.as_array() { Some(a) => a, None => return diags };
    for d in arr {
        let line = d.get("line").and_then(|x| x.as_u64()).unwrap_or(1) as u32;
        let character = d.get("character").and_then(|x| x.as_u64()).unwrap_or(1) as u32;
        let raw_msg = d.get("message").and_then(|x| x.as_str()).unwrap_or("");
        let message = strip_ansi(raw_msg);
        let sev = match d.get("severity").and_then(|x| x.as_str()).unwrap_or("error") {
            "warning" => 2, "info" => 3, "hint" => 4, _ => 1,
        };
        diags.push(RDiag { line, character, severity: sev, message });
    }
    diags
}

fn publish_diagnostics<W: Write>(w: &mut W, uri: &str, diags: &[RDiag]) {
    let items: Vec<Diagnostic> = diags.iter().map(|d| {
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
    let uri_parsed: Uri = uri.parse().unwrap_or_else(|_| "file:///rook".parse().unwrap());
    let params = PublishDiagnosticsParams {
        uri: uri_parsed,
        diagnostics: items,
        version: None,
    };
    send_notification(w, "textDocument/publishDiagnostics", serde_json::to_value(params).unwrap());
}

// ─── Outline Symbols ───────────────────────────────────────────────────

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
    let uri: Uri = match doc_uri.parse() { Ok(u) => u, Err(_) => return Vec::new() };
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
        let pos = Position { line: line.saturating_sub(1), character: col.saturating_sub(1) };
        Some(lsp_types::SymbolInformation {
            name: name.to_string(),
            kind: sym_kind,
            tags: None,
            deprecated: None,
            location: Location { uri: uri.clone(), range: Range { start: pos, end: pos } },
            container_name: None,
        })
    }).collect()
}

// ─── Request Dispatch ──────────────────────────────────────────────────

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
                    document_link_provider: Some(DocumentLinkOptions {
                        resolve_provider: Some(false),
                        work_done_progress_options: Default::default(),
                    }),
                    completion_provider: Some(CompletionOptions {
                        trigger_characters: Some(vec![
                            ".".to_string(),
                            ">".to_string(),
                            ":".to_string(),
                            "#".to_string(),
                        ]),
                        ..Default::default()
                    }),
                    ..Default::default()
                },
                ..Default::default()
            };
            send_response(w, frame.id.clone(), serde_json::to_value(result).unwrap());
        }
        Some("textDocument/didOpen") => {
            if let Ok(p) = serde_json::from_value::<DidOpenTextDocumentParams>(params) {
                let uri = p.text_document.uri.to_string();
                let content = p.text_document.text;
                let ver = p.text_document.version;
                state.docs.insert(uri.clone(), (content.clone(), ver));
                let doc_path = uri_to_path(&uri);
                let diags = rook_diagnostics(&state.rokade, cmdir, &content, doc_path.as_ref().and_then(|p| p.parent()));
                publish_diagnostics(w, &uri, &diags);
            }
        }
        Some("textDocument/didChange") => {
            if let Ok(p) = serde_json::from_value::<DidChangeTextDocumentParams>(params) {
                let uri = p.text_document.uri.to_string();
                let content = p.content_changes.into_iter()
                    .next().map(|c| c.text).unwrap_or_default();
                let ver = p.text_document.version;
                state.docs.insert(uri.clone(), (content.clone(), ver));
                let doc_path = uri_to_path(&uri);
                let diags = rook_diagnostics(&state.rokade, cmdir, &content, doc_path.as_ref().and_then(|p| p.parent()));
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
            if let Ok(p) = serde_json::from_value::<CompletionParams>(params) {
                let uri = p.text_document_position.text_document.uri.to_string();
                let content = state.docs.get(&uri).map(|(c, _)| c.clone()).unwrap_or_default();
                let items = get_completions(state, &uri, &content);
                let list = CompletionList { is_incomplete: false, items };
                send_response(w, frame.id.clone(), serde_json::to_value(list).unwrap());
            }
        }
        Some("textDocument/hover") => {
            if let Ok(p) = serde_json::from_value::<HoverParams>(params) {
                let uri = p.text_document_position_params.text_document.uri.to_string();
                let content = state.docs.get(&uri).map(|(c, _)| c.clone());
                let result = do_hover(state, &p, content.as_deref(), &uri).map(|h| serde_json::to_value(h).unwrap());
                send_response(w, frame.id.clone(), result.unwrap_or(Value::Null));
            }
        }
        Some("textDocument/declaration" | "textDocument/definition") => {
            let uri = params.get("textDocument").and_then(|t| t.get("uri"))
                .and_then(|u| u.as_str()).unwrap_or("").to_string();
            let pos_val = params.get("position").and_then(|p| p.as_object());
            let line = pos_val.and_then(|p| p.get("line")).and_then(|x| x.as_u64()).unwrap_or(0) as u32;
            let character = pos_val.and_then(|p| p.get("character")).and_then(|x| x.as_u64()).unwrap_or(0) as u32;
            let content = state.docs.get(&uri).map(|(c, _)| c.clone()).unwrap_or_default();

            // Fast internal definition resolution first (covers comprises & C headers)
            let loc = find_definition(state, &content, &uri, Position { line, character });
            if let Some(l) = loc {
                send_response(w, frame.id.clone(), serde_json::to_value(l).unwrap());
            } else {
                // Fallback to compiler --def-at
                let mut tmp = std::env::temp_dir();
                tmp.push(format!("rook_lsp_def_{}.rook", DOC_SEQ.fetch_add(1, Ordering::Relaxed)));
                let _ = std::fs::write(&tmp, &content);
                let out = Command::new(&state.rokade)
                    .arg("--def-at")
                    .arg(&tmp)
                    .arg(line.to_string())
                    .arg(character.to_string())
                    .env("ROKADE_DATA_DIR", cmdir)
                    .output();
                let _ = std::fs::remove_file(&tmp);
                let result = out.ok().and_then(|o| {
                    if !o.status.success() { return None; }
                    let s = String::from_utf8_lossy(&o.stdout);
                    let v: Value = serde_json::from_str(&s).ok()?;
                    if v.is_null() { return None; }
                    let l: Location = serde_json::from_value(v).ok()?;
                    let target_uri: Uri = uri.parse().ok()?;
                    Some(Location { uri: target_uri, range: l.range })
                });
                send_response(w, frame.id.clone(), result.map(|l| serde_json::to_value(l).unwrap()).unwrap_or(Value::Null));
            }
        }
        Some("textDocument/documentSymbol") => {
            let uri = params.get("textDocument").and_then(|t| t.get("uri"))
                .and_then(|u| u.as_str()).unwrap_or("").to_string();
            let content = state.docs.get(&uri).map(|(c, _)| c.as_str()).unwrap_or("");
            let symbols = rook_symbols(&state.rokade, cmdir, content, &uri);
            send_response(w, frame.id.clone(), serde_json::to_value(symbols).unwrap());
        }
        Some("textDocument/documentLink") => {
            if let Ok(p) = serde_json::from_value::<DocumentLinkParams>(params) {
                let uri = p.text_document.uri.to_string();
                let content = state.docs.get(&uri).map(|(c, _)| c.clone()).unwrap_or_default();
                let links = get_document_links(state, &uri, &content);
                send_response(w, frame.id.clone(), serde_json::to_value(links).unwrap());
            }
        }
        Some("shutdown") => send_response(w, frame.id.clone(), Value::Null),
        Some("exit") => std::process::exit(0),
        _ => { send_response(w, frame.id.clone(), Value::Null); }
    }
}

fn ensure_commandlist_dir() -> String {
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
        docs: HashMap::new(),
        cfuncs: parse_commandlist(COMMANDLIST_JSON),
        rokade: std::env::var("ROKADE").unwrap_or_else(|_| "rokade".to_string()),
        c_header_cache: HashMap::new(),
        rook_module_cache: HashMap::new(),
        std_dir: find_std_dir(),
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
    fn test_extract_directives() {
        let code = r#"
        #comprise <std/io>
        #comprise std.math
        comprise "player.rook"
        #include <raylib.h>
        #include "custom.h"
        "#;
        let (c_headers, rook_modules) = extract_directives(code);
        assert_eq!(c_headers, vec!["raylib.h", "custom.h"]);
        assert_eq!(rook_modules, vec!["std/io", "std/math", "player.rook"]);
    }

    #[test]
    fn test_scan_rook_symbols() {
        let code = r#"
        object Player {
            id: int
            name: char*
        }

        impl Player {
            void greet(self) {
                println("hello");
            }
        }

        int add(int a, int b) {
            let total = a + b;
            return total;
        }
        "#;
        let syms = scan_rook_symbols(code, Path::new("test.rook"));
        let obj = syms.iter().find(|s| s.name == "Player").expect("Player object found");
        match &obj.kind {
            RookSymbolKind::Object { fields } => {
                assert_eq!(fields.len(), 2);
                assert_eq!(fields[0], ("id".to_string(), "int".to_string()));
            }
            _ => panic!("expected object"),
        }
        let greet = syms.iter().find(|s| s.name == "greet").expect("greet method found");
        match &greet.kind {
            RookSymbolKind::Method { obj, .. } => assert_eq!(obj, "Player"),
            _ => panic!("expected method"),
        }
        let add = syms.iter().find(|s| s.name == "add").expect("add function found");
        match &add.kind {
            RookSymbolKind::Function { .. } => {},
            _ => panic!("expected function"),
        }
        let total = syms.iter().find(|s| s.name == "total").expect("total let found");
        match &total.kind {
            RookSymbolKind::Variable { .. } => {},
            _ => panic!("expected variable"),
        }
    }

    #[test]
    fn test_scan_c_header() {
        let header = r#"
        #define PI 3.14159f
        #define LIGHTGRAY (Color){ 200, 200, 200, 255 }
        struct Vector2 { float x; float y; };
        void InitWindow(int width, int height, const char *title);
        void CloseWindow(void);
        "#;
        let tmp = std::env::temp_dir().join("test_raylib.h");
        fs::write(&tmp, header).unwrap();
        let syms = scan_c_header(&tmp);
        let _ = fs::remove_file(&tmp);

        assert!(syms.iter().any(|s| s.name == "PI"));
        assert!(syms.iter().any(|s| s.name == "LIGHTGRAY"));
        assert!(syms.iter().any(|s| s.name == "Vector2"));
        assert!(syms.iter().any(|s| s.name == "InitWindow"));
        assert!(syms.iter().any(|s| s.name == "CloseWindow"));
    }

    #[test]
    fn test_get_completions_includes_all_layers() {
        let mut state = ServerState {
            docs: HashMap::new(),
            cfuncs: parse_commandlist(COMMANDLIST_JSON),
            rokade: "rokade".to_string(),
            c_header_cache: HashMap::new(),
            rook_module_cache: HashMap::new(),
            std_dir: find_std_dir(),
        };

        let code = r#"
        #comprise <std/io>
        #include <stdio.h>

        object Enemy { hp: int }

        int main() {
            let e = Enemy { hp: 100 };
            return 0;
        }
        "#;

        let items = get_completions(&mut state, "file:///tmp/main.rook", code);
        let labels: Vec<String> = items.into_iter().map(|i| i.label).collect();

        // 1. Keywords & snippets
        assert!(labels.contains(&"object".to_string()));
        assert!(labels.contains(&"impl".to_string()));
        assert!(labels.contains(&"defer".to_string()));
        assert!(labels.contains(&"match".to_string()));
        assert!(labels.contains(&"let".to_string()));

        // 2. Buffer symbols
        assert!(labels.contains(&"Enemy".to_string()));
        assert!(labels.contains(&"e".to_string()));

        // 3. Comprised symbols (<std/io>)
        assert!(labels.contains(&"println".to_string()), "println from std/io should be available");
        assert!(labels.contains(&"print".to_string()), "print from std/io should be available");

        // 4. C Libc symbols
        assert!(labels.contains(&"printf".to_string()));
    }

    #[test]
    fn test_hover_documentation() {
        let mut state = ServerState {
            docs: HashMap::new(),
            cfuncs: parse_commandlist(COMMANDLIST_JSON),
            rokade: "rokade".to_string(),
            c_header_cache: HashMap::new(),
            rook_module_cache: HashMap::new(),
            std_dir: find_std_dir(),
        };

        let code = "int main() {\n    defer 1;\n    return 0;\n}\n";

        let params = HoverParams {
            text_document_position_params: lsp_types::TextDocumentPositionParams {
                text_document: lsp_types::TextDocumentIdentifier { uri: "file:///tmp/main.rook".parse().unwrap() },
                position: Position { line: 1, character: 6 }, // on "defer"
            },
            work_done_progress_params: Default::default(),
        };

        let h = do_hover(&mut state, &params, Some(code), "file:///tmp/main.rook");
        assert!(h.is_some(), "expected hover on defer keyword");
        let content = match h.unwrap().contents {
            lsp_types::HoverContents::Markup(m) => m.value,
            _ => panic!("expected markup"),
        };
        assert!(content.contains("defer"), "hover content should explain defer");
    }

    #[test]
    fn test_document_links() {
        let mut state = ServerState {
            docs: HashMap::new(),
            cfuncs: parse_commandlist(COMMANDLIST_JSON),
            rokade: "rokade".to_string(),
            c_header_cache: HashMap::new(),
            rook_module_cache: HashMap::new(),
            std_dir: find_std_dir(),
        };

        let code = "#include <stdio.h>\n#comprise <std/io>\n";
        let links = get_document_links(&mut state, "file:///tmp/main.rook", code);
        assert_eq!(links.len(), 2, "expected 2 links for include and comprise");
        let stdio_link = &links[0];
        assert_eq!(stdio_link.range.start.line, 0);
        assert!(stdio_link.target.as_ref().unwrap().to_string().contains("stdio.h"));

        let io_link = &links[1];
        assert_eq!(io_link.range.start.line, 1);
        assert!(io_link.target.as_ref().unwrap().to_string().contains("io.rook"));
    }

    #[test]
    fn test_find_definition_for_includes_and_comprises() {
        let mut state = ServerState {
            docs: HashMap::new(),
            cfuncs: parse_commandlist(COMMANDLIST_JSON),
            rokade: "rokade".to_string(),
            c_header_cache: HashMap::new(),
            rook_module_cache: HashMap::new(),
            std_dir: find_std_dir(),
        };

        let code = "#include <stdio.h>\n#comprise <std/io>\n";

        // Definition on line 0 (#include <stdio.h>)
        let loc1 = find_definition(&mut state, code, "file:///tmp/main.rook", Position { line: 0, character: 10 });
        assert!(loc1.is_some(), "expected definition for #include <stdio.h>");
        assert!(loc1.unwrap().uri.to_string().contains("stdio.h"));

        // Definition on line 1 (#comprise <std/io>)
        let loc2 = find_definition(&mut state, code, "file:///tmp/main.rook", Position { line: 1, character: 11 });
        assert!(loc2.is_some(), "expected definition for #comprise <std/io>");
        assert!(loc2.unwrap().uri.to_string().contains("std/io.rook"));
    }
}
