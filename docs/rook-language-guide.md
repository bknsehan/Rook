# Rook Language & Architecture Guide (v0.7.0)

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

| Feature / Dimension | Standard C (C11/C23) | Rust (2024 Edition) | Zig (0.13+) | Rook (v0.7.0) |
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
When a `struct` inherits from a parent struct, the parent fields are placed at byte offset 0:

```rook
struct Entity {
    id: int;
    active: bool;
};

struct Player : Entity {
    health: int;
    score: int;
};

int main() {
    Player p;
    Entity* e = (Entity*)&p; // Exact same memory address, zero adjustment
    return 0;
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
#include <stdio.h>
#include <stdint.h>

// uint32_t is a typedef alias to 'unsigned int' in C standard headers.
// The compiler unwraps typedef aliases dynamically to verify exact compatibility:
int main() {
    uint32_t count = 42;
    unsigned int raw = count; // Exact compatibility verified by ck_unwrap_typedef
    return 0;
}
```

---

## 6. Control Flow & Evaluation Rules

### 6.1 Conditionals & Assignment Rejection
Assignment inside condition expressions is rejected at compile time:
```rook
int main() {
    int x = 0;
    if (x == 5) {
        // Valid equality check
    }

    // if (x = 5) { } // Hard compile-time error: assignment not allowed in condition
    return 0;
}
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

### 6.3 Loops
```rook
#include <stdio.h>

int main() {
    // Standard while loop
    int n = 0;
    while (n < 5) {
        n = n + 1;
    }

    // Three-clause for loop
    for (int i = 0; i < 10; i++) {
        printf("%d\n", i);
    }

    // Array literal iteration (for-in)
    for item in [10, 20, 30, 40] {
        printf("%d\n", item);
    }
    return 0;
}
```

### 6.4 Deterministic Cleanup (`defer`)
The `defer` statement registers a cleanup statement or block executed upon scope exit in reverse order (LIFO):
```rook
#include <stdio.h>
#include <stdlib.h>

int main() {
    FILE* f = fopen("data.bin", "rb");
    if (!f) return 0;
    defer fclose(f); // Guaranteed to execute on any exit path

    void* buf = malloc(1024);
    if (!buf) return 0;
    defer free(buf); // Executes before fclose(f)

    return 0;
}
```

---

## 7. Algebraic Data Types: sum, enum & match

### 7.1 Sum Types (Tagged Unions)
```rook
sum Shape {
    Circle { radius: float; };
    Rectangle { width: float; height: float; };
    Point;
}
```

Memory layout:
- `tag: int32_t` (0 = Circle, 1 = Rectangle, 2 = Point)
- Padding (4 bytes)
- `payload: union` sized to `max(sizeof(Circle), sizeof(Rectangle))` = 8 bytes

### 7.2 Exhaustive Pattern Matching
```rook
float get_area(Shape s) {
    return match (s) {
        Circle { radius }           => 3.14159 * radius * radius,
        Rectangle { width, height } => width * height,
        Point                       => 0.0,
        _                           => 0.0,
    };
}

int main() {
    Shape c = Circle { radius: 2.0 };
    Shape r = Rectangle { width: 3.0, height: 4.0 };
    Shape p = Point;
    return 0;
}
```

### 7.3 Canonical Error & Option Handling: `std/result`
```rook
#include <stdio.h>
#comprise <std/result>
#comprise <std/io>

Result parse_positive_int(int val) {
    if (val > 0) {
        return result_ok((void*)1);
    }
    return result_err("Value must be positive");
}

