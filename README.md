# Rook (`rokade`)

> **A 1:1 Transpiled Systems Language with Zero-Overhead C Interop, Headerless Modules, Clean OOP, and Compile-Time Safety.**

Rook is a modern systems programming language that compiles directly into standard, human-readable C (C23 / C11). It combines the speed, control, and universal compatibility of C with modern ergonomics: single-file modules without header files, single inheritance with static dispatch, resource safety via `defer`, and compile-time elimination of C's classic undefined behaviors.

---

## Table of Contents

- [Why Rook?](#why-rook)
- [Core Features](#core-features)
- [Safety & Standards (What Rook Bans vs What C Accepts)](#safety--standards)
- [What Rook Is Used For](#what-rook-is-used-for)
- [What Rook Is NOT Used For](#what-rook-is-not-used-for)
- [Advantages & Disadvantages](#advantages--disadvantages)
- [Language Tour](#language-tour)
  - [Variables & Types](#variables--types)
  - [Headerless Modules with `#comprise`](#headerless-modules-with-comprise)
  - [C Interoperability](#c-interoperability)
  - [Object-Oriented Programming (`object` / `impl`)](#object-oriented-programming-object--impl)
  - [Resource Management with `defer`](#resource-management-with-defer)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Building Rokade](#building-rokade)
  - [Project Workflow](#project-workflow)
- [License](#license)

---

## Why Rook?

C is the standard systems programming language, but it carries several well-known drawbacks:
1. **Header synchronization**: Every symbol must be manually synchronized across `.h` and `.c` files.
2. **Undefined Behavior (UB) traps**: Uninitialized stack variables, assignments disguised as conditions (`if (x = 5)`), unconstrained pointer arithmetic, and unstructured `goto`.
3. **No native OOP**: Implementing simple object inheritance in C requires manual nested struct boilerplate, error-prone pointer casts, or custom macros.
4. **Complex build configurations**: Writing Makefiles or CMake files just to compile a basic multi-file project.

**Rook addresses these issues while remaining standard C under the hood.**

---

## Core Features

- **1:1 Clean C Transpilation**: The emitted C code is transparent, legible, and directly compilable with any modern C compiler (`gcc`, `clang`, `tcc`).
- **No Header Files**: Rook has no `.h` files. Modules are referenced using `#comprise <module>`, with automatic `#pragma once` deduplication that cleanly resolves diamond dependencies.
- **Direct C Interoperability**: C headers (e.g. `#include <stdio.h>`) can be written directly inside Rook source files and pass straight through to the transpiled output.
- **Zero-Cost OOP**: Clean single inheritance using `object Child : Parent` and `impl` blocks. Method calls lower to compile-time static function calls (`Parent_method(&child._base)`) with zero vtables and zero runtime overhead.
- **Flat Designated Initializers**: Initialize inherited hierarchies with clean flat syntax:
  ```rook
  Dog d = Dog { name: "Rover", age: 3, breed: "Labrador" };
  ```
  Lowered to C subobject designated initializers:
  ```c
  Dog d = {._base.name = "Rover", ._base.age = 3, .breed = "Labrador"};
  ```
- **Native Toolchain Driver**: `rokade build` and `rokade run` detect your host C compiler (`gcc`/`clang`), compile to `.o`, and link binaries natively without shellouts to CMake.

---

## Safety & Standards

Rook enforces strict safety inside `.rook` source files to eliminate common sources of Undefined Behavior (UB) and logic bugs:

| Feature | In Standard C | In Rook (`.rook`) |
| :--- | :--- | :--- |
| **Assignment in Conditions** | `if (x = 5)` silently assigns and evaluates | **Compile Error**: `assignment used as condition; did you mean '=='?` |
| **Pointer Syntax Format** | `int *p`, `int* p`, `*int p` (ambiguous) | **Standardized**: Postfix `Type*` (e.g. `int* p`). Prefix `*Type` is banned. |
| **Uninitialized Stack Memory** | Indeterminate garbage memory (UB) | **Eliminated**: Uninitialized locals auto-emit `= {0}` in C. |
| **`goto` Statements** | Permitted, bypasses scopes | **Banned**: `'goto' is not supported in Rook`. |
| **Comma Operator Expressions** | `(a, b)` discards `a`, evaluates `b` | **Banned as Expression**: Commas only allowed as separators. |
| **`void*` Arithmetic** | GCC extension; ISO C UB | **Compile Error**: Arithmetic on `void*` requires explicit cast to `char*` or `uint8_t*`. |
| **Pointer Multiplication / Modulo** | Invalid or confusing semantics | **Compile Error**: Multiplication, division, modulo, and bitwise ops on pointers are rejected. |
| **Literal Division by Zero** | Triggers runtime trap / UB | **Compile Error**: `x / 0` and `x % 0` caught at compile time. |

> **The C Interop Boundary**: All restrictions apply exclusively to Rook source code. Standard C headers (`#include <header.h>`), `[[raw]]` blocks, and external C libraries retain full, unrestricted access to standard C semantics.

---

## What Rook Is Used For

- **Systems Programming**: OS kernels, embedded firmware, device drivers, and system daemons.
- **High-Performance Tools**: Game engines, graphics pipelines, audio processing, compilers, and CLI utilities.
- **Modernizing C Codebases**: Teams wanting the portability, speed, and ABI of C without the maintenance overhead of headers, Makefiles, and UB traps.
- **Embedded & Resource-Constrained Environments**: Zero-runtime, zero-vtable, and deterministic memory footprint.

---

## What Rook Is NOT Used For

- **Rapid Dynamic Web Scripting**: Rook is statically typed and manually managed; it is not meant to replace Python, Ruby, or JavaScript for quick glue scripts.
- **Heavy Dynamic Reflection**: Rook has no runtime introspection or reflection metadata.
- **Garbage-Collected Applications**: Memory management in Rook is manual (augmented with deterministic `defer` cleanup), not automated via a tracing GC.

---

## Advantages & Disadvantages

### Advantages
1. **100% C ABI Compatibility**: Native interop with any C library with zero wrapper overhead.
2. **Transparent Output**: Emitted C files can be audited, debugged, and inspected in `build/generated/`.
3. **No Header Maintenance**: Change a function signature once, and all comprising modules update immediately.
4. **Zero Runtime Overhead**: No hidden allocations, no vtable lookups, no garbage collector pauses.
5. **Instant Build Speed**: Fast parsing and direct toolchain compilation.

### Disadvantages / Trade-offs
1. **Requires Host C Compiler**: `rokade` requires `gcc` or `clang` on the machine to generate final binaries.
2. **Manual Memory Management**: You are responsible for freeing what you allocate, though `defer` prevents leak bugs.
3. **Evolving Ecosystem**: Rook relies directly on C's ecosystem rather than a standalone package registry.

---

## Language Tour

### Variables & Types

```rook
#include <stdio.h>

int main() {
    // Type inference with let
    let count = 42;
    let message = "Hello from Rook!";

    // Explicit typed declarations
    int x = 10;
    int* ptr = &x;

    // Uninitialized locals are deterministically zeroed (no stack garbage)
    int y; // emitted as int y = {0};

    printf("%s count=%d *ptr=%d y=%d\n", message, count, *ptr, y);
    return 0;
}
```

### Headerless Modules with `#comprise`

Rook uses `#comprise` to import other Rook files without separate headers:

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
    printf("sum = %d\n", add(3, 4));
    return 0;
}
```

Diamond dependencies (`A -> B`, `A -> C`, `B -> D`, `C -> D`) are automatically deduplicated with `#pragma once` semantics.

### C Interoperability

Standard C headers work out of the box:

```rook
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main() {
    double root = sqrt(144.0);
    printf("sqrt(144) = %.1f\n", root);
    return 0;
}
```

### Object-Oriented Programming (`object` / `impl`)

Rook provides clean single inheritance with zero runtime overhead:

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
    // Flat initialization across base and derived fields
    Dog dog = Dog { name: "Buddy", age: 4, breed: "Golden Retriever" };

    // Static method dispatch: zero vtable overhead
    dog.speak(); // calls Animal_speak(&dog._base)
    dog.bark();  // calls Dog_bark(&dog)

    return 0;
}
```

### Resource Management with `defer`

`defer` ensures cleanups run when exiting scope:

```rook
#include <stdio.h>
#include <stdlib.h>

int main() {
    int* buffer = (int*)malloc(1024 * sizeof(int));
    defer free(buffer);

    buffer[0] = 123;
    printf("buffer[0] = %d\n", buffer[0]);
    // buffer is automatically freed here
    return 0;
}
```

---

## Getting Started

### Prerequisites

- A C compiler (`gcc` or `clang`)
- CMake (for compiling the `rokade` compiler itself)
- Optional: Cargo/Rust (if building the LSP server `rook-lsp`)

### Building & Installing Rokade

#### Linux (Automated Installer):
```bash
git clone https://github.com/bknsehan/Rook.git
cd Rook
./install.sh --prefix=/home/bknsehan/bin/Rook --with-zed
```
This builds `rokade` and `rook-lsp`, installs the toolchain and `std/` into your chosen prefix, creates symlinks in `~/bin/`, and automatically integrates with the Zed editor.

> [!NOTE]
> Do **not** run `install.sh` with `sudo`. Rook installs directly into your personal user environment (`~/bin/Rook`), so simple user installation works without any root permissions.

#### Windows (PowerShell):
```powershell
git clone https://github.com/bknsehan/Rook.git
cd Rook
.\install.ps1 -WithZed
```

#### Manual Build with CMake:
```bash
cmake -B build -S .
cmake --build build
./build/rokade doctor
```

### Project Workflow

Create a new project:
```bash
./build/rokade new myapp
cd myapp
```

Build the project (generates `.c` files in `build/generated/` and native binary in `build/`):
```bash
rokade build
```

Run the project:
```bash
rokade run
```

---

## Multi-Target & Cross-Platform Builds

Rook features a built-in cross-compilation engine that transpiles source code once into `build/generated/*.c` and compiles/links for multiple platforms simultaneously.

### `rokade.toml` Multi-Target Configuration

```toml
[package]
name = "myapp"
version = "0.1.0"

[build]
kind = "exe"                    # Default build kind: exe, shared-lib, static-lib
standard = "c2x"                # C standard: c11, c17, c2x, gnu23
targets = ["linux", "android", "windows"] # Builds all 3 targets simultaneously!

# Target-specific customizations:
[target.linux]
kind = "exe"
cflags = "-O3"

[target.android]
kind = "shared-lib"             # Typically .so for JNI/NDK
api = 24                        # Android API level / min SDK
arch = ["arm64-v8a", "x86_64"]  # Targets multiple Android ABIs!
cflags = "-fPIC -O3"

[target.windows]
kind = "exe"
# Auto-detects x86_64-w64-mingw32-gcc when cross-compiling from Linux!
```

### Build CLI Options

- Build all configured targets:
  ```bash
  rokade build
  # or explicitly:
  rokade build --all
  ```
- Build a specific target:
  ```bash
  rokade build --target=android
  rokade build --target=windows
  rokade build --target=linux
  ```

### Generated Binaries Output

```
build/
├── generated/
│   └── main.c                        # Shared transpiled C
├── linux/
│   └── myapp                         # ELF 64-bit Linux executable
├── windows/
│   └── myapp.exe                     # PE32+ Windows executable
└── android/
    ├── arm64-v8a/
    │   └── libmyapp.so               # ELF 64-bit ARM aarch64 shared library
    └── x86_64/
        └── libmyapp.so               # ELF 64-bit x86-64 shared library
```

---

## Package Dependencies & C Library Integration

Rook provides two modern dependency mechanisms: **Source-Level Packages** for other Rook projects, and **First-Class C Library Interop** via `pkg-config`.

### 1. Rook-to-Rook Package Dependencies

Rook uses source-level module dependencies (similar to Go and Zig). Because Rook has no header files, source dependencies allow full compile-time static dispatch, whole-program optimizations, and consistent multi-target cross-compilation.

In your consumer's `rokade.toml`:
```toml
[package]
name = "mygame"
version = "0.1.0"

[dependencies]
mathlib = { path = "../mathlib" }
```

In your Rook source code:
```rook
#comprise mathlib

int main() {
    let v1 = Vec2 { x: 10.0, y: 20.0 };
    let v2 = Vec2 { x: 5.0, y: 5.0 };
    let v3 = v1.add(v2);
    return 0;
}
```

### 2. C Libraries as Dependencies (e.g. Raylib, SDL2, SQLite)

Rook natively understands C headers. By using `pkg-config`, Rokade automatically resolves all include paths, compiler flags, and link flags for system C libraries.

In `rokade.toml`:
```toml
[package]
name = "raylib_demo"
version = "0.1.0"

[build]
kind = "exe"
pkg-config = ["raylib"]
```

In `src/main.rook`:
```rook
#include <stdio.h>
#include <raylib.h>

int main() {
    InitWindow(800, 450, "Rook + Raylib Demo");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("Congrats! You are running Raylib natively in Rook!", 120, 200, 20, DARKGRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
```

Build and run:
```bash
rokade build
rokade run
```

---

## Standard Library (`std`)

Rook comes with a modular standard library installed directly with the toolchain (located at `<install_prefix>/std`). Rokade strictly resolves standard library modules from the verified installation directory, eliminating messy relative include paths.

### Available Modules
- `<std/io>`: Basic output (`println`, `print`, `eprintln`).
- `<std/math>`: Vector math (`Vec2`, `Vec3`, methods like `.add()`, `.dot()`, and math functions `clampf`, `minf`, `maxf`, `lerpf`).
- `<std/os>`: Runtime control (`panic`, `exit_with`).
- `std`: Central prelude module importing all foundational utilities.

### Example
```rook
#comprise <std/io>
#comprise <std/math>

int main() {
    println("Hello from Rook Standard Library!");
    let v1 = Vec2 { x: 3.0, y: 4.0 };
    let v2 = Vec2 { x: 1.0, y: 2.0 };
    let v3 = v1.add(v2);
    printf("Vec2 sum: (%f, %f)\n", v3.x, v3.y);
    return 0;
}
```

---

## Zed Editor Integration

Rook provides first-class integration with the [Zed](https://zed.dev) editor:
- **Language Extension**: Located at `editors/zed/` (auto-installed by `./install.sh --with-zed` to `~/.local/share/zed/extensions/installed/rook`).
- **Syntax Highlighting**: Leverages C grammar mapping for robust, instant highlighting.
- **Language Server (`rook-lsp`)**: Fully supported via stdio JSON-RPC.

Configure `~/.config/zed/settings.json`:
```json
{
  "lsp": {
    "rook-lsp": {
      "binary": {
        "path": "/home/bknsehan/bin/Rook/bin/rook-lsp"
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

---

## License

Rook is released under the [MIT License](LICENSE).
