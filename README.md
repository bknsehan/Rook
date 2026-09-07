# Rook (`rokade`)

Rook is an explicit systems programming language featuring direct C ABI compatibility, zero runtime overhead, and a multi-backend compiler architecture emitting standard C (C11/C23) and native LLVM IR. It eliminates the manual header synchronization, indeterminate stack memory, and silent conditional assignment bugs of C while preserving direct hardware mapping, manual memory control, and zero-cost abstraction.

---

## Table of Contents

- [1. Architecture & Design Invariants](#1-architecture--design-invariants)
- [2. Compiler Pipeline & Multi-Backend Architecture](#2-compiler-pipeline--multi-backend-architecture)
- [3. Technical Comparison: C, Rust, Zig, and Rook](#3-technical-comparison-c-rust-zig-and-rook)
- [4. Safety Guarantees & Compiler Enforcements](#4-safety-guarantees--compiler-enforcements)
- [5. Language Mechanics & Memory Layouts](#5-language-mechanics--memory-layouts)
  - [5.1 Variables, Type Inference & Zero-Initialization](#51-variables-type-inference--zero-initialization)
  - [5.2 Single Inheritance with Prefix Subtyping](#52-single-inheritance-with-prefix-subtyping)
  - [5.3 Sum Types & Pattern Matching](#53-sum-types--pattern-matching)
  - [5.4 Deterministic Scoped Cleanup (`defer`)](#54-deterministic-scoped-cleanup-defer)
  - [5.5 Error Propagation (`?` Operator)](#55-error-propagation--operator)
- [6. Direct C Interoperability & Libclang Integration](#6-direct-c-interoperability--libclang-integration)
- [7. Toolchain Installation & Verification](#7-toolchain-installation--verification)
  - [7.1 Build Prerequisites](#71-build-prerequisites)
  - [7.2 Building and Installing](#72-building-and-installing)
  - [7.3 Environment Health Check (`rokade doctor`)](#73-environment-health-check-rokade-doctor)
  - [7.4 Cross-Compilation](#74-cross-compilation)
- [8. Project System & Configuration (`rokade.toml`)](#8-project-system--configuration-rokadetoml)
  - [8.1 Manifest Schema](#81-manifest-schema)
  - [8.2 CLI Command Reference](#82-cli-command-reference)
- [9. Standard Library Catalog (`std/`)](#9-standard-library-catalog-std)
- [10. Tooling & Ecosystem](#10-tooling--ecosystem)
  - [10.1 Language Server Protocol (`rook-lsp`)](#101-language-server-protocol-rook-lsp)
  - [10.2 Zed Editor Extension](#102-zed-editor-extension)
  - [10.3 Comprehensive Language Guides](#103-comprehensive-language-guides)
- [11. License](#11-license)

---

## 1. Architecture & Design Invariants

Rook adheres to five foundational engineering invariants:

1. **1:1 C ABI Alignment:** Function calling conventions, primitive bit-widths, alignment constraints, and struct layouts map directly to host C ABI specifications (System V AMD64, Microsoft x64, ARM AAPCS64). Rook can link with any compiled C library (`.a`, `.so`, `.dll`) without FFI wrappers or marshalling overhead.
2. **Dynamic Header Ingestion:** Declarations and implementations exist in unified `.rook` files. External C libraries are ingested directly from system C headers via libclang at compile time without requiring manual bindings or header translation steps.
3. **Zero Runtime Overhead:** The language runtime contains no garbage collector, no asynchronous thread scheduler, no reference-counting machinery, and no implicit memory allocations. Memory management is explicit via stack allocation, libc allocators (`malloc`/`free`), or custom memory arenas.
4. **Static Single Inheritance via Prefix Subtyping:** Object inheritance embeds the parent struct at byte offset 0. Casting a child pointer to a parent pointer is a zero-offset, zero-cost pointer cast with no virtual method tables (vtables) or dynamic dispatch indirection.
5. **Deterministic Stack Zero-Initialization:** Stack-allocated local variables and aggregate structures are guaranteed by the compiler to be initialized to zero unless an explicit initialization expression is provided.

---

## 2. Compiler Pipeline & Compiler Backends

The `rokade` compiler converts Rook source code through a modular semantic pipeline into two production compiler backends selectable via `--backend=<target>` (or via `backend = "..."` in `rokade.toml`):

```
Source (.rook) ➔ Lexer / Parser (AST) ➔ Sema & Libclang AST Engine
                                                  │
                ┌─────────────────────────────────┴─────────────────────────────────┐
                ▼                                                                   ▼
         C Backend Target                                                  LLVM Backend Target
         (--backend=c)                                                     (--backend=llvm)
                │                                                                   │
                ▼                                                                   ▼
         C11/C23 Source                                                    Typed LLVM IR (.ll) / Object (.o)
                │                                                                   │
                ▼                                                                   ▼
      Host Compiler (GCC/Clang)                                            LLVM TargetMachine / Clang Linker / JIT
                │                                                                   │
                └─────────────────────────────────┬─────────────────────────────────┘
                                                  ▼
                                       Native Binary Executable
```

### Backend Comparison

| Dimension | C Backend (`--backend=c`) | LLVM Backend (`--backend=llvm`) |
| :--- | :--- | :--- |
| **Implementation** | `src/c_backend.c` | `src/llvm_backend.c` *(formerly llvm2)* |
| **Intermediate Output** | ISO C11 / C23 Source | Formatted Typed LLVM IR (`.ll`) |
| **Final Target** | Native executable via host CC (GCC/Clang) | Native object file (`.o`) or In-memory JIT execution |
| **Toolchain Dependency**| GCC or Clang on PATH | LLVM 15+ development libraries |
| **JIT Execution** | No (compiles native binary to execute) | **Yes** (`rokade run --jit`) |
| **Short-Circuit Lowering**| Emits native C `&&` / `||` | **Conditional Basic Blocks + PHI Nodes** |
| **Union & Typedef Lowering**| Native C struct / typedef emission | **Recursive unwrapping + Backing Buffers** |
| **Primary Use Cases** | Maximum portability, zero LLVM dependency, GDB debugging | Direct native compilation, fast JIT testing, optimization passes |

*(Note: `--backend=llvm2` and `--emit-llvm2` are fully supported as transparent backwards-compatible aliases for `--backend=llvm`).*

---

## 3. Technical Comparison: C, Rust, Zig, and Rook

| Feature / Dimension | Standard C (C11/C23) | Rust (2024 Edition) | Zig (0.13+) | Rook (v0.6.0) |
| :--- | :--- | :--- | :--- | :--- |
| **Memory Management** | Manual (`malloc`/`free`), uninitialized stack by default. | Affine type system, compile-time borrow checker, static lifetimes. | Explicit allocators, manual management, no hidden control flow. | Manual explicit allocators, deterministic stack zero-initialization, optional bounds checks (`-b`). |
| **C ABI Compatibility** | Native (is C). | Requires `extern "C"` declarations and external binding tools (`bindgen`). | Requires `@cImport` translation step. | Direct 1:1 ABI mapping; dynamic in-memory libclang C header parsing without wrappers. |
| **Object Polymorphism** | Manual `void*` casting or custom function pointer structs. | Traits, dynamic trait objects (vtables), parametric monomorphization. | Compile-time duck typing (`comptime`). | Single inheritance with prefix subtyping (zero-offset casting), static `impl` methods. |
| **Algebraic Data Types** | Manual tagged unions (`struct` + `enum` + `union`). | First-class `enum` with payload pattern matching. | Tagged `union(enum)` with `switch` statements. | First-class `sum` and `enum` types with compile-time exhaustive `match`. |
| **Resource Cleanup** | Manual cleanup paths, non-standard cleanup attributes (`__attribute__((cleanup))`). | RAII via destructor execution (`Drop` trait). | Scoped `defer` and `errdefer` expressions. | Scoped `defer` statements lowered to deterministic LIFO scope unwinding. |
| **Error Handling** | In-band sentinel values (`-1`, `NULL`), global `errno`. | Tagged `Result<T, E>` / `Option<T>` with early-return `?` operator. | Error sets, error unions (`!T`), `try` keyword. | Tagged `Result<T, E>` / `Option<T>` with early-return `?` operator. |
| **Compiler Toolchain** | Host compiler (GCC, Clang, MSVC). | `rustc` + LLVM toolchain (~1.5 GB installation). | Self-contained single binary with bundled Clang. | Lightweight C binary (~500 KB) with optional LLVM / libclang linking. |

---

## 4. Safety Guarantees & Compiler Enforcements

Rook statically rejects syntactic patterns that lead to undefined behavior or silent logic errors in C:

| Source Pattern | C Standard Behavior | Rook Compiler Enforcement |
| :--- | :--- | :--- |
| **Assignment in Conditions** | `if (x = 5)` assigns value and tests truthiness | **Compile Error:** Assignments (`=`, `+=`, `-=`, etc.) are syntactically prohibited inside conditional expressions. |
| **Uninitialized Stack Memory** | Reads indeterminate stack garbage (Undefined Behavior) | **Guaranteed Zero:** All stack variables declared without initializers are lowered to `= {0}`. |
| **Short-Circuit Evaluation** | Guaranteed by C ISO standard | **Guaranteed in all backends:** Lowered to short-circuiting control-flow basic blocks with PHI nodes in `llvm`. |
| **`void*` Pointer Arithmetic** | Prohibited by ISO C; permitted by non-standard extensions | **Compile Error:** Pointer arithmetic on `void*` is strictly rejected. Explicit cast to `char*` or `uint8_t*` required. |
| **Non-Additive Pointer Math** | Permitted via unchecked casting (`ptr * 2`, `ptr / 2`) | **Compile Error:** Multiplication, division, modulo, and bitwise operations on pointer types are rejected. |
| **Pointer Syntax Format** | Ambiguous: `int *p`, `int* p`, `*int p` | **Standardized:** Postfix `Type*` syntax required (e.g., `int* p`). Prefix `*Type` is rejected. |
| **Literal Division by Zero** | Triggers hardware exception or undefined behavior | **Compile Error:** Expressions dividing or moduloing by literal `0` fail compilation. |
| **Arbitrary `goto` Statements** | Permitted; bypasses variable initialization | **Banned:** `goto` statements are prohibited in Rook source files. |

---

## 5. Language Mechanics & Memory Layouts

### 5.1 Variables, Type Inference & Zero-Initialization

Variables are declared with explicit types or inferred using `let`. Local variables declared without an initializer are guaranteed zero-initialized:

```rook
#include <stdio.h>

int main() {
    int count = 10;
    let inferred_val = 250;     // Type inferred as int
    int* ptr = &count;          // Standard pointer syntax

    int uninitialized_var;      // Emitted as: int uninitialized_var = {0};
    printf("uninitialized_var: %d\n", uninitialized_var); // Prints 0
    return 0;
}
```

### 5.2 Single Inheritance with Prefix Subtyping

Single inheritance is supported for `object` types. The parent object's memory layout is embedded at byte offset 0 of the child object:

```rook
object Entity {
    id: int
    active: bool
}

object Player : Entity {
    health: int
    score: int
}

impl Player {
    void take_damage(Player* self, int amount) {
        self.health -= amount;
    }
}
```

#### Memory Layout (x86-64 / AAPCS64)

```
Byte Offset:  0      3  4     5        7  8       11 12      15
              ┌─────────┬─────┬──────────┬──────────┬──────────┐
Entity:       │ id(i32) │ act │ [pad 3B] │          │          │
              └─────────┴─────┴──────────┴──────────┴──────────┘
Player:       │       Entity (8B)        │ hth(i32) │ scr(i32) │
              └──────────────────────────┴──────────┴──────────┘
```

Because `Entity` begins at byte offset 0 of `Player`:
- Upcasting `(Entity*)&player` is a zero-offset operation.
- No virtual method table (vtable) pointer is stored.
- Memory layout is 100% compatible with C struct nesting: `struct Player { struct Entity base; int health; int score; };`.

### 5.3 Sum Types & Pattern Matching

Sum types represent tagged unions where variants may hold distinct payload fields. The compiler enforces exhaustive matching at compile time:

```rook
sum Shape {
    Circle { radius: float },
    Rectangle { width: float, height: float },
    Point
}

float compute_area(Shape s) {
    match (s) {
        Circle(c) => return 3.14159 * c.radius * c.radius;
        Rectangle(r) => return r.width * r.height;
        Point => return 0.0;
    }
}
```

#### Memory Layout

A 4-byte discriminator integer (`tag`), followed by struct alignment padding, followed by a union buffer sized to `max(sizeof(variant))`:

```
Byte Offset:  0          3  4        7  8                                15
              ┌────────────┬───────────┬──────────────────────────────────┐
Shape:        │  tag(i32)  │  padding  │ variant payload (max 8 bytes)    │
              └────────────┴───────────┴──────────────────────────────────┘
```

### 5.4 Deterministic Scoped Cleanup (`defer`)

The `defer` statement schedules cleanup expressions to execute when the enclosing lexical scope exits. Handlers execute in reverse declaration order (LIFO):

```rook
#include <stdio.h>
#include <stdlib.h>

int process_dataset(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return -1;
    defer fclose(file); // Guaranteed to execute on function exit

    char* buffer = (char*)malloc(4096);
    if (!buffer) return -2;
    defer free(buffer); // Executes BEFORE fclose(file)

    if (fread(buffer, 1, 4096, file) <= 0) {
        return -3; // Both free(buffer) and fclose(file) execute
    }

    return 0;      // Both free(buffer) and fclose(file) execute
}
```

### 5.5 Error Propagation (`?` Operator)

Rook standard library types `Result<T, E>` and `Option<T>` integrate with the postfix `?` operator to short-circuit function execution on errors:

```rook
#comprise std/io

Result<int, IOError> load_configuration() {
    File file = File::open("config.bin", "rb")?; // Returns IOError immediately if open fails
    defer file.close();

    int magic = file.read_int()?;                // Returns IOError immediately if read fails
    return Result::Ok(magic);
}
```

---

## 6. Direct C Interoperability & Libclang Integration

Rook parses C headers directly via libclang during the semantic analysis pass:

1. **Header Parsing:** `#include <header.h>` instructs `src/c_import.c` to parse the header into an in-memory Clang Translation Unit.
2. **Type Extraction:** Struct definitions, function prototypes, typedefs, and union declarations are loaded directly into the Rook symbol table.
3. **Typedef Unwrapping:** The compiler automatically resolves transitive C typedefs (e.g., `gchar*` ➔ `char*`, `uint32_t` ➔ `unsigned int`) via `ck_unwrap_typedef`.
4. **Direct Callbacks:** Rook functions matching standard C function signatures can be passed directly as function pointers without trampoline code:

```rook
#include <stdio.h>
#include <Elementary.h>

// Direct C callback matching: void (*Evas_Smart_Cb)(void*, Evas_Object*, void*)
void on_button_clicked(void* data, Evas_Object* obj, void* event_info) {
    printf("Button clicked via C callback\n");
}

int main() {
    elm_init(0, NULL);
    Evas_Object* win = elm_win_util_standard_add("main", "Rook App");
    Evas_Object* btn = elm_button_add(win);
    evas_object_smart_callback_add(btn, "clicked", on_button_clicked, NULL);
    evas_object_show(win);
    elm_run();
    elm_shutdown();
    return 0;
}
```

---

## 7. Toolchain Installation & Verification

### 7.1 Build Prerequisites

- **C Compiler:** GCC 11+ or Clang 14+
- **Build Utilities:** CMake 3.16+ and Ninja or Make
- **Optional LLVM Suite:** LLVM 15+ and libclang development headers (required for `--backend=llvm`, and dynamic C header parsing)
- **Optional LSP Build:** Rust / Cargo (required to compile `rook-lsp`)

### 7.2 Building and Installing

```bash
# Clone the repository
git clone https://github.com/bknsehan/Rook.git
cd Rook

# Run the automated installer (builds compiler, stdlib, data, and LSP)
./install.sh

# Or build manually using CMake
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

By default, `./install.sh` installs the toolchain to `$HOME/bin/Rook` and creates symlinks in `$HOME/bin`:
- Compiler: `$HOME/bin/rokade`
- Language Server: `$HOME/bin/rook-lsp`
- Standard Library: `$HOME/bin/Rook/std`

### 7.3 Environment Health Check (`rokade doctor`)

Run `rokade doctor` to verify host compiler versions, LLVM JIT status, libclang AST availability, test corpus integrity, and cross-compilers:

```bash
$ rokade doctor
rokade doctor — environment health check
=========================================
[PASS] toolchain: /usr/bin/gcc (gcc) — gcc (GCC) 16.2.1 20260810
[PASS] backend: c (C23 / C11)
[PASS] backend: llvm (22.1.8) [JIT verified]
[PASS] c-interop: libclang (dynamic C header AST)
[PASS] corpus: 44 pass, 0 skip
[PASS] android NDK: /home/bknsehan/android-sdk/ndk/27.0.12077973
[PASS] android clang: /home/bknsehan/android-sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang
[PASS] windows cross-compiler: /usr/bin/x86_64-w64-mingw32-gcc
[PASS] cmake: found on PATH
[PASS] ninja: found on PATH
=========================================
doctor: PASS
```

### 7.4 Cross-Compilation

Target foreign platforms directly using the `--target` flag:

```bash
# Cross-compile for Windows x64 (generates .exe via MinGW-w64)
rokade build --target=windows

# Cross-compile for Android ARM64 (generates aarch64 binary via Android NDK)
rokade build --target=android
```

---

## 8. Project System & Configuration (`rokade.toml`)

### 8.1 Manifest Schema

Projects are configured using a `rokade.toml` file placed in the project root:

```toml
[project]
name = "engine_demo"
version = "0.1.0"
backend = "llvm"                   # "c", "llvm", or "llvm2"
pkg-config = ["raylib", "sqlite3"]  # Automated pkg-config link flags
cflags = ["-O3"]
ldflags = ["-lm"]
```

### 8.2 CLI Command Reference

```bash
# Create a new project structure
rokade new my_project

# Compile the active project
rokade build [path] [--backend=c|llvm]

# Compile and execute immediately
rokade run [path] [--backend=c|llvm]

# Execute using LLVM in-memory JIT (requires --backend=llvm)
rokade run --jit src/main.rook

# Output transpiled C source to stdout
rokade --emit-c src/main.rook

# Output typed LLVM IR to stdout
rokade --emit-llvm src/main.rook

# Format Rook source files
rokade fmt src/main.rook

# Output AST diagnostics in JSON format for editor tools
rokade --diagnostics src/main.rook

# Inspect or configure active toolchain compilers
rokade toolchain
rokade toolchain set cc /usr/bin/clang
```

---

## 9. Standard Library Catalog (`std/`)

The Rook standard library is located in `std/` and provides core capabilities:

| Module | Directive | Description |
| :--- | :--- | :--- |
| **`std/io`** | `#comprise std/io` | Stream I/O abstractions, file descriptor operations, line-by-line reading, and binary read/write primitives. |
| **`std/math`** | `#comprise std/math` | Numerical utilities: `min`, `max`, `clamp`, `abs`, integer power, floating-point comparisons, and constants. |
| **`std/json`** | `#comprise std/json` | Streaming recursive-descent JSON parser and serializer with zero external dependencies. |
| **`std/log`** | `#comprise std/log` | Leveled structured logging (`DEBUG`, `INFO`, `WARN`, `ERROR`) with ISO-8601 timestamps and terminal styling. |
| **`std/test`** | `#comprise std/test` | Lightweight test runner and assertions (`assert_eq`, `assert_true`, `assert_null`). |

---

## 10. Tooling & Ecosystem

### 10.1 Language Server Protocol (`rook-lsp`)

The Rook Language Server is written in Rust and integrates with any LSP-compliant editor:
- **Diagnostics:** Compile-time syntax and semantic errors reported via `rokade --diagnostics`.
- **Definition Navigation:** Jump to symbol definition (`--def-at <file> <line> <col>`).
- **Symbol Outlines:** Document symbol tree inspection (`--symbols <file>`).
- **Formatting:** Document formatting on save via `rokade fmt`.

### 10.2 Zed Editor Extension

The automated installer detects and installs the Rook Zed extension into:
`~/.local/share/zed/extensions/installed/rook`

Provides syntax highlighting, indentation rules, and direct LSP server connection.

### 10.3 Comprehensive Language Guides

For comprehensive tutorials, language mechanics, and Diátaxis documentation:
- **Interactive Guide (HTML):** [docs/rook-language-guide.html](file:///home/bknsehan/Projects/Rook/docs/rook-language-guide.html) (Searchable single-page guide with code copy features).
- **Technical Reference (Markdown):** [docs/rook-language-guide.md](file:///home/bknsehan/Projects/Rook/docs/rook-language-guide.md) (Self-contained technical reference and beginner tutorial).

---

## 11. License

Rook and the Rokade compiler toolchain are licensed under the [MIT License](LICENSE).
