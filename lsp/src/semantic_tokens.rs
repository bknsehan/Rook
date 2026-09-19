//! `semantic_tokens.rs` — In-memory high-fidelity semantic tokens generator for Rook.
//!
//! Provides pixel-perfect syntax and semantic highlighting for modern LSP clients
//! (Zed, VS Code, Neovim, Helix) conforming to LSP 3.16+ specifications.

use lsp_types::{
    SemanticToken, SemanticTokenModifier, SemanticTokenType, SemanticTokens,
    SemanticTokensFullOptions, SemanticTokensLegend, SemanticTokensOptions,
    SemanticTokensServerCapabilities,
};
use std::collections::HashSet;

pub const TOKEN_TYPES: &[SemanticTokenType] = &[
    SemanticTokenType::KEYWORD,      // 0
    SemanticTokenType::TYPE,         // 1
    SemanticTokenType::STRUCT,       // 2
    SemanticTokenType::ENUM,         // 3
    SemanticTokenType::ENUM_MEMBER,  // 4
    SemanticTokenType::FUNCTION,     // 5
    SemanticTokenType::METHOD,       // 6
    SemanticTokenType::PARAMETER,    // 7
    SemanticTokenType::VARIABLE,     // 8
    SemanticTokenType::PROPERTY,     // 9
    SemanticTokenType::MACRO,        // 10
    SemanticTokenType::STRING,       // 11
    SemanticTokenType::NUMBER,       // 12
    SemanticTokenType::OPERATOR,     // 13
    SemanticTokenType::COMMENT,      // 14
];

pub const TOKEN_MODIFIERS: &[SemanticTokenModifier] = &[
    SemanticTokenModifier::DECLARATION,      // bit 0 (1)
    SemanticTokenModifier::DEFINITION,       // bit 1 (2)
    SemanticTokenModifier::READONLY,         // bit 2 (4)
    SemanticTokenModifier::DEFAULT_LIBRARY,  // bit 3 (8)
];

const MOD_DECLARATION: u32 = 1 << 0;
const MOD_DEFINITION: u32 = 1 << 1;
const MOD_READONLY: u32 = 1 << 2;
const MOD_DEFAULT_LIB: u32 = 1 << 3;

pub fn semantic_tokens_legend() -> SemanticTokensLegend {
    SemanticTokensLegend {
        token_types: TOKEN_TYPES.to_vec(),
        token_modifiers: TOKEN_MODIFIERS.to_vec(),
    }
}

pub fn semantic_tokens_capabilities() -> SemanticTokensServerCapabilities {
    SemanticTokensServerCapabilities::SemanticTokensOptions(SemanticTokensOptions {
        work_done_progress_options: Default::default(),
        legend: semantic_tokens_legend(),
        range: Some(false),
        full: Some(SemanticTokensFullOptions::Bool(true)),
    })
}

#[derive(Debug, Clone)]
struct RawToken {
    line: u32,
    col: u32,
    length: u32,
    token_type: u32,
    modifiers: u32,
}

