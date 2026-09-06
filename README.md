# Rook (`rokade`)

Rook is a systems programming language that compiles to clean, standard C (C23 / C11) or native machine code via LLVM. It pairs the performance, transparent ABI, and universal interoperability of C with modern language ergonomics: headerless single-file modules, single inheritance with static dispatch, algebraic sum types with pattern matching, deterministic resource cleanup (`defer`), and compile-time prevention of common undefined behaviors.

---

## Table of Contents

- [Architecture & Design Principles](#architecture--design-principles)
- [Safety & Compiler Enforcements](#safety--compiler-enforcements)
- [Language Tour](#language-tour)
  - [Variables & Type Inference](#variables--type-inference)
  - [Headerless Modules with `#comprise`](#headerless-modules-with-comprise)
  - [C Interoperability](#c-interoperability)
  - [Object-Oriented Programming (`object` / `impl`)](#object-oriented-programming-object--impl)
  - [Sum Types & Pattern Matching (`sum` / `match`)](#sum-types--pattern-matching-sum--match)
  - [Deterministic Resource Management (`defer`)](#deterministic-resource-management-defer)
- [Getting Started & Installation](#getting-started--installation)
  - [Prerequisites](#prerequisites)
  - [Building from Source](#building-from-source)
  - [Automated Installer (Linux / macOS)](#automated-installer-linux--macos)
  - [Windows Installation (PowerShell)](#windows-installation-powershell)
  - [Environment Diagnostics (`rokade doctor`)](#environment-diagnostics-rokade-doctor)
- [Project System & Multi-Target Builds](#project-system--multi-target-builds)
  - [Project Workflow](#project-workflow)
  - [`rokade.toml` Configuration](#rokadetoml-configuration)
  - [Cross-Platform Compilation](#cross-platform-compilation)
  - [Dependencies: Source Packages & `pkg-config`](#dependencies-source-packages--pkg-config)
- [Standard Library (`std`)](#standard-library-std)
- [Editor Integration (Zed & LSP)](#editor-integration-zed--lsp)
- [License](#license)

---

## Architecture & Design Principles

Rook is designed around five core systems engineering principles:

1. **Direct C ABI Compatibility**: Functions, structs, and primitive types in Rook map 1:1 to standard C layouts. Rook source files can directly include C headers (`#include <stdio.h>`) and link against any system C library without wrappers or binding generators.
2. **Dual Compiler Backends**:
   - **C Backend (Default)**: Emits readable, portable C23/C11 code to `build/generated/` and drives the host toolchain (`gcc`, `clang`, MinGW, or Android NDK).
   - **LLVM Backend (`--backend=llvm`)**: Native LLVM code generation with JIT execution (`rokade run --jit`) and cross-compilation support via LLVM target triples.
3. **Headerless Single-File Modules**: Rook replaces separate `.h` header files with `#comprise <module>`. Modules are parsed and compiled directly from source, with automatic deduplication resolving circular or diamond dependencies.
4. **Zero-Overhead Abstractions**: Single inheritance (`object Child : Parent`) and method implementations (`impl`) lower directly to static function calls at compile time. There are no virtual method tables (vtables), dynamic dispatch lookups, or hidden allocations.
5. **Deterministic Memory Model**: Memory is managed explicitly using pointers, stack allocation, or custom allocators. Scope-exit cleanup is handled deterministically via `defer`, without garbage collection pauses.

---

## Safety & Compiler Enforcements

Rook enforces strict compiler guards within `.rook` source code to eliminate common sources of Undefined Behavior (UB) and silent logic errors:

| Language Feature | Standard C Behavior | Rook (`.rook`) Enforcement |
| :--- | :--- | :--- |
| **Assignment in Conditions** | `if (x = 5)` assigns and evaluates truthiness | **Compile Error**: `assignment used as condition; did you mean '=='?` |
| **Pointer Syntax Format** | `int *p`, `int* p`, `*int p` (ambiguous) | **Standardized**: Postfix `Type*` required (e.g. `int* p`). Prefix `*Type` rejected. |
| **Uninitialized Stack Memory** | Contains indeterminate stack garbage (UB) | **Zero-Initialized**: Uninitialized locals default to `= {0}`. |
| **`goto` Statements** | Permitted; bypasses scope initialization | **Banned**: `'goto' is not supported in Rook`. |
| **Comma Operator as Expression** | `(a, b)` discards `a`, evaluates `b` | **Banned as Expression**: Commas permitted only as syntactic separators. |
| **`void*` Pointer Arithmetic** | Permitted as compiler extension (ISO C UB) | **Compile Error**: Pointer arithmetic on `void*` requires explicit typed cast. |
| **Pointer Arithmetic Operators** | `*`, `/`, `%`, `&`, `\|`, `^` on pointers | **Compile Error**: Multiplication, division, modulo, and bitwise ops on pointers rejected. |
| **Literal Division by Zero** | Triggers runtime crash or hardware trap | **Compile Error**: Literal `x / 0` and `x % 0` rejected at compile time. |

> [!NOTE]
> These compiler rules apply strictly to Rook source code (`.rook`). External C headers (`#include`) and `[[raw]]` C blocks retain unrestricted ISO C semantics.

---

## Language Tour

### Variables & Type Inference

```rook
#include <stdio.h>

int main() {
    // Type inference via `let`
    let count = 42;
    let message = "Rook initialized";

    // Explicit type declarations
    int x = 10;
    int* ptr = &x;

    // Uninitialized locals are zero-initialized by the compiler
    int zero_val; // Emitted as int zero_val = {0};

    printf("%s: count=%d, *ptr=%d, zero=%d\n", message, count, *ptr, zero_val);
    return 0;
}
```

### Headerless Modules with `#comprise`

Rook uses `#comprise` to import other modules without separate header files:

```rook
// math.rook
int add(int a, int b) {
    return a + b;
}
```

```rook
// main.rook
#include <stdio.h>
#comprise math

int main() {
    printf("sum = %d\n", add(10, 20));
    return 0;
}
```

Diamond dependency graphs (`A -> B`, `A -> C`, `B -> D`, `C -> D`) are deduplicated with `#pragma once` semantics.

### C Interoperability

Standard C libraries and POSIX headers can be included directly:

```rook
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main() {
    double value = 144.0;
    double root = sqrt(value);
    printf("sqrt(%.1f) = %.1f\n", value, root);
    return 0;
}
```

### Object-Oriented Programming (`object` / `impl`)

Rook provides single inheritance with static dispatch and zero runtime overhead:

```rook
#include <stdio.h>

object Animal {
    name: const char*
    age: int
}

impl Animal {
    void speak(self) {
        printf("%s makes a sound.\n", self.name);
    }
}

object Dog : Animal {
    breed: const char*
}

impl Dog {
    void bark(self) {
        printf("%s (%s) barks!\n", self.name, self.breed);
    }
}

int main() {
    // Flat initializer syntax lowers to subobject initialization
    Dog dog = Dog { name: "Rover", age: 3, breed: "Retriever" };

    // Static dispatch: lowers to Animal_speak(&dog._base) and Dog_bark(&dog)
    dog.speak();
    dog.bark();

    return 0;
}
```

### Sum Types & Pattern Matching (`sum` / `match`)

Rook supports tagged unions (algebraic sum types) with compile-time layout matching and exhaustive pattern matching:

```rook
#include <stdio.h>

sum Shape {
    Circle { radius: double; };
    Rect { width: double; height: double; };
    Point;
}

double compute_area(Shape s) {
    match (s) {
        Circle { radius } => 3.1415926535 * radius * radius,
        Rect { width, height } => width * height,
        Point => 0.0,
        _ => 0.0,
    }
}

int main() {
    Shape s1 = Circle { radius: 2.0 };
    Shape s2 = Rect { width: 4.0, height: 5.0 };

    printf("Circle area: %.2f\n", compute_area(s1));
    printf("Rect area: %.2f\n", compute_area(s2));
    return 0;
}
```

### Deterministic Resource Management (`defer`)

`defer` schedules cleanup statements to execute upon exiting the enclosing scope or returning from a function:

```rook
#include <stdio.h>
#include <stdlib.h>

int process_data(int size) {
    int* buffer = (int*)malloc(size * sizeof(int));
    if (!buffer) return -1;
    defer free(buffer);

    buffer[0] = 42;
    if (buffer[0] < 0) {
        return -2; // buffer is freed before early return
    }

    printf("Buffer value: %d\n", buffer[0]);
    return 0; // buffer is freed before normal return
}
```

---

## Getting Started & Installation

### Prerequisites

- C compiler (`gcc` or `clang`)
- CMake 3.16+
- Optional: Cargo/Rust (if building the language server `rook-lsp`)

### Building from Source

```bash
git clone https://github.com/bknsehan/Rook.git
cd Rook
cmake -B build -S .
cmake --build build
```

### Automated Installer (Linux / macOS)

The installer script builds the `rokade` compiler and `rook-lsp` server, installs standard libraries, and configures user symlinks:

```bash
git clone https://github.com/bknsehan/Rook.git
cd Rook
./install.sh --prefix=$HOME/.local --with-zed
```

- Default prefix (if `--prefix` is omitted): `$HOME/bin/Rook`
- Symlinks are placed in `$HOME/.local/bin` or `$HOME/bin`

### Windows Installation (PowerShell)

```powershell
git clone https://github.com/bknsehan/Rook.git
cd Rook
.\install.ps1 -WithZed
```

### Environment Diagnostics (`rokade doctor`)

Verify your compiler toolchain, backends, cross-compilers, and standard library:

```bash
rokade doctor
```

Sample output:
```text
rokade doctor — environment health check
=========================================
[PASS] toolchain: /usr/bin/gcc (gcc) — gcc (GCC) 16.2.1
[PASS] backend: c (C23 / C11)
[PASS] backend: llvm (22.1.8) [JIT verified]
[PASS] c-interop: libclang (dynamic C header AST)
[PASS] corpus: 38 pass, 0 skip
[PASS] android NDK: /opt/android-sdk/ndk/27.0.12077973
[PASS] windows cross-compiler: /usr/bin/x86_64-w64-mingw32-gcc
=========================================
doctor: PASS
```

---

## Project System & Multi-Target Builds

### Project Workflow

Create a new project scaffold:
```bash
rokade new myapp
cd myapp
```

Directory structure:
```text
myapp/
├── rokade.toml
└── src/
    └── main.rook
```

Build and execute:
```bash
rokade build        # Transpiles to build/generated/ and compiles binary to build/
rokade run          # Builds and runs target binary
rokade test         # Runs test suite
```

### `rokade.toml` Configuration

```toml
[package]
name = "myapp"
version = "0.1.0"

[build]
kind = "exe"                              # "exe", "shared-lib", or "static-lib"
standard = "c2x"                          # "c11", "c17", "c2x", "gnu23"
targets = ["linux", "windows", "android"] # Target platforms

[target.linux]
kind = "exe"
cflags = "-O3"

[target.android]
kind = "shared-lib"
api = 24
arch = ["arm64-v8a", "x86_64"]
cflags = "-fPIC -O3"

[target.windows]
kind = "exe"
```

### Cross-Platform Compilation

Build all targets or select a specific platform:

```bash
rokade build --all
rokade build --target=windows    # Cross-compiles using MinGW
rokade build --target=android    # Cross-compiles using Android NDK clang
rokade build --target=linux
```

Output layout:
```text
build/
├── generated/
│   └── main.c                        # Transpiled C source
├── linux/
│   └── myapp                         # Linux ELF binary
├── windows/
│   └── myapp.exe                     # Windows PE32+ binary
└── android/
    ├── arm64-v8a/
    │   └── libmyapp.so               # Android ARM64 shared library
    └── x86_64/
        └── libmyapp.so               # Android x86_64 shared library
```

### Dependencies: Source Packages & `pkg-config`

#### 1. Source-Level Rook Packages
```toml
[dependencies]
mathlib = { path = "../mathlib" }
```

In source code:
```rook
#comprise mathlib
```

#### 2. System C Libraries via `pkg-config`
```toml
[build]
kind = "exe"
pkg-config = ["raylib"]
```

In source code:
```rook
#include <raylib.h>

int main() {
    InitWindow(800, 450, "Rook Application");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("Running Raylib natively in Rook", 120, 200, 20, DARKGRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
```

---

## Standard Library (`std`)

Rook provides a modular standard library installed under `<install_prefix>/std`:

- `<std/io>`: Formatted output functions (`println`, `print`, `eprintln`).
- `<std/math>`: Vector math (`Vec2`, `Vec3`, `.add()`, `.dot()`, `clampf`, `minf`, `maxf`, `lerpf`).
- `<std/os>`: Runtime and process primitives (`panic`, `exit_with`).
- `std`: Umbrella prelude module importing core utilities.

Example:
```rook
#comprise <std/io>
#comprise <std/math>

int main() {
    println("Using Rook standard library");
    let a = Vec2 { x: 3.0, y: 4.0 };
    let b = Vec2 { x: 1.0, y: 2.0 };
    let c = a.add(b);
    printf("Result: (%.1f, %.1f)\n", c.x, c.y);
    return 0;
}
```

---

## Editor Integration (Zed & LSP)

Rook includes a Language Server Protocol implementation (`rook-lsp`) and an extension for the [Zed](https://zed.dev) editor:

- **Extension Location**: `editors/zed/` (installed automatically via `./install.sh --with-zed`).
- **Language Server**: Built from `lsp/` (`rook-lsp`).

### Zed Configuration

Add to `~/.config/zed/settings.json`:

```json
{
  "lsp": {
    "rook-lsp": {
      "binary": {
        "path": "rook-lsp"
      }
    }
  },
  "languages": {
    "Rook": {
      "language_servers": ["rook-lsp"]
    }
  }
}
```

If `rook-lsp` is located in `$HOME/.local/bin` or `$HOME/bin`, ensure the directory is present in your user environment `PATH`.

---

## License

Rook is distributed under the [MIT License](LICENSE).
