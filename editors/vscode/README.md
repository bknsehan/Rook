# Rook Language Support for VS Code

Official Visual Studio Code / VSCodium / Code - OSS extension for the **Rook** programming language.

## Features
- **Syntax Highlighting**: Full TextMate grammar for Rook language constructs, sum types, directives, and types.
- **Language Server Integration**: Integrates directly with `rook-lsp` over stdio.
- **Go to Definition**: Jump to local definitions, standard library modules (`#comprise <std/...>`), and C headers (`#include <...>`).
- **Autocompletion & Snippets**: Intelligent keyword suggestions, smart snippets (`main`, `func`, `struct`, `sum`, `impl`, `if`, `while`, `for`, `match`), and local variable scope awareness.
- **Hover Information**: Documentation and signatures on hover.
- **Diagnostics**: Real-time syntax and semantic validation.
- **Formatting**: Document formatting.

## Configuration
- `rook.lsp.path`: Custom path to the `rook-lsp` binary (defaults to `rook-lsp` in PATH or standard Rook installation directory).