pub fn compute_semantic_tokens(source: &str) -> SemanticTokens {
    // Pass 1: Collect user-defined structs, sums, and enums so they are highlighted as types everywhere
    let mut known_types = HashSet::new();
    known_types.insert("Str".to_string());
    known_types.insert("Vec".to_string());
    known_types.insert("Result".to_string());
    known_types.insert("Option".to_string());
    known_types.insert("StringBuilder".to_string());
    known_types.insert("ElementPool".to_string());
    known_types.insert("TestSuite".to_string());
    known_types.insert("AtomicBool".to_string());
    known_types.insert("AtomicPtr".to_string());
    known_types.insert("AtomicInt".to_string());
    known_types.insert("Mutex".to_string());
    known_types.insert("CondVar".to_string());
    known_types.insert("Thread".to_string());

    for line in source.lines() {
        let trimmed = line.trim();
        if let Some(rest) = trimmed.strip_prefix("struct ") {
            let name = rest.split(['{', ' ', ':', ';']).next().unwrap_or("").trim();
            if !name.is_empty() { known_types.insert(name.to_string()); }
        } else if let Some(rest) = trimmed.strip_prefix("sum ") {
            let name = rest.split(['{', ' ', ':', ';']).next().unwrap_or("").trim();
            if !name.is_empty() { known_types.insert(name.to_string()); }
        } else if let Some(rest) = trimmed.strip_prefix("enum ") {
            let name = rest.split(['{', ' ', ':', ';']).next().unwrap_or("").trim();
            if !name.is_empty() { known_types.insert(name.to_string()); }
        } else if let Some(rest) = trimmed.strip_prefix("typedef ") {
            let name = rest.split(['{', ' ', ';']).last().unwrap_or("").trim();
            if !name.is_empty() { known_types.insert(name.to_string()); }
        }
    }

    // Pass 2: Tokenize source code line-by-line with full syntactic context
    let mut raw_tokens = Vec::new();
    let mut in_block_comment = false;
    let mut current_impl: Option<String> = None;
    let mut brace_depth = 0;
    let mut in_struct = false;

    for (line_idx, line) in source.lines().enumerate() {
        let line_u32 = line_idx as u32;
        let bytes = line.as_bytes();
        let len = bytes.len();
        let mut i = 0;
        let mut prev_token_word = String::new();

        while i < len {
            // Check block comment continuation
            if in_block_comment {
                let start_col = i as u32;
                if let Some(end_idx) = line[i..].find("*/") {
                    let end_pos = i + end_idx + 2;
                    raw_tokens.push(RawToken {
                        line: line_u32,
                        col: start_col,
                        length: (end_pos - i) as u32,
                        token_type: 14, // COMMENT
                        modifiers: 0,
                    });
                    in_block_comment = false;
                    i = end_pos;
                    continue;
                } else {
                    raw_tokens.push(RawToken {
                        line: line_u32,
                        col: start_col,
                        length: (len - i) as u32,
                        token_type: 14, // COMMENT
                        modifiers: 0,
                    });
                    break;
                }
            }

            let b = bytes[i];

            // Whitespace
            if b == b' ' || b == b'\t' || b == b'\r' {
                i += 1;
                continue;
            }

            // Track block braces
            if b == b'{' {
                brace_depth += 1;
                i += 1;
                continue;
            }
            if b == b'}' {
                if brace_depth > 0 { brace_depth -= 1; }
                if brace_depth == 0 {
                    current_impl = None;
                    in_struct = false;
                }
                i += 1;
                continue;
            }

            // Single line or block comment start
            if b == b'/' && i + 1 < len {
                if bytes[i + 1] == b'/' {
                    // Line comment to end of line
                    raw_tokens.push(RawToken {
                        line: line_u32,
                        col: i as u32,
                        length: (len - i) as u32,
                        token_type: 14, // COMMENT
                        modifiers: 0,
                    });
                    break;
                } else if bytes[i + 1] == b'*' {
                    // Block comment start
                    let start_col = i as u32;
                    if let Some(end_idx) = line[i + 2..].find("*/") {
                        let end_pos = i + 2 + end_idx + 2;
                        raw_tokens.push(RawToken {
                            line: line_u32,
                            col: start_col,
                            length: (end_pos - i) as u32,
                            token_type: 14, // COMMENT
                            modifiers: 0,
                        });
                        i = end_pos;
                        continue;
                    } else {
                        raw_tokens.push(RawToken {
                            line: line_u32,
                            col: start_col,
                            length: (len - i) as u32,
                            token_type: 14, // COMMENT
                            modifiers: 0,
                        });
                        in_block_comment = true;
                        break;
                    }
                }
            }

            // Preprocessor directive: `#include`, `#comprise`, `#define`, etc.
            if b == b'#' {
                let start_col = i as u32;
                let mut end = i + 1;
                while end < len && (bytes[end].is_ascii_alphanumeric() || bytes[end] == b'_') {
                    end += 1;
                }
                let directive = &line[i..end];
                raw_tokens.push(RawToken {
                    line: line_u32,
                    col: start_col,
                    length: (end - i) as u32,
                    token_type: 10, // MACRO
                    modifiers: 0,
                });
                i = end;

                // After #include or #comprise, scan the header/module path
                if directive == "#include" || directive == "#comprise" {
                    while i < len && (bytes[i] == b' ' || bytes[i] == b'\t') {
                        i += 1;
                    }
                    if i < len && (bytes[i] == b'<' || bytes[i] == b'"') {
                        let close = if bytes[i] == b'<' { b'>' } else { b'"' };
                        let path_start = i as u32;
                        let mut p_end = i + 1;
                        while p_end < len && bytes[p_end] != close {
                            p_end += 1;
                        }
                        if p_end < len && bytes[p_end] == close {
                            p_end += 1;
                        }
                        raw_tokens.push(RawToken {
                            line: line_u32,
                            col: path_start,
                            length: (p_end - i) as u32,
                            token_type: 11, // STRING
                            modifiers: 0,
                        });
                        i = p_end;
                    }
                }
                continue;
            }

            // String literal: `"..."`
            if b == b'"' {
                let start_col = i as u32;
                let mut end = i + 1;
                while end < len {
                    if bytes[end] == b'\\' {
                        end += 2; // skip escaped character
                    } else if bytes[end] == b'"' {
                        end += 1;
                        break;
                    } else {
                        end += 1;
                    }
                }
                raw_tokens.push(RawToken {
                    line: line_u32,
                    col: start_col,
                    length: (end - i) as u32,
                    token_type: 11, // STRING
                    modifiers: 0,
                });
                i = end;
                prev_token_word.clear();
                continue;
            }

            // Character literal: `'c'`
            if b == b'\'' {
                let start_col = i as u32;
                let mut end = i + 1;
                while end < len {
                    if bytes[end] == b'\\' {
                        end += 2;
                    } else if bytes[end] == b'\'' {
                        end += 1;
                        break;
                    } else {
                        end += 1;
                    }
                }
                raw_tokens.push(RawToken {
                    line: line_u32,
                    col: start_col,
                    length: (end - i) as u32,
                    token_type: 11, // STRING
                    modifiers: 0,
                });
                i = end;
                prev_token_word.clear();
                continue;
            }

            // Numbers: Hex `0x...`, Binary `0b...`, Decimal / Float
            if b.is_ascii_digit() || (b == b'.' && i + 1 < len && bytes[i + 1].is_ascii_digit()) {
                let start_col = i as u32;
                let mut end = i;
                if bytes[end] == b'0' && end + 1 < len && (bytes[end + 1] == b'x' || bytes[end + 1] == b'X') {
                    end += 2;
                    while end < len && (bytes[end].is_ascii_hexdigit() || bytes[end] == b'_') { end += 1; }
                } else if bytes[end] == b'0' && end + 1 < len && (bytes[end + 1] == b'b' || bytes[end + 1] == b'B') {
                    end += 2;
                    while end < len && (bytes[end] == b'0' || bytes[end] == b'1' || bytes[end] == b'_') { end += 1; }
                } else {
                    while end < len && (bytes[end].is_ascii_digit() || bytes[end] == b'_' || bytes[end] == b'.') {
                        end += 1;
                    }
                    if end < len && (bytes[end] == b'e' || bytes[end] == b'E') {
                        end += 1;
                        if end < len && (bytes[end] == b'+' || bytes[end] == b'-') { end += 1; }
                        while end < len && (bytes[end].is_ascii_digit() || bytes[end] == b'_') { end += 1; }
                    }
                }
                // Number suffix (e.g. `f`, `L`, `LL`, `u`, `ULL`)
                while end < len && (bytes[end].is_ascii_alphabetic() || bytes[end] == b'_') {
                    end += 1;
                }
                raw_tokens.push(RawToken {
                    line: line_u32,
                    col: start_col,
                    length: (end - i) as u32,
                    token_type: 12, // NUMBER
                    modifiers: 0,
                });
                i = end;
                prev_token_word.clear();
                continue;
            }

            // Identifiers and Keywords
            if b.is_ascii_alphabetic() || b == b'_' {
                let start_col = i as u32;
                let mut end = i;
                while end < len && (bytes[end].is_ascii_alphanumeric() || bytes[end] == b'_') {
                    end += 1;
                }
                let word = &line[i..end];

                // Check next non-whitespace char for contextual disambiguation
                let mut next_char = ' ';
                let mut j = end;
                while j < len {
                    if bytes[j] != b' ' && bytes[j] != b'\t' {
                        next_char = bytes[j] as char;
                        break;
                    }
                    j += 1;
                }

                // Disambiguate token type
                let (token_type, modifiers) = classify_identifier(
                    word,
                    &prev_token_word,
                    next_char,
                    current_impl.is_some(),
                    in_struct,
                    &known_types,
                );

                if word == "impl" {
                    // Start impl context: next word is struct name
                    // Lookahead to capture struct name
                    let rest = line[end..].trim();
                    let st_name = rest.split(['{', ' ']).next().unwrap_or("").trim();
                    if !st_name.is_empty() {
                        current_impl = Some(st_name.to_string());
                    }
                } else if word == "struct" {
                    in_struct = true;
                }

                raw_tokens.push(RawToken {
                    line: line_u32,
                    col: start_col,
                    length: (end - i) as u32,
                    token_type,
                    modifiers,
                });

                prev_token_word = word.to_string();
                i = end;
                continue;
            }

            // Two-character operators: `=>`, `::`, `->`, `==`, `!=`, `<=`, `>=`, `&&`, `||`, `+=`, `-=`, `*=`, `/=`
            if i + 1 < len {
                let op2 = &line[i..i + 2];
                if op2 == "=>" || op2 == "::" || op2 == "->" || op2 == "==" || op2 == "!="
                    || op2 == "<=" || op2 == ">=" || op2 == "&&" || op2 == "||"
                    || op2 == "+=" || op2 == "-=" || op2 == "*=" || op2 == "/="
                    || op2 == "%=" || op2 == "&=" || op2 == "|=" || op2 == "^="
                    || op2 == "<<" || op2 == ">>"
                {
                    raw_tokens.push(RawToken {
                        line: line_u32,
                        col: i as u32,
                        length: 2,
                        token_type: 13, // OPERATOR
                        modifiers: 0,
                    });
                    prev_token_word = op2.to_string();
                    i += 2;
                    continue;
                }
            }

            // Single-character operators & punctuation
            if b == b'.' || b == b'?' || b == b'+' || b == b'-' || b == b'*' || b == b'/'
                || b == b'%' || b == b'&' || b == b'|' || b == b'^' || b == b'!' || b == b'~'
                || b == b'<' || b == b'>' || b == b'=' || b == b':'
            {
                raw_tokens.push(RawToken {
                    line: line_u32,
                    col: i as u32,
                    length: 1,
                    token_type: 13, // OPERATOR
                    modifiers: 0,
                });
                prev_token_word = (b as char).to_string();
                i += 1;
                continue;
            }

            // Punctuation (braces, commas, semicolons): advance without token
            prev_token_word = (b as char).to_string();
            i += 1;
        }
    }

    raw_tokens_to_semantic_tokens(raw_tokens)
}

