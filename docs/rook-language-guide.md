# Rook Language & Architecture Guide (v0.5.2)

> **Specification & Reference Manual**  
> Technical documentation covering Rook language mechanics, memory layouts, C ABI compatibility, compiler architecture, and systems programming foundations.

---

## Table of Contents

- [PART I: Technical Reference & Architecture](#part-i-technical-reference--architecture)
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
- [PART II: Foundational Systems Tutorial (Beginner Track)](#part-ii-foundational-systems-tutorial-beginner-track)
  - [B1. Hardware Architecture & Memory Hierarchy](#b1-hardware-architecture--memory-hierarchy)
  - [B2. Your First Program & Execution Lifecycle](#b2-your-first-program--execution-lifecycle)
  - [B3. Bits, Bytes, and Integer Widths](#b3-bits-bytes-and-integer-widths)
  - [B4. Memory Addresses & The Pointer Mental Model](#b4-memory-addresses--the-pointer-mental-model)
  - [B5. Structs & Memory Layout in Practice](#b5-structs--memory-layout-in-practice)
  - [B6. Arrays, Buffers, and Bounds Safety](#b6-arrays-buffers-and-bounds-safety)
  - [B7. Resource Management & Avoiding Leaks (defer)](#b7-resource-management--avoiding-leaks-defer)
  - [B8. Building Real Projects & C Library Integration](#b8-building-real-projects--c-library-integration)

---

# PART I: Technical Reference & Architecture

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

## 2. Architecture & The Three Compiler Backends

The `rokade` compiler features a modular, multi-backend pipeline:

```
Source (.rook) ➔ Lexer/Parser (AST) ➔ Sema & Libclang ➔ Codegen
                                                    ├── C Backend (C11/C23)
                                                    ├── LLVM Backend (LLVM-C JIT)
                                                    └── LLVM2 Backend (Direct IR)
```

| Backend | Implementation | Target Output | Characteristics |
| :--- | :--- | :--- | :--- |
| **C Backend (`--backend=c`)** | `src/c_backend.c` | C11 / C23 (`.c`) | Default backend. Transpiles AST directly to portable C source and invokes GCC or Clang. Compatible with existing Makefiles, CMake builds, GDB, and LLDB. |
| **LLVM Backend (`--backend=llvm`)** | `src/llvm_backend.c` | Native Object (`.o`) / JIT | Emits LLVM IR via LLVM-C API. Supports in-memory execution (`rokade run --jit`) for rapid testing without disk artifacts. |
| **LLVM2 Backend (`--backend=llvm2`)** | `src/llvm2_backend.c` | Direct LLVM IR (`.ll` / `.o`) | Next-generation backend. Direct typed LLVM IR codegen, short-circuit evaluation (`&&`, `||`) via PHI nodes, automated C typedef resolution, struct union backing buffers, and zero-wrapper foreign calls. |

---

## 3. Technical Comparison: C, Rust, Zig, and Rook

| Feature / Dimension | Standard C (C11/C23) | Rust (2024 Edition) | Zig (0.13+) | Rook (v0.5.2) |
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

In the `llvm2` backend, short-circuiting is lowered using basic blocks (`land.rhs`, `land.merge`) and PHI nodes:
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

## 9. Standard Library (std/) Architecture

| Module | Include | Primary Capabilities |
| :--- | :--- | :--- |
| **`std/io`** | `#comprise std/io` | Buffered stream I/O, file descriptor abstraction, line-by-line reading, binary read/write. |
| **`std/math`** | `#comprise std/math` | Arithmetic routines: `min`, `max`, `clamp`, `abs`, power routines, float comparisons. |
| **`std/json`** | `#comprise std/json` | Streaming recursive-descent JSON parser and serializer with zero external dependencies. |
| **`std/log`** | `#comprise std/log` | Structured leveled logging (`DEBUG`, `INFO`, `WARN`, `ERROR`) with ISO-8601 timestamps. |
| **`std/test`** | `#comprise std/test` | Assertion framework (`assert_eq`, `assert_true`), test harness, and failure reports. |

---

## 10. Configuration, Multi-Target Builds & Toolchains

### 10.1 CLI Reference
```bash
# Build and execute
rokade run [path] [--backend=c|llvm|llvm2]

# Build release binary
rokade build [path] [--backend=c|llvm|llvm2]

# Comprehensive environment and corpus health check
rokade doctor

# Query and set toolchain overrides
rokade toolchain
rokade toolchain set cc /usr/bin/clang
```

### 10.2 Cross-Compilation
- **Windows (x86_64-w64-mingw32):** Compiles via MinGW-w64 GCC.
- **Android (aarch64-linux-android):** Cross-compiles using the Android NDK Clang toolchain.

---

# PART II: Foundational Systems Tutorial (Beginner Track)

## B1. Hardware Architecture & Memory Hierarchy

Software executes directly on physical hardware composed of three primary tiers:

```
[Permanent Storage (SSD)]  ➔  [Main Memory (RAM)]  ➔  [CPU Registers & Caches]
~10–50 μs latency              ~50–100 ns latency         ~0.5–5 ns latency
```

- **Permanent Storage:** Holds binary files on disk. The CPU cannot execute instructions directly from storage.
- **Main Memory (RAM):** A contiguous array of byte storage cells indexed by numerical addresses.
- **CPU Registers:** Small, high-speed storage slots inside the core (`RAX`, `RSP`, `RIP`).

### Stack vs. Heap
- **Call Stack:** Managed by adjusting the Stack Pointer register (`RSP`). Allocations are instantaneous and deallocated automatically on function return.
- **Heap:** Managed by an allocator (`malloc`/`free`). Dynamically sized and persists until explicitly freed.

---

## B2. Your First Program & Execution Lifecycle

```rook
#include <stdio.h>

int main() {
    printf("Hello from Rook systems code!\n");
    return 0; // Return code 0 indicates success to the operating system
}
```

When you run `rokade run`:
1. **Compilation:** `main.rook` is translated into machine instructions.
2. **Linking:** The object code is linked with standard C runtime libraries (`libc`).
3. **Execution:** The operating system kernel initializes a process, allocates virtual memory, and points the Instruction Pointer to `main`.

---

## B3. Bits, Bytes, and Integer Widths

In systems programming, data types represent concrete physical bit patterns:

| Type | Bit Width | Byte Size | Signed Range | Unsigned Equivalent |
| :--- | :--- | :--- | :--- | :--- |
| `int8_t` | 8 bits | 1 byte | -128 to 127 | `uint8_t` (0 to 255) |
| `int16_t` | 16 bits | 2 bytes | -32,768 to 32,767 | `uint16_t` (0 to 65,535) |
| `int32_t` (`int`) | 32 bits | 4 bytes | ~-2.14B to ~2.14B | `uint32_t` (0 to ~4.29B) |
| `int64_t` | 64 bits | 8 bytes | -9.22 &times; 10<sup>18</sup> to 9.22 &times; 10<sup>18</sup> | `uint64_t` (0 to 1.84 &times; 10<sup>19</sup>) |

Signed integers use **Two's Complement** encoding. If an 8-bit signed integer holding `127` (`01111111`) is incremented by 1, it wraps around to `-128` (`10000000`).

---

## B4. Memory Addresses & The Pointer Mental Model

A pointer is an integer variable whose value is an address in memory.

### Address-Of (`&`) and Dereference (`*`)
```rook
int target = 42;
int* ptr = &target; // ptr stores the address of target

printf("Address: %p\n", (void*)ptr);
printf("Value:   %d\n", *ptr); // Reads 4 bytes at that address

*ptr = 99; // Writes 99 to the memory address in ptr
printf("Updated target: %d\n", target); // Prints 99
```

### Pointer Arithmetic
Adding `1` to a pointer `T*` advances the memory address by `sizeof(T)` bytes:
```rook
int numbers[3] = { 100, 200, 300 };
int* p = &numbers[0]; // Address: 0x1000

p = p + 1; // Advances by 1 * sizeof(int) (4 bytes) -> Address: 0x1004
printf("%d\n", *p); // Prints 200
```

> **Warning:** Dereferencing a null pointer (`NULL` or address `0`) triggers a Segmentation Fault (SIGSEGV) from the CPU Memory Management Unit (MMU).

---

## B5. Structs & Memory Layout in Practice

Structures group heterogeneous fields into contiguous memory blocks:

```rook
struct Vector3 {
    x: float
    y: float
    z: float
}

int main() {
    Vector3 v = { 1.0, 2.0, 3.0 };
    printf("Vector: (%.1f, %.1f, %.1f)\n", v.x, v.y, v.z);
    return 0;
}
```

### Passing Strategies
- **Pass by Value (`void process(Vector3 v)`):** Copies all 12 bytes onto the new stack frame. Modifications affect only the local copy.
- **Pass by Pointer (`void process(Vector3* v)`):** Passes a single 8-byte memory address in a CPU register. Modifications directly mutate the caller's memory.

---

## B6. Arrays, Buffers, and Bounds Safety

```rook
int buffer[5]; // 5 integers * 4 bytes = 20 contiguous stack bytes
buffer[0] = 10;
buffer[1] = 20;

for item in buffer {
    printf("%d\n", item);
}
```

Compiling with `-b` (`rokade build -b`) injects runtime bounds checks that terminate execution safely if an invalid index is accessed.

---

## B7. Resource Management & Avoiding Leaks (defer)

```rook
#include <stdio.h>
#include <stdlib.h>

int process_data(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) return -1;
    defer fclose(f); // Guaranteed to execute on function exit

    char* buf = (char*)malloc(1024);
    if (!buf) return -2;
    defer free(buf); // Guaranteed to execute before fclose(f)

    if (fread(buf, 1, 1024, f) <= 0) {
        return -3; // Both buf is freed and f is closed
    }

    return 0;     // Both buf is freed and f is closed
}
```

---

## B8. Building Real Projects & C Library Integration

```toml
[project]
name = "graphics_demo"
version = "0.1.0"
pkg-config = ["raylib"]
```

```rook
#include <raylib.h>

int main() {
    InitWindow(640, 480, "Rook Raylib Window");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("Native C Library Called from Rook!", 100, 200, 20, DARKGRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
```
Compile and run directly:
```bash
rokade run
```
