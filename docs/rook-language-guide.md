# Rook Language & Architecture Guide (v0.6.2)

> **Specification & Reference Manual**  
> Technical documentation covering Rook language mechanics, memory layouts, C ABI compatibility, compiler architecture, and systems programming foundations.

---

## Table of Contents

- [1. Language Fundamentals & Design Constraints](#1-language-fundamentals--design-constraints)
- [2. Architecture & The Three Compiler Backends](#2-architecture--the-three-compiler-backends)
- [3. Technical Comparison: C, Rust, Zig, and Rook](#3-technical-comparison-c-rust-zig-and-rook)
- [4. Memory Layout & Storage Semantics](#4-memory-layout--storage-semantics)
- [5. Type System & Semantic Analysis](#5-type-system--semantic-analysis)
- [6. Control Flow & Evaluation Rules](#6-control-flow--evaluation-rules)
- [7. Algebraic Data Types: sum, enum & match](#7-algebraic-data-types-sum-enum--match)
- [8. Zero-Overhead C Interoperability & Libclang Integration](#8-zero-overhead-c-interoperability--libclang-integration)
- [9. Standard Library (std/) Architecture](#9-standard-library-std-architecture)
- [10. Configuration, Multi-Target Builds & Toolchains](#10-configuration-multi-target-builds--toolchains)

---

## 1. Language Fundamentals & Design Constraints

Rook is an explicit systems programming language designed to eliminate the structural maintenance burdens and common undefined behavior patterns of C while maintaining 100% ABI parity and zero runtime overhead. Rook is implemented in C11 and compiles directly to standard C (C11/C23) and native LLVM IR.

### 1.1 Design Problems in Standard C
In standard C (ISO/IEC 9899), systems developers encounter recurring structural and safety challenges inherent to the language grammar and compilation model:
- **Header Synchronization Overhead:** C separates declarations into `.h` header files and definitions into `.c` translation units. Mismatched prototypes or macro definitions between headers and source files lead to silent ABI corruption or linker errors.
- **Uninitialized Stack Variables:** In C, local variables without initializers retain indeterminate stack memory. Reading uninitialized stack values causes undefined behavior.
- **Accidental Assignment in Conditionals:** C treats assignment as an expression. Writing `if (x = 5)` instead of `if (x == 5)` compiles without warning, inverting program logic at runtime.
- **Boilerplate for Abstractions:** Implementing single inheritance or tagged unions requires nested structures, manual discriminator checks, and void pointer casting without compiler exhaustiveness enforcement.

### 1.2 Technical Invariants of Rook
- **Deterministic Zero-Initialization:** Every local variable, stack array, and struct allocated on the stack is guaranteed by the compiler to be initialized to zero unless explicitly initialized with an expression.
- **Single Source of Truth:** Source files declare and define items in a single `.rook` file. External C headers are parsed dynamically at compile time via libclang without manual bindings.
- **1:1 C ABI Compatibility:** Every struct, primitive type, and function signature matches the host C ABI (System V AMD64 ABI, Microsoft x64, ARM AAPCS64).
- **Hard Rejection of Assignment in Conditionals:** Assignment operators (`=`, `+=`, etc.) cannot be used as conditions. `if (x = 5)` triggers a compile-time error.
- **Prefix Subtyping Layout:** Inherited structs embed parent fields at offset 0, enabling zero-offset pointer upcasting without runtime cost.

### 1.3 Explicit Non-Goals
- **No Borrow Checker:** Memory management is explicit (stack allocations, `malloc`/`free`, custom arenas).
- **No Garbage Collector:** No runtime engine, reference counting headers, or stop-the-world pauses.
- **No Hidden Control Flow:** No operator overloading, copy constructors, or hidden memory allocations.
- **Static Dispatch Only:** Method calls (`impl`) resolve statically at compile time to mangled symbol names; there are no vtables unless explicitly declared by the user.

---

## 2. Architecture & Compiler Backends

The `rokade` compiler features a modular multi-backend pipeline:

```
Source (.rook) ➔ Lexer/Parser (AST) ➔ Sema & Libclang ➔ Codegen
                                                    ├── C Backend (C11/C23 Source)
                                                    └── LLVM Backend (Native Object / Direct IR / JIT)
```

| Backend | Implementation | Target Output | Characteristics |
| :--- | :--- | :--- | :--- |
| **C Backend (`--backend=c`)** | `src/c_backend.c` | C11 / C23 (`.c`) | Default portable backend. Transpiles AST directly to standard C source and drives host GCC or Clang. Fully compatible with GDB/LLDB and native C build systems. |
| **LLVM Backend (`--backend=llvm`)** | `src/llvm_backend.c` | Native Object (`.o`) / IR (`.ll`) / JIT | Production native LLVM backend. Direct typed LLVM IR codegen, conditional basic-block short-circuiting (`&&`, `||`) via PHI nodes, automated C typedef resolution, struct union backing buffers, in-memory JIT execution (`rokade run --jit`), and zero-wrapper foreign calls. (`--backend=llvm2` is supported as an alias). |

---

## 3. Technical Comparison: C, Rust, Zig, and Rook

| Feature / Dimension | Standard C (C11/C23) | Rust (2024 Edition) | Zig (0.13+) | Rook (v0.6.1) |
| :--- | :--- | :--- | :--- | :--- |
| **Memory Model** | Manual, uninitialized stack defaults, raw pointers. | Affine type system, borrow checker, compile-time lifetimes. | Manual with allocators, slices, no hidden control flow. | Manual, deterministic zero initialization, raw pointers, bounds checks. |
| **C ABI Compatibility** | Native (is C). | Requires `extern "C"` blocks and binding tools (`bindgen`). | Requires `@cImport` and translated C type headers. | Native 1:1 ABI mapping, dynamic libclang header parsing without wrappers. |
| **Polymorphism** | Manual `void*` casting or manual function pointer tables. | Traits, dynamic trait objects (vtables), generics. | Compile-time duck typing (`comptime`). | Single inheritance with prefix subtyping (zero-offset casting), static `impl` methods. |
| **Algebraic Data Types** | Manual tagged union (`struct` + `enum` + `union`). | First-class `enum` with pattern matching. | Tagged `union(enum)` with `switch`. | First-class `sum` and `enum` with exhaustive `match`. |
| **Resource Cleanup** | Manual `goto cleanup` or non-standard extensions. | RAII (`Drop` trait implementation). | `defer` and `errdefer` statements. | `defer` statement with deterministic LIFO block unwinding. |
| **Compiler & Dependencies** | Host compiler (GCC, Clang, MSVC). | `rustc` + LLVM (~1.5 GB installation). | Self-contained binary compiler with embedded Clang. | Single lightweight C binary (~500 KB) with optional LLVM/libclang linking. |

---

## 4. Memory Layout & Storage Semantics

### 4.1 Primitive Scalar Storage
| Rook Type | C Equivalent | LLVM IR Type | Size (Bytes) | Alignment (Bytes) |
| :--- | :--- | :--- | :--- | :--- |
| `bool` | `bool` / `_Bool` | `i8` / `i1` | 1 | 1 |
| `char` | `char` | `i8` | 1 | 1 |
| `int8_t` / `uint8_t` | `int8_t` / `uint8_t` | `i8` | 1 | 1 |
| `int16_t` / `uint16_t` | `int16_t` / `uint16_t` | `i16` | 2 | 2 |
| `int` / `int32_t` | `int` / `int32_t` | `i32` | 4 | 4 |
| `int64_t` / `uint64_t` | `int64_t` / `uint64_t` | `i64` | 8 | 8 |
| `float` | `float` | `float` | 4 | 4 |
| `double` | `double` | `double` | 8 | 8 |
| `T*` (Pointer) | `T*` | `ptr` | 8 (on 64-bit) | 8 (on 64-bit) |

### 4.2 Struct Memory Layout & Padding
Fields are allocated in declaration order. Natural alignment padding is inserted between fields:

```rook
struct Node {
    flag: bool     // 1 byte offset 0, followed by 3 bytes padding
    id: int        // 4 bytes offset 4
    ptr: void*     // 8 bytes offset 8
}
// Total size: 16 bytes, alignment: 8 bytes
```

### 4.3 Prefix Subtyping (Single Inheritance)
When an `object` inherits from a parent struct, the parent fields are placed at byte offset 0:

```rook
object Entity {
    id: int
    active: bool
}

object Player : Entity {
    health: int
    score: int
}
```

Memory representation:
- Offset 0..3: `id` (from `Entity`)
- Offset 4: `active` (from `Entity`)
- Offset 5..7: Alignment padding
- Offset 8..11: `health` (from `Player`)
- Offset 12..15: `score` (from `Player`)

Because `Entity` begins at byte offset 0, upcasting `(Entity*)&player` is a zero-cost operation requiring no pointer offset arithmetic.

---

## 5. Type System & Semantic Analysis

### 5.1 Variable Declarations
```rook
int count = 10;          // Explicit scalar
let score = 250;         // Inferred as int
let ptr = &count;        // Inferred as int*
int uninitialized_val;   // Guaranteed zero-initialized
```

### 5.2 Pointer Semantics
- `void*` is compatible with any pointer type, permitting standard memory allocators (`malloc`) without explicit casts.
- Pointer arithmetic on `void*` is rejected at compile time; pointers must be cast to `char*` or `uint8_t*` before applying byte offsets.
- Member access works via dot (`ptr.field`) or arrow (`ptr->field`).

### 5.3 Dynamic Typedef Unwrapping (`ck_unwrap_typedef`)
When C headers are imported via libclang, the semantic analyzer registers all typedef symbols. `ck_unwrap_typedef` recursively unwraps aliases:
```rook
#include <gtk/gtk.h>

// gtk_window_get_title returns 'const gchar*'
// In GLib: typedef char gchar;
// Unwrapped to 'char', matching 'const char*' exactly:
const char* title = gtk_window_get_title(win);
```

---

## 6. Control Flow & Evaluation Rules

### 6.1 Conditionals & Assignment Rejection
Assignment inside condition expressions is rejected at compile time:
```rook
int x = 0;
if (x == 5) { /* Valid */ }
// if (x = 5) { } // Hard compile-time error
```

### 6.2 Guaranteed Short-Circuit Evaluation
Logical AND (`&&`) and logical OR (`||`) guarantee short-circuit evaluation:
```rook
int* ptr = NULL;
if (ptr != NULL && *ptr == 10) {
    // Safe: '*ptr' is never evaluated when ptr is NULL
}
```

In the `llvm` backend, short-circuiting is lowered using basic blocks (`land.rhs`, `land.merge`) and PHI nodes:
```llvm
  %lhs_val = icmp ne ptr %ptr, null
  br i1 %lhs_val, label %land.rhs, label %land.merge

land.rhs:
  %deref = load i32, ptr %ptr, align 4
  %rhs_val = icmp eq i32 %deref, 10
  br label %land.merge

land.merge:
  %result = phi i1 [ false, %entry ], [ %rhs_val, %land.rhs ]
```

### 6.3 Deterministic Cleanup (`defer`)
The `defer` statement registers a cleanup statement or block executed upon scope exit in reverse order (LIFO):
```rook
FILE* f = fopen("data.bin", "rb");
if (!f) return -1;
defer fclose(f);

void* buf = malloc(1024);
defer free(buf); // Executes before fclose(f)

if (error_condition()) {
    return -2; // buf is freed, then f is closed
}
return 0;      // buf is freed, then f is closed
```

---

## 7. Algebraic Data Types: sum, enum & match

### 7.1 Sum Types (Tagged Unions)
```rook
sum Shape {
    Circle { radius: float },
    Rectangle { width: float, height: float },
    Point
}
```

Memory layout:
- `tag: int32_t` (0 = Circle, 1 = Rectangle, 2 = Point)
- Padding (4 bytes)
- `payload: union` sized to `max(sizeof(Circle), sizeof(Rectangle))` = 8 bytes

### 7.2 Exhaustive Pattern Matching
```rook
float get_area(Shape s) {
    match (s) {
        Circle(c) => return 3.14159 * c.radius * c.radius;
        Rectangle(r) => return r.width * r.height;
        Point => return 0.0;
    }
}
```

### 7.3 Result, Option, and the `?` Operator
```rook
#comprise std/io

Result<int, IOError> read_config() {
    File f = File::open("config.json", "r")?;
    defer f.close();
    int val = f.read_int()?;
    return Result::Ok(val);
}
```

---

## 8. Zero-Overhead C Interoperability & Libclang Integration

### 8.1 Ingestion via Libclang
When an `#include <header.h>` is encountered, `c_import.c` parses the header AST using libclang in memory, automatically extracting:
- Function prototypes and variadic flags.
- Struct/union definitions, field offsets, and backing buffer sizes.
- Typedef aliases and canonical types.

### 8.2 Function Pointer Callbacks
Rook functions can be passed directly as C callbacks:
```rook
#include <stdio.h>
#include <Elementary.h>

void on_click(void* data, Evas_Object* obj, void* event_info) {
    printf("Button clicked!\n");
}

int main() {
    elm_init(0, NULL);
    Evas_Object* win = elm_win_util_standard_add("main", "Demo");
    Evas_Object* btn = elm_button_add(win);
    evas_object_smart_callback_add(btn, "clicked", on_click, NULL);
    evas_object_show(win);
    elm_run();
    return 0;
}
```

### 8.3 Package Dependencies (`rokade.toml`)
Dependencies on system C libraries are declared via `pkg-config`:
```toml
[project]
name = "gui_app"
version = "0.1.0"
pkg-config = ["raylib", "elementary", "sqlite3"]
```

---

### 9. Standard Library (std/) Architecture

| Module | Include | Primary Capabilities |
| :--- | :--- | :--- |
| **`std/io`** | `#comprise <std/io>` | Buffered stream I/O, file descriptor abstraction, line-by-line reading, binary read/write. |
| **`std/str`** | `#comprise <std/str>` | Non-owning string slices (`Str`) with bounds checking, splitting, searching, and conversions. |
| **`std/mem`** | `#comprise <std/mem>` | High-performance memory management: bump allocators, scratchpads, and fixed-size element pools. |
| **`std/atomic`** | `#comprise <std/atomic>` | Lock-free atomic primitives (`AtomicInt`, `AtomicBool`, `AtomicPtr`) with CAS and exchange operations. |
| **`std/sync`** | `#comprise <std/sync>` | Concurrency synchronization: native OS threads (`Thread`), mutexes (`Mutex`), and condition variables (`CondVar`). |
| **`std/option`** | `#comprise <std/option>` | Ergonomic optional types (`Option`) for safe null-free value representation and unwrapping. |
| **`std/result`** | `#comprise <std/result>` | Explicit error handling types (`Result`) without exception overhead. |
| **`std/math`** | `#comprise <std/math>` | Arithmetic routines: `min`, `max`, `clamp`, `abs`, power routines, float comparisons. |
| **`std/json`** | `#comprise <std/json>` | Streaming recursive-descent JSON parser and serializer with zero external dependencies. |
| **`std/log`** | `#comprise <std/log>` | Structured leveled logging (`DEBUG`, `INFO`, `WARN`, `ERROR`) with ISO-8601 timestamps. |
| **`std/test`** | `#comprise <std/test>` | Assertion framework (`assert_eq`, `assert_true`), test harness, and failure reports. |

---

## 10. Configuration, Multi-Target Builds & Toolchains

### 10.1 CLI Reference
```bash
# Build and execute
rokade run [path] [--backend=c|llvm|llvm2]

# Build native binary
rokade build [path] [--backend=c|llvm|llvm2]

# Comprehensive environment and corpus health check
rokade doctor

# Query and set toolchain overrides
rokade toolchain
rokade toolchain set cc /usr/bin/clang
```

### 10.2 Cross-Compilation
- **Windows (x86_64-w64-mingw32):** Compiles via MinGW-w64 GCC toolchain.
- **Android (aarch64-linux-android):** Cross-compiles using the Android NDK Clang toolchain and target sysroot.

### 10.3 Language Server Protocol (`rook-lsp`)
The official language server provides editor integration for editors including Zed, VSCode, and Neovim. It provides syntax validation, semantic diagnostics via `rokade --diagnostics`, jump-to-definition (`--def-at`), and document outlines (`--symbols`).