fn classify_identifier(
    word: &str,
    prev_word: &str,
    next_char: char,
    in_impl: bool,
    in_struct: bool,
    known_types: &HashSet<String>,
) -> (u32, u32) {
    // 1. Rook & C keywords
    match word {
        "impl" | "sum" | "match" | "defer" | "let" | "object" | "comprise"
        | "if" | "else" | "while" | "for" | "switch" | "case" | "default"
        | "break" | "continue" | "return" | "do" | "goto"
        | "struct" | "enum" | "union" | "typedef" | "extern" | "inline"
        | "const" | "static" | "sizeof" | "volatile" | "fn" | "auto" => {
            return (0, 0); // KEYWORD
        }
        _ => {}
    }

    // 2. Builtin primitive types
    match word {
        "int" | "float" | "double" | "char" | "void" | "bool" | "size_t" | "ssize_t" | "ptrdiff_t"
        | "int8_t" | "int16_t" | "int32_t" | "int64_t" | "uint8_t" | "uint16_t" | "uint32_t" | "uint64_t"
        | "u8" | "u16" | "u32" | "u64" | "i8" | "i16" | "i32" | "i64" | "f32" | "f64"
        | "uintptr_t" | "intptr_t" | "FILE" | "va_list" => {
            return (1, MOD_DEFAULT_LIB); // TYPE (defaultLibrary)
        }
        _ => {}
    }

    // 3. Builtin constants
    match word {
        "true" | "false" | "NULL" | "null" | "nullptr" | "EOF" | "ERANGE" | "CLOCK_MONOTONIC" => {
            return (4, MOD_READONLY | MOD_DEFAULT_LIB); // ENUM_MEMBER (readonly)
        }
        "self" => {
            return (7, MOD_READONLY); // PARAMETER
        }
        _ => {}
    }

    // 4. Definition contexts based on preceding token
    if prev_word == "struct" {
        return (2, MOD_DEFINITION | MOD_DECLARATION); // STRUCT
    }
    if prev_word == "sum" || prev_word == "enum" {
        return (3, MOD_DEFINITION | MOD_DECLARATION); // ENUM
    }
    if prev_word == "impl" {
        return (2, MOD_DECLARATION); // STRUCT
    }

    // 5. Member access / call: `foo.bar` or `foo->bar`
    if prev_word == "." || prev_word == "->" {
        if next_char == '(' {
            return (6, 0); // METHOD
        } else {
            return (9, 0); // PROPERTY
        }
    }

    // 6. Function or method calls: `ident(...)`
    if next_char == '(' {
        if in_impl {
            return (6, MOD_DEFINITION); // METHOD
        }
        if word.chars().next().map_or(false, |c| c.is_uppercase()) && !known_types.contains(word) {
            return (4, 0); // ENUM_MEMBER constructor (e.g. Circle(5))
        }
        return (5, 0); // FUNCTION
    }

    // 7. Field definitions in struct or struct initialization: `x: int` or `Point { x: 10 }`
    if next_char == ':' {
        return (9, if in_struct { MOD_DEFINITION } else { 0 }); // PROPERTY
    }

    // 8. Known types from comprises or codebase
    if known_types.contains(word) {
        return (1, 0); // TYPE
    }

    // 9. PascalCase convention: Types (structs, sums, enums)
    let first = word.chars().next().unwrap_or('_');
    if first.is_uppercase() {
        // Check if ALL_CAPS constant
        if word.chars().all(|c| c.is_uppercase() || c.is_ascii_digit() || c == '_') && word.len() > 1 {
            return (4, MOD_READONLY); // ENUM_MEMBER / CONSTANT
        }
        return (1, 0); // TYPE
    }

    // 10. Default fallback: Variable
    (8, 0) // VARIABLE
}

fn raw_tokens_to_semantic_tokens(mut tokens: Vec<RawToken>) -> SemanticTokens {
    // Sort by line, then column
    tokens.sort_by(|a, b| a.line.cmp(&b.line).then_with(|| a.col.cmp(&b.col)));

    let mut data = Vec::with_capacity(tokens.len());
    let mut prev_line = 0;
    let mut prev_col = 0;

    for t in tokens {
        let delta_line = t.line - prev_line;
        let delta_start = if delta_line == 0 {
            t.col.saturating_sub(prev_col)
        } else {
            t.col
        };

        data.push(SemanticToken {
            delta_line,
            delta_start,
            length: t.length,
            token_type: t.token_type,
            token_modifiers_bitset: t.modifiers,
        });

        prev_line = t.line;
        prev_col = t.col;
    }

    SemanticTokens {
        result_id: None,
        data,
    }
}