int main() {
    Result res = parse_positive_int(42);
    if (res.is_ok()) {
        println("Success: valid positive number");
    } else {
        printf("Error: %s\n", res.unwrap_err());
    }
    return 0;
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
Rook functions matching C callback prototypes can be passed directly as C function pointers:
```rook
#include <stdio.h>
#include <stdlib.h>

int compare_ints(const void* a, const void* b) {
    int arg1 = *(const int*)a;
    int arg2 = *(const int*)b;
    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
}

int main() {
    int arr[5] = { 5, 2, 8, 1, 9 };
    qsort(arr, 5, sizeof(int), compare_ints);
    for (int i = 0; i < 5; i = i + 1) {
        printf("%d ", arr[i]);
    }
    printf("\n");
    return 0;
}
```

### 8.3 Package Dependencies (`rokade.toml`)
Dependencies on system C libraries are declared in `rokade.toml` under `pkg-config` in the `[build]` table:
```toml
[package]
name = "gui_app"
version = "0.1.0"

[build]
kind = "exe"
pkg-config = ["raylib"]
```
The compiler automatically queries `pkg-config --cflags` and `pkg-config --libs` during build execution.

---

## 9. Project Composition: Inclusions, Modules & Cross-Path Ingestion

Rook separates the ingestion of foreign C interfaces from native Rook module composition via two distinct directives: `#include` and `#comprise`.

### 9.1 The Two Ingestion Models: `#include` vs `#comprise`

| Directive | Target File Type | Parsing Engine | Behavior & Semantics |
| :--- | :--- | :--- | :--- |
| `#include <header.h>`<br>`#include "header.h"` | C Header (`.h`) | libclang (Clang C Frontend) | Parses host C headers dynamically at compile time. Ingests C functions, structs, unions, typedefs, enums, and macros directly into Rook's symbol table without hand-written binding layers. |
| `#comprise <std/mod>`<br>`#comprise "path.rook"` | Rook Source (`.rook`) | Rokade Preprocessor | Inlines and resolves Rook source modules. Expands declarations into the compilation unit with canonical path deduplication. |

### 9.2 Cross-Directory Module Ingestion & Relative Path Resolution
Rook source files can be composed across nested directories using standard POSIX relative paths:
- **Standard Library Modules:** Use angle brackets with the `std/` prefix, e.g., `#comprise <std/io>` or `#comprise <std/str>`. The compiler resolves these from the toolchain installation directory or `ROKADE_PATH`.
- **Sibling Modules:** Use quoted relative paths, e.g., `#comprise "utils.rook"` or `#comprise "./types.rook"`.
- **Nested Subdirectories:** Specify the relative path to sub-modules, e.g., `#comprise "engine/renderer.rook"` or `#comprise "net/socket.rook"`.
- **Parent and Sibling Trees:** Walk up directory hierarchies using `../`, e.g., `#comprise "../shared/config.rook"`.

> [!NOTE]
> **Canonical Path Deduplication:** Rook resolves every included file to its canonical filesystem path (via `rk_realpath`). If multiple modules comprise the same source file—either directly or transitively—the compiler expands and parses that file exactly once. Circular comprises are automatically prevented, eliminating the need for C-style `#ifndef` include guards.

### 9.3 Multi-File Project Architecture (Comprehensive Example)

Consider a modular systems project structured across directories with external C dependencies:

```text
my_game/
├── rokade.toml
└── src/
    ├── main.rook
    ├── config.rook
    └── engine/
        ├── math.rook
        └── renderer.rook
```

In `src/engine/math.rook`:
```rook
// src/engine/math.rook
struct Vec2 {
    x: float;
    y: float;
};

Vec2 vec2_new(float x, float y) {
    return Vec2 { x: x, y: y };
}
```

In `src/engine/renderer.rook`:
```rook
// src/engine/renderer.rook
#include <raylib.h>
#comprise <std/str>
#comprise "math.rook"       // Sibling comprise inside engine/

struct RenderContext {
    width: int;
    height: int;
    title: Str;
};

RenderContext renderer_create(int w, int h, Str title) {
    char* c_title = title.to_cstr();
    defer free(c_title);
    InitWindow(w, h, c_title);
    return RenderContext { width: w, height: h, title: title };
}

void renderer_draw_point(RenderContext* ctx, Vec2 pos, Color c) {
    DrawPixel((int)pos.x, (int)pos.y, c);
}
```

In `src/main.rook`:
```rook
// src/main.rook
#include <stdio.h>
#comprise <std/io>
#comprise <std/str>
#comprise "config.rook"             // Local comprise
#comprise "engine/renderer.rook"    // Subdirectory comprise

int main() {
    Str app_name = str_from_cstr("Rook Modular Engine");
    println_str(app_name);

    RenderContext rc = renderer_create(800, 600, app_name);
    defer CloseWindow();

    Vec2 player_pos = vec2_new(400.0f, 300.0f);
    renderer_draw_point(&rc, player_pos, RAYWHITE);
    return 0;
}
```

---

## 10. The Rook Standard Library (std/) Deep Dive

The Rook Standard Library resides in `std/`. It is written purely in Rook, carries zero runtime overhead, and relies exclusively on standard C ABI primitives.

### 10.1 Safe String Slices: `std/str`
C strings (`char*`) are null-terminated, requiring $O(n)$ scans for length calculations and risking buffer overflows. Rook's `Str` provides a safe, non-owning slice holding a pointer and an explicit length:

```rook
struct Str {
    data: const char*;
    len: size_t;
};
```

#### Core Capabilities of `Str`:
- **Zero-Allocation Slicing:** `s.slice(start, end)` returns a new `Str` view over existing memory in $O(1)$ time without allocating.
- **Safe Inspection:** `s.is_empty()`, `s.starts_with(prefix)`, `s.ends_with(suffix)`, `s.equals(other)`, `s.char_at(index)`.
- **Searching & Splitting:** `s.find_char(c)`, `s.find(needle)`, `s.contains(needle)`, `s.split_once(delim, &left, &right)`.
- **Whitespace Trimming:** `s.trim_start()`, `s.trim_end()`, `s.trim()`.
- **Parsing & Conversions:** `s.to_int()`, `s.to_float()`, `s.to_bool()`, `s.to_cstr()` (heap-allocated copy).

```rook
#include <stdio.h>
#comprise <std/io>
#comprise <std/str>

void demonstrate_strings() {
    Str full = str_from_cstr("HOST=127.0.0.1:8080");
    Str key;
    Str val;

    if (full.split_once('=', &key, &val)) {
        println_str(key); // Prints: HOST
        println_str(val); // Prints: 127.0.0.1:8080
    }

    Str port_str = val.slice(10, val.len);
    int port = port_str.to_int();
    println_int(port); // Prints: 8080
}

int main() {
    demonstrate_strings();
    return 0;
}
```

### 10.2 Ergonomic Input/Output: `std/io`
`std/io` replaces raw `printf`/`scanf` with type-safe, bounds-checked I/O routines:
- **Printing:** `print(s)`, `println(s)`, `println_int(n)`, `println_float(f)`, `println_bool(b)`, `println_str(s)`.
- **Standard Error:** `eprint(s)`, `eprintln(s)`, `eprintln_int(n)`, `eprintln_str(s)`, `io_eflush()`.
- **Safe Input Scanning:** `scanln(buf, cap)`, `scanln_alloc()`, `scan_word(buf, cap)`, `scan_int(&out)`, `scan_float(&out)`.

### 10.3 High-Performance Memory Management: `std/mem`
In high-throughput systems, invoking `malloc`/`free` repeatedly fragments the heap and incurs allocator lock contention. `std/mem` provides specialized allocators:

| Allocator | Allocation Pattern | Deallocation Mechanism | Use Case |
| :--- | :--- | :--- | :--- |
| **`Arena`** | Linear monotonic bump pointer. Aligns all allocations to 8-byte boundaries. | Bulk reset via `arena.reset()` or free via `arena.destroy()`. | Per-frame allocations in games, request-scoped lifecycles in network servers, AST compilation phases. |
| **`ArenaTemp`** | Saves current arena offset. Sub-allocations increment offset. | Restores offset via `arena_temp_end()`. | Temporary scratchpad buffers inside inner loops or subroutines. |
| **`ElementPool`** | Fixed-size chunk allocator with embedded free-list recycling. | Instantaneous $O(1)$ return via `pool.free(ptr)`. | Game entities, network connection slots, graph nodes, AST nodes. |

```rook
#include <stdio.h>
#comprise <std/mem>
#comprise <std/io>

struct Packet {
    id: int
    data: char[60]
}

void test_allocators() {
    // 1. Linear Arena Allocation
    Arena arena = arena_new(1024 * 1024); // 1 MB buffer
    defer arena.destroy();

    int* numbers = (int*)arena.alloc(100 * sizeof(int));
    numbers[0] = 42;

    // 2. Element Pool (O(1) recycling)
    ElementPool pool = pool_new(sizeof(Packet), 100);
    defer pool.destroy();

    Packet* p1 = (Packet*)pool.alloc();
    p1->id = 1;

    pool.free((void*)p1); // Re-added to free list immediately
    Packet* p2 = (Packet*)pool.alloc(); // Reuses p1's slot without OS allocation
}

int main() {
    test_allocators();
    return 0;
}
```

### 10.4 Hardware Lock-Free Atomics: `std/atomic`
Rook exposes direct CPU memory bus atomic operations without library indirection:
- `AtomicInt` & `AtomicBool`: wrappers over machine-word atomic storage.
- **Operations:** `load()`, `store(val)`, `fetch_add(delta)`, `fetch_sub(delta)`, `exchange(val)`, and `compare_exchange(&expected, desired)`.

### 10.5 Multi-Threading & Synchronization: `std/sync`
Provides zero-overhead abstractions over host OS threads (POSIX pthreads / Windows Win32 threads):
- **`Thread`:** Created with `thread_spawn(worker_fn, arg)`. Joined via `t.join(&ret_val)`.
- **`Mutex`:** Native OS mutual exclusion lock (`m.init()`, `m.lock()`, `m.unlock()`, `m.try_lock()`, `m.destroy()`).
- **`CondVar`:** Condition variable for thread coordination (`cv.init()`, `cv.wait(&mutex)`, `cv.signal()`, `cv.broadcast()`).

### 10.6 Concurrency Safety Model & Current Limitations

> [!WARNING]
> **Explicit Concurrency Model:** Rook does not incorporate a compile-time borrow checker or automatic race detector. Thread safety is explicit and the responsibility of the systems programmer.

When developing concurrent software in Rook, developers must enforce the following architectural rules:
1. **Shared Mutable State Must Be Synchronized:** Any data structure accessible by multiple threads must be protected by a `Mutex` or implemented with lock-free atomic primitives (`AtomicInt`). Unsynchronized concurrent writes cause undefined behavior.
2. **Thread Worker Argument Lifetimes:** The `void* arg` passed to `thread_spawn` must remain valid until the spawned thread finishes executing. Passing a pointer to a local stack variable of a function that returns before `t.join()` results in a dangling pointer read. Shared state should be allocated on the heap or in an Arena that outlives all worker threads.
3. **Deadlock Prevention with `defer`:** Always unlock mutexes using `defer m.unlock()` immediately after acquiring them. This guarantees the lock is released across all return paths and branches.

```rook
#include <stdio.h>
#comprise <std/sync>
#comprise <std/atomic>

struct CounterTask {
    counter: AtomicInt;
    shared_sum: int;
    lock: Mutex;
};

void* worker(void* arg) {
    CounterTask* task = (CounterTask*)arg;
    for (int i = 0; i < 1000; i = i + 1) {
        // Lock-free atomic increment (no mutex needed)
        task->counter.fetch_add(1);

        // Mutex protects standard non-atomic shared state
        task->lock.lock();
        task->shared_sum = task->shared_sum + 1;
        task->lock.unlock();
    }
    return NULL;
}

int main() {
    CounterTask task;
    task.counter = atomic_int_new(0);
    task.shared_sum = 0;
    task.lock.init();
    defer task.lock.destroy();

    Thread t1 = thread_spawn(worker, &task);
    Thread t2 = thread_spawn(worker, &task);

    void* r1 = NULL;
    void* r2 = NULL;
    t1.join(&r1);
    t2.join(&r2);

    printf("Atomic counter: %d, Mutex sum: %d\n", task.counter.load(), task.shared_sum);
    return 0;
}
```

### 10.7 Null Safety & Explicit Errors: `std/option` & `std/result`
- **`std/option`:** Replaces unchecked nullable pointers with `Option` (`option_some(val)`, `option_none()`, `Option_unwrap(&opt)`, `Option_unwrap_or(&opt, default)`).
- **`std/result`:** Explicit error propagation (`result_ok(val)`, `result_err(code)`, `Result_is_ok(&res)`, `Result_unwrap(&res)`) eliminating silent error code ignoring.

### 10.8 Zero-Allocation JSON Parser, Path Navigator & Fluent Builder: `std/json`
`std/json` provides high-performance JSON parsing, unified dot/index-path navigation (`"users[1].name"`, `"server.port"`), 64-bit integer (`int64_t`) and `double` precision numeric getters, surrogate pair UTF-16 decoding (`\uD83D\uDE00`), complete RFC 8259 compliance (control char escaping `\u00XX`, clean float formatting), full structural validation (`json_valid()`), safe file loading (`json_load_file()`), and a container-enforced fluent `JsonBuilder`:

```rook
#comprise <std/json>

int main() {
    const char* doc = "{\"server\": {\"host\": \"127.0.0.1\", \"port\": 8080}, \"big\": 9999999999, \"users\": [{\"name\": \"Alice\"}, {\"name\": \"Bob\"}]}";
    char host[32];
    int port = 0;
    int64_t big = 0;
    char u1[32];

    json_get_string(doc, "server.host", host, sizeof(host));
    json_get_int(doc, "server.port", &port);
    json_get_i64(doc, "big", &big);
    json_get_string(doc, "users[1].name", u1, sizeof(u1));

    bool is_valid = json_valid(doc);

    JsonBuilder jb = json_builder_new(128);
    defer jb.destroy();
    jb.begin_object();
    jb.key_string("status", "ok");
    jb.key_i64("timestamp_ns", 1700000000000LL);
    jb.key("data");
    jb.begin_array();
    jb.val_int(10);
    jb.val_int(20);
    jb.end_array();
    jb.end_object();
    return 0;
}
```

### 10.9 Fast Configuration & Structured Data: `std/toml`
`std/toml` provides a zero-allocation TOML v1.0 parser and serializer supporting sections, nested tables (`[a.b]`), array of tables (`[[table]]`), dotted keys (`a.b = 1`), single/double-quoted keys (`'my key' = 1`), inline tables (`{ x = 1 }`), 64-bit integers (`toml_get_i64`), IEEE 754 floats with special values (`inf`, `-inf`, `nan`), datetime parsing (`toml_get_date_time`), string escapes (`\e`, `\uXXXX`, `\UXXXXXXXX`, multiline basic line continuation `\`), type introspection (`TomlType`), raw token extraction (`toml_get_raw`), safe file loading (`toml_load_file`), and a fluent `TomlBuilder`:

```rook
#comprise <std/toml>

int main() {
    const char* cfg = "[database]\nhost = \"localhost\"\nport = 5432\nbig = 9999999999\nval = inf\nserver = { pool = 10 }\n";
    char host[32];
    int port = 0;
    int64_t big = 0;
    double val = 0.0;
    int pool = 0;

    toml_get_string(cfg, "database", "host", host, sizeof(host));
    toml_get_int(cfg, "database", "port", &port);
    toml_get_i64(cfg, "database", "big", &big);
    toml_get_double(cfg, "database", "val", &val);
    toml_get_int(cfg, "database", "server.pool", &pool);

    TomlType t = toml_get_type(cfg, "database", "host"); // TomlString

    TomlBuilder tb = toml_builder_new(128);
    defer tb.destroy();
    tb.section("server");
    tb.set_string("bind", "0.0.0.0");
    tb.set_i64("big_id", 9999999999LL);
    return 0;
}
```

---

## 11. Idiomatic Data Structure Implementation in Rook

Building high-performance data structures in Rook leverages explicit memory allocation, pointers, struct methods via `impl`, and deterministic cleanup via `defer`.

### 11.1 Dynamic Array (Vector)
Here is an idiomatic resizable integer vector demonstrating explicit allocation, growth doubling, and bounds checking:

```rook
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

struct IntVector {
    items: int*;
    count: size_t;
    capacity: size_t;
};

IntVector vec_new() {
    return IntVector { items: NULL, count: 0, capacity: 0 };
}

impl IntVector {
    void push(IntVector* self, int value) {
        if (self->count >= self->capacity) {
            size_t new_cap = self->capacity == 0 ? 8 : self->capacity * 2;
            int* new_items = (int*)realloc(self->items, new_cap * sizeof(int));
            if (!new_items) return;
            self->items = new_items;
            self->capacity = new_cap;
        }
        self->items[self->count] = value;
        self->count = self->count + 1;
    }

    int get(IntVector* self, size_t index) {
        if (index >= self->count) return -1; // Bounds guard
        return self->items[index];
    }

    void destroy(IntVector* self) {
        if (self->items) {
            free(self->items);
            self->items = NULL;
        }
        self->count = 0;
        self->capacity = 0;
    }
}

int main() {
    IntVector v = vec_new();
    defer v.destroy(); // Guarantees zero memory leaks on return

    for (int i = 0; i < 20; i = i + 1) {
        v.push(i * 10);
    }

    printf("Element at 5: %d\n", v.get(5)); // Prints: 50
    return 0;
}
```

### 11.2 Singly Linked List with Recycled Allocations
By pairing custom node structures with `ElementPool` from `std/mem`, linked lists achieve cache locality and zero heap fragmentation:

```rook
#include <stdio.h>
#comprise <std/mem>

struct ListNode {
    value: int;
    next: ListNode*;
};

struct LinkedList {
    head: ListNode*;
    pool: ElementPool*;
};

LinkedList list_create(ElementPool* pool) {
    return LinkedList { head: NULL, pool: pool };
}

impl LinkedList {
    void prepend(LinkedList* self, int value) {
        ListNode* node = (ListNode*)self->pool->alloc();
        if (!node) return;
        node->value = value;
        node->next = self->head;
        self->head = node;
    }

    void print_all(LinkedList* self) {
        ListNode* curr = self->head;
        while (curr != NULL) {
            printf("%d -> ", curr->value);
            curr = curr->next;
        }
        printf("NULL\n");
    }
}

int main() {
    ElementPool pool = pool_new(sizeof(ListNode), 50);
    defer pool.destroy();

    LinkedList list = list_create(&pool);
    list.prepend(10);
    list.prepend(20);
    list.prepend(30);
    list.print_all();
    return 0;
}
```

---

## 12. Configuration, Multi-Target Builds & Toolchains

The Rokade build system manages compilation, cross-compilation, and language server diagnostics.

### 12.1 CLI Commands
```bash
# Compile and run project
rokade run [path] [--backend=c|llvm|llvm2]

# Build release binary
rokade build [path] [--backend=c|llvm|llvm2]

# Run one-shot health and toolchain verification
rokade doctor

# Query or modify toolchain overrides
rokade toolchain
rokade toolchain set cc /usr/bin/clang
```

### 12.2 Cross-Compilation
Rokade supports cross-compilation targets out of the box:
- **Windows (x86_64-w64-mingw32):** Compiles via MinGW-w64 GCC toolchain.
- **Android (aarch64-linux-android):** Cross-compiles using the Android NDK Clang toolchain and target sysroot.

### 12.3 Language Server Protocol (`rook-lsp`)
The official language server (written in Rust) provides editor integration for editors including Zed, VSCode, and Neovim. It provides syntax validation, semantic diagnostics via `rokade --diagnostics`, jump-to-definition (`--def-at`), and document outlines (`--symbols`).
