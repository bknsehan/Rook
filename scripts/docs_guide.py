#!/usr/bin/env python3
"""
docs_guide.py
Technical reference manual for the Rook programming language and Rokade compiler (v0.5.0).
Contains Chapters 1 to 21.
"""

def get_guide_chapters(make_code_box, make_callout):
    chapters = []

    def add_ch(cid, title, content):
        chapters.append((cid, title, content))

    # ==========================================
    # Chapter 1: The Rook Philosophy & Prior Art
    # ==========================================
    ch1 = """
<p>C has been the standard language for operating systems, embedded controllers, device drivers, and real-time graphics for decades. Nearly every computing platform exposes a C Application Binary Interface (ABI), making C the universal interface for systems software.</p>

<p>At the same time, developing in C has well-known maintenance challenges and safety pitfalls:</p>
<ul>
  <li><strong>Header synchronization:</strong> Maintaining declarations in <code>.h</code> files and definitions in <code>.c</code> files requires duplicate maintenance. Inconsistencies between headers and source files lead to linker errors or mismatched symbol definitions.</li>
  <li><strong>Uninitialized memory defaults:</strong> Local variables declared without initializers contain whatever values were left on the stack. Reading them causes undefined behavior.</li>
  <li><strong>Accidental assignment in conditions:</strong> Writing <code>if (x = 5)</code> instead of <code>if (x == 5)</code> compiles silently in standard C, overwriting variables and introducing subtle logic bugs.</li>
  <li><strong>Boilerplate for common abstractions:</strong> Implementing single inheritance or tagged unions in standard C requires nested structs, manual tag checks, and void pointer casting without compiler checks.</li>
</ul>

<h3>Language Trade-Offs and Prior Art</h3>
<p>Different languages address these challenges with different design trade-offs:</p>
<ul>
  <li><strong>C:</strong> Direct hardware mapping, universal ABI compatibility, and zero runtime overhead. However, it lacks module systems, tagged unions, and basic safety guards. <em>Rook preserves C ABI compatibility and compiles directly to standard C, while replacing header files with <code>#comprise</code>, initializing variables to zero by default, and providing single inheritance and sum types.</em></li>
  <li><strong>C++:</strong> Rich abstractions, generic templates, and extensive standard libraries. However, it introduces complex grammar rules, slow compile times, and symbol name mangling that complicates C interop. <em>Rook retains the straightforward compilation model of C, adding method syntax and <code>defer</code> without virtual tables, mangled symbols, or template overhead.</em></li>
  <li><strong>Rust:</strong> Compile-time memory safety and concurrency guarantees enforced through a borrow checker. However, it has a steep learning curve and introduces friction when interfacing directly with untyped C memory layouts. <em>Rook uses traditional pointers and explicit memory management, adding targeted compiler checks to prevent common C mistakes without lifetime annotations.</em></li>
  <li><strong>Zig and Odin:</strong> Clean systems ergonomics, explicit memory allocation, and independent compiler toolchains. However, their standalone ABIs require dedicated bindings to integrate into existing C builds. <em>Rook transpiles to standard C, allowing single <code>.rook</code> files to integrate directly into existing C Makefiles and CMake projects.</em></li>
  <li><strong>Go:</strong> Fast compilation, simple syntax, and built-in concurrency. However, its garbage-collected runtime makes it unsuitable for bare-metal targets, low-latency audio, or embedded microcontrollers. <em>Rook has no garbage collector, no runtime overhead, and predictable memory layout.</em></li>
</ul>

<h3>Design Goals</h3>
<ul>
  <li><strong>1:1 C ABI compatibility:</strong> Functions, structs, and primitive types in Rook match standard C layouts, allowing direct calls to C libraries (such as SDL, Raylib, SQLite, or Vulkan) without wrapper code.</li>
  <li><strong>Headerless modules:</strong> Modules are imported using <code>#comprise</code>, eliminating separate <code>.h</code> files and header guards.</li>
  <li><strong>Safe defaults:</strong> Local variables default to zero initialization, assignments inside conditional expressions are rejected at compile time, and optional bounds checking (<code>-b</code>) checks array access at runtime.</li>
  <li><strong>Lightweight abstractions:</strong> Single inheritance (<code>object Child : Parent</code>), methods (<code>impl</code>), and tagged unions with pattern matching (<code>sum</code> and <code>match</code>) lower to clean, static C code.</li>
</ul>

<h3>Explicit Limitations</h3>
<ul>
  <li><strong>No borrow checker:</strong> Rook does not prove memory lifetimes at compile time. Memory is managed explicitly using pointers, allocators, and arenas.</li>
  <li><strong>No garbage collector:</strong> Memory allocations remain allocated until freed.</li>
  <li><strong>Early ecosystem:</strong> The standard library is small, though direct C interop allows using any existing C library.</li>
  <li><strong>LLVM backend status:</strong> Rokade has two backends: a C transpiler backend and an LLVM backend. The C backend is the primary, production-ready backend. The LLVM backend is experimental and in active development.</li>
</ul>

<h3>Common Use Cases</h3>
<ul>
  <li><strong>Command-line tools and utilities:</strong> Standalone binaries that build quickly and run without runtime dependencies.</li>
  <li><strong>Game engines and multimedia:</strong> Audio, physics, and rendering with Raylib, SDL2, OpenGL, or Vulkan.</li>
  <li><strong>Upgrading existing C projects:</strong> Replacing or adding modules written in type-safe <code>.rook</code> files within existing C codebases.</li>
</ul>
"""
    add_ch("philosophy", "1. The Rook Philosophy & Prior Art", ch1)

    # ==========================================
    # Chapter 2: Architecture & Compilation Model
    # ==========================================
    ch2 = """
<p>Rook uses a compilation pipeline managed by the <code>rokade</code> compiler, which is written in C11.</p>

<div class="arch-diagram">
  <div class="arch-box">Source Files<br><code>.rook</code></div>
  <div class="arch-arrow">➔</div>
  <div class="arch-box">Frontend<br>AST &amp; Semantic Analysis</div>
  <div class="arch-arrow">➔</div>
  <div class="arch-split">
    <div class="arch-box branch"><strong>C Backend (Default)</strong><br>Stable &amp; primary<br>Emits C to <code>build/generated/main.c</code></div>
    <div class="arch-box branch"><strong>LLVM Backend (Experimental)</strong><br>Active development<br>Object emission &amp; JIT (<code>--jit</code>)</div>
  </div>
</div>

<h3>2.1 Compiler Backends</h3>
<div class="table-container">
<table>
  <thead>
    <tr><th>Backend</th><th>Status</th><th>Description</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>C Backend (Default)</strong></td>
      <td>Primary</td>
      <td>Transpiles <code>.rook</code> files into formatted C code (C11/C23) and invokes the host C compiler (GCC or Clang) directly. Full C ABI compatibility, standard debugging with GDB or LLDB, and direct inclusion of external C headers. Recommended for regular development.</td>
    </tr>
    <tr>
      <td><strong>LLVM Backend</strong></td>
      <td>Experimental</td>
      <td>Uses LLVM libraries to generate object files (<code>.o</code>) directly and support in-memory execution (<code>rokade --jit</code>). Supports unsigned operations, pointer arithmetic, sum types, and bounds checks. In active development.</td>
    </tr>
  </tbody>
</table>
</div>

<h3>2.2 Compilation Pipeline</h3>
<ol>
  <li><strong>Lexing and Parsing:</strong> Reads source tokens and builds an Abstract Syntax Tree (AST). The <code>#comprise</code> directive recursively discovers dependencies and deduplicates imports into a single compilation unit.</li>
  <li><strong>Semantic Analysis (<code>sema.c</code>):</strong> Checks types, validates function argument counts, checks pattern exhaustiveness, enforces pointer rules, and flags prohibited syntax (such as assignments in conditions, division by zero, and <code>goto</code>).</li>
  <li><strong>Code Generation:</strong>
    <ul>
      <li><strong>C Backend:</strong> Emits formatted C source to <code>build/generated/main.c</code>. Each Rook construct maps to a standard C construct.</li>
      <li><strong>LLVM Backend:</strong> Emits LLVM IR (<code>.ll</code>) and invokes LLVM to produce object files (<code>.o</code>) or run directly in memory via JIT.</li>
    </ul>
  </li>
  <li><strong>Process Execution:</strong> Rokade executes the host compiler and linker directly using POSIX <code>fork()</code> and <code>execv()</code> (or <code>CreateProcess</code> on Windows). It avoids shell wrappers (<code>sh -c</code>) to prevent argument escaping problems and injection issues.</li>
</ol>
""" + make_callout("note", "Inspecting Generated Code", """
You can view the C code emitted by Rokade at any time by running <code>rokade --emit-c src/main.rook</code> or inspecting <code>build/generated/main.c</code>.
""") + """
<h3>2.3 C ABI Compatibility</h3>
<p>Rook adheres to the host C ABI:</p>
<ul>
  <li>Primitives (<code>int</code>, <code>size_t</code>, <code>float</code>, <code>double</code>, <code>char*</code>) have the same sizes, alignments, and calling conventions as C.</li>
  <li>Rook <code>struct</code> and <code>object</code> types have the same memory layout, field offsets, and padding as C structs.</li>
  <li>Rook functions can be called directly from C, and C functions can be called directly from Rook without glue code.</li>
</ul>
"""
    add_ch("architecture", "2. Architecture & Compilation Model", ch2)

    # ==========================================
    # Chapter 3: Direct Comparison: Standard C vs. Rook
    # ==========================================
    ch3 = """
<p>This chapter compares standard C patterns with their Rook equivalents.</p>

<h3>3.1 Modules and Imports</h3>
<p>Standard C requires declaring interfaces in <code>.h</code> header files with include guards, and writing implementations in <code>.c</code> files. Rook uses single-file modules imported with <code>#comprise</code>.</p>

<div class="table-container">
<table>
  <thead>
    <tr><th>Aspect</th><th>Standard C</th><th>Rook</th></tr>
  </thead>
  <tbody>
    <tr>
      <td>Source files</td>
      <td>Dual files: <code>math.h</code> and <code>math.c</code></td>
      <td>Single file: <code>math.rook</code></td>
    </tr>
    <tr>
      <td>Import mechanism</td>
      <td><code>#include "math.h"</code> with <code>#ifndef</code> guards</td>
      <td><code>#comprise math</code>, deduplicated automatically</td>
    </tr>
    <tr>
      <td>Signature consistency</td>
      <td>Header and source must be kept in sync manually</td>
      <td>Single definition; no duplicate declarations</td>
    </tr>
  </tbody>
</table>
</div>
""" + make_code_box("c", """
// math.h
#ifndef MATH_H
#define MATH_H
int add(int a, int b);
#endif

// math.c
#include "math.h"
int add(int a, int b) { return a + b; }
""", "Standard C: Header and Source") + make_code_box("rook", """
// src/math.rook
int add(int a, int b) {
    return a + b;
}

// src/main.rook
#comprise math
#include <stdio.h>

int main() {
    printf("Sum: %d\n", add(10, 20));
    return 0;
}
""", "Rook: Module Import") + """
<h3>3.2 Variable Initialization</h3>
<p>In standard C, uninitialized local variables contain indeterminate stack data. Reading them is undefined behavior. In Rook, uninitialized local variables are automatically set to zero.</p>

<div class="table-container">
<table>
  <thead>
    <tr><th>Declaration</th><th>Standard C</th><th>Rook</th></tr>
  </thead>
  <tbody>
    <tr>
      <td>Local variable</td>
      <td><code>int x;</code> has indeterminate value</td>
      <td><code>int x;</code> is initialized to 0 (<code>int x = {0};</code>)</td>
    </tr>
    <tr>
      <td>Array</td>
      <td><code>int arr[100];</code> contains uninitialized elements</td>
      <td><code>int arr[100];</code> is zeroed at declaration</td>
    </tr>
    <tr>
      <td>Struct</td>
      <td><code>Point p;</code> contains uninitialized fields</td>
      <td><code>Point p;</code> has all fields zeroed</td>
    </tr>
  </tbody>
</table>
</div>
""" + make_code_box("c", """
// Standard C: Undefined behavior
int health; // Uninitialized stack value
if (health > 0) { // Non-deterministic branch
    take_action();
}
""", "Standard C (Uninitialized Local)") + make_code_box("rook", """
// Rook: Deterministic initialization
int health; // Initialized to 0
if (health > 0) { // Predictable: branch is not taken
    take_action();
}
""", "Rook (Zero-Initialized)") + """
<h3>3.3 Assignment Inside Conditions</h3>
<p>In standard C, accidentally using <code>=</code> instead of <code>==</code> inside an <code>if</code> condition assigns the value and evaluates its truthiness. Rook rejects assignments in conditions at compile time.</p>
""" + make_code_box("c", """
// Standard C: Compiles silently (or with warning), overwriting status
if (status = 0) {
    handle_success();
}
""", "Standard C (Silent Mutation)") + make_code_box("rook", """
// Rook: Compile-time error
if (status = 0) { // error: assignment used as condition; did you mean '=='?
    handle_success();
}

// Correct:
if (status == 0) {
    handle_success();
}
""", "Rook (Compile Error)") + """
<h3>3.4 Methods and Single Inheritance</h3>
<p>Standard C lacks method syntax and inheritance. Structs must be nested manually, and type casting loses safety checks. Rook provides <code>object</code> and <code>impl</code> blocks, lowering to standard C structs without virtual tables.</p>
""" + make_code_box("c", """
// Standard C: Manual nesting and casts
typedef struct { float x, y; } Point;
typedef struct { Point base; float radius; } Circle;

void point_move(Point* p, float dx, float dy) { p->x += dx; p->y += dy; }

Circle c = { {10.0f, 20.0f}, 5.0f };
point_move((Point*)&c, 1.0f, 2.0f); // Explicit unverified cast
""", "Standard C: Emulated Inheritance") + make_code_box("rook", """
// Rook: Single inheritance and methods
object Point {
    float x;
    float y;
}

object Circle : Point {
    float radius;
}

impl Point {
    void move(Point* self, float dx, float dy) {
        self.x += dx;
        self.y += dy;
    }
}

Circle c = Circle{ x: 10.0f, y: 20.0f, radius: 5.0f };
c.move(1.0f, 2.0f); // Calls point_move(&c._base, 1.0f, 2.0f)
""", "Rook: Single Inheritance") + """
<h3>3.5 Tagged Unions and Pattern Matching</h3>
<p>Standard C requires manually synchronizing an enum tag with an untagged union. Rook provides <code>sum</code> types and <code>match</code> expressions.</p>
""" + make_code_box("rook", """
sum NetworkEvent {
    Connected { int client_id; const char* ip; };
    DataReceived { int client_id; int bytes; };
    Disconnected { int client_id; };
}

void handle_event(NetworkEvent* ev) {
    match (*ev) {
        Connected(c)    => printf("Client %d connected from %s\n", c.client_id, c.ip),
        DataReceived(d) => printf("Client %d sent %d bytes\n", d.client_id, d.bytes),
        Disconnected(q) => printf("Client %d disconnected\n", q.client_id),
    }
}
""", "Rook: Sum Types and Pattern Matching") + """
<h3>3.6 Scope Cleanup: goto vs. defer</h3>
<p>Cleaning up resources across multiple error exits in C often uses <code>goto cleanup;</code> ladders. Rook provides <code>defer</code>, which executes cleanup statements at scope exit in LIFO order.</p>
""" + make_code_box("c", """
// Standard C: goto cleanup ladder
int process(const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) return -1;
    char* buf = malloc(1024);
    if (!buf) { fclose(fp); return -2; }
    
    int ret = 0;
    if (do_work(fp, buf) < 0) { ret = -3; goto cleanup; }
    
cleanup:
    free(buf);
    fclose(fp);
    return ret;
}
""", "Standard C: goto Cleanup") + make_code_box("rook", """
// Rook: Scope-based defer cleanup
int process(const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) return -1;
    defer fclose(fp); // Runs on any exit path

    char* buf = (char*)malloc(1024);
    if (!buf) return -2;
    defer free(buf); // Runs before fclose(fp)

    if (do_work(fp, buf) < 0) return -3;
    return 0;
}
""", "Rook: defer Cleanup") + """
<h3>3.7 Pointer Safety</h3>
<p>Standard C allows pointer arithmetic on <code>void*</code> as a compiler extension. Rook rejects arithmetic on <code>void*</code> at compile time, requiring explicit casts to byte-oriented pointers (<code>char*</code> or <code>uint8_t*</code>).</p>

<h3>3.8 Build Configuration</h3>
<p>Rather than requiring Makefiles or CMake scripts for simple projects, Rokade reads a declarative <code>rokade.toml</code> configuration file and handles compilation directly.</p>

<div class="table-container">
<table>
  <thead>
    <tr><th>Feature</th><th>Standard C</th><th>Rook / Rokade</th></tr>
  </thead>
  <tbody>
    <tr><td>Build specification</td><td>Makefiles, CMakeLists.txt</td><td>Declarative <code>rokade.toml</code></td></tr>
    <tr><td>Process execution</td><td>Shell scripts or Make subprocesses</td><td>Direct POSIX <code>fork()</code>/<code>execv()</code></td></tr>
    <tr><td>Cross-compilation</td><td>Manual toolchain prefix configuration</td><td>Built-in <code>--target=&lt;triple&gt;</code> support</td></tr>
    <tr><td>Diagnostics</td><td>Varies by compiler</td><td>ANSI terminal output + JSON for editors</td></tr>
  </tbody>
</table>
</div>
"""
    add_ch("c-comparison", "3. Direct Comparison: Standard C vs. Rook", ch3)

    # ==========================================
    # Chapter 4: Mental Model & Memory Semantics
    # ==========================================
    ch4 = f"""
<p>Rook follows the memory model of standard C: values live on the stack or the heap, pointers represent memory addresses, and data structures have predictable layouts.</p>

<h3>4.1 Stack Memory and Zero-Initialization</h3>
<p>In standard C, uninitialized local variables contain indeterminate stack data:</p>
{make_code_box("c", """
// Standard C: Reading uninitialized stack value
void compute(void) {
    int counter;
    if (ready) counter++; // Undefined behavior
}
""", "Standard C (Uninitialized)")}

<p>In Rook, uninitialized local variables are automatically initialized to zero:</p>
{make_code_box("rook", """
int compute() {
    int counter; // Lowered to: int counter = {0};
    printf("%d\n", counter); // Prints 0
    return 0;
}
""", "Rook (Zero-Initialized)")}

<h3>4.2 Heap Allocations and defer</h3>
<p>Heap memory in Rook is explicit. Programs allocate memory using functions like <code>malloc</code>, <code>calloc</code>, or custom allocators (such as <code>std/mem</code> arenas). Rook has no garbage collector.</p>

<p>The <code>defer</code> statement ensures resources are released when exiting scope:</p>
{make_code_box("rook", """
#include <stdio.h>
#include <stdlib.h>

int process_file(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return -1;
    defer fclose(fp); // Guaranteed to execute on function return

    char* buffer = (char*)malloc(4096);
    if (!buffer) return -2;
    defer free(buffer); // Executes before fclose(fp)

    return 0;
}
""", "Deterministic Scope Cleanup")}

<h3>4.3 Object Memory Layout</h3>
<p>Rook's object inheritance embeds the parent type as the first struct member named <code>_base</code>. There are no virtual method tables (vtables), runtime type identifiers, or hidden pointer indirections. A pointer to a derived object is binary-compatible with a pointer to its base type under standard C layout rules.</p>
"""
    add_ch("mental-model", "4. Mental Model & Memory Semantics", ch4)

    # ==========================================
    # Chapter 5: Installation, Setup & Diagnostics
    # ==========================================
    ch5 = f"""
<p>Rook can be built from source and installed into your user directory without root privileges.</p>

<h3>5.1 Prerequisites</h3>
<ul>
  <li><strong>C Compiler:</strong> <code>gcc</code> (version 9 or higher) or <code>clang</code> (version 10 or higher)</li>
  <li><strong>Build Tool:</strong> <code>cmake</code> (version 3.16 or higher)</li>
  <li><strong>Language Server (optional):</strong> <code>cargo</code> (required only if building <code>rook-lsp</code>)</li>
</ul>

<h3>5.2 Installation Steps</h3>
<p><strong>Linux and macOS:</strong></p>
{make_code_box("bash", """
git clone https://github.com/bknsehan/Rook.git
cd Rook
./install.sh --prefix=$HOME/.local/rook --with-zed
""", "Linux / macOS Install")}

<p>Add the installation directory to your <code>PATH</code>:</p>
{make_code_box("bash", """
export PATH="$HOME/.local/rook/bin:$PATH"
""", "Shell Configuration")}

<p><strong>Windows (PowerShell):</strong></p>
{make_code_box("powershell", """
git clone https://github.com/bknsehan/Rook.git
cd Rook
.\\install.ps1 -WithZed
""", "Windows Install")}

<h3>5.3 Diagnostics with rokade doctor</h3>
<p>Rokade includes a diagnostics command that checks your compiler, paths, standard library, and dependencies:</p>
{make_code_box("bash", """
rokade doctor
""", "Run Diagnostics")}

<p>Sample output:</p>
{make_code_box("text", """
=== Rokade System Diagnostics ===
[PASS] Host OS: linux (x86_64)
[PASS] C Compiler: /usr/bin/gcc (GNU 13.2.0)
[PASS] Standard Library: /home/user/.local/rook/std/std.rook
[PASS] Include Jail: secure sandboxing active
[PASS] pkg-config: /usr/bin/pkg-config (found)
[PASS] LLVM Backend: enabled (LLVM 18.1.3)
[PASS] LSP Server: /home/user/.local/rook/bin/rook-lsp
11 checks passed, 0 warnings, 0 failures. System ready.
""", "rokade doctor Output")}
"""
    add_ch("installation", "5. Installation, Setup & Diagnostics", ch5)

    # ==========================================
    # Chapter 6: Project Lifecycle & Quickstart
    # ==========================================
    ch6 = f"""
<p>Rokade provides commands for creating, building, and running projects.</p>

<h3>6.1 Creating Projects</h3>
<p>To create a new executable application:</p>
{make_code_box("bash", """
rokade new my_service
cd my_service
""", "New Application")}

<p>To create a library project:</p>
{make_code_box("bash", """
rokade new --lib math_utils
""", "New Library")}

<h3>6.2 Project Layout</h3>
<p>A standard Rook project directory contains:</p>
{make_code_box("text", """
my_service/
├── rokade.toml          # Project configuration and dependencies
├── src/
│   └── main.rook        # Entry point
├── vendor/              # Vendored dependencies
└── build/               # Generated output
    ├── generated/
    │   └── main.c       # Emitted C source
    └── linux/
        └── my_service   # Executable binary
""", "Project Structure")}

<h3>6.3 Common Commands</h3>
{make_code_box("bash", """
# Build the project
rokade build

# Build and run immediately
rokade run

# Print generated C code to stdout
rokade --emit-c src/main.rook

# Run in memory with the experimental LLVM JIT
rokade run --jit
""", "CLI Workflow")}
"""
    add_ch("quickstart", "6. Project Lifecycle & Quickstart", ch6)

    # ==========================================
    # Chapter 7: Variables, Types & Invariants
    # ==========================================
    ch7 = f"""
<p>Rook is statically typed. Variables can be declared with inferred types, explicit types, or C-style syntax.</p>

<h3>7.1 Variable Declaration Forms</h3>
<p>Rook supports three declaration forms:</p>
{make_code_box("rook", """
#include <stdio.h>

int main() {
    // 1. Inferred type with 'let'
    let count = 42;             // Inferred as int
    let greeting = "Hello";     // Inferred as const char*

    // 2. Explicit type with 'let'
    let threshold: int = 100;
    let ratio: float = 3.14159f;

    // 3. C-style declaration
    int limit = 500;
    const char* user = "admin";

    printf("%s: count=%d, limit=%d\n", greeting, count, limit);
    return 0;
}
""", "Variable Declarations")}

{make_callout("ban", "Variable Declaration Syntax", """
Variables must be declared with <code>let</code> (e.g. <code>let x = 10;</code> or <code>let x: int = 10;</code>) or with C-style syntax (<code>int x = 10;</code>). Bare colon declarations without <code>let</code> (such as <code>x: int = 10;</code>) are rejected to prevent ambiguity with labels, conditionals, and struct field definitions.
""")}

<h3>7.2 Primitive Types</h3>
<div class="table-container">
<table>
  <thead>
    <tr><th>Type</th><th>Description</th><th>Size (x86_64)</th><th>C Equivalent</th></tr>
  </thead>
  <tbody>
    <tr><td><code>int</code></td><td>Signed 32-bit integer</td><td>4 bytes</td><td><code>int32_t</code> / <code>int</code></td></tr>
    <tr><td><code>unsigned int</code></td><td>Unsigned 32-bit integer</td><td>4 bytes</td><td><code>uint32_t</code> / <code>unsigned int</code></td></tr>
    <tr><td><code>short</code> / <code>long</code></td><td>Short / long integer</td><td>2 / 8 bytes</td><td><code>short</code> / <code>long</code></td></tr>
    <tr><td><code>size_t</code></td><td>Unsigned size type</td><td>8 bytes</td><td><code>size_t</code></td></tr>
    <tr><td><code>float</code></td><td>Single-precision float</td><td>4 bytes</td><td><code>float</code></td></tr>
    <tr><td><code>double</code></td><td>Double-precision float</td><td>8 bytes</td><td><code>double</code></td></tr>
    <tr><td><code>char</code></td><td>Character or byte</td><td>1 byte</td><td><code>char</code> / <code>int8_t</code></td></tr>
    <tr><td><code>const char*</code></td><td>Null-terminated string pointer</td><td>8 bytes</td><td><code>const char*</code></td></tr>
    <tr><td><code>bool</code></td><td>Boolean flag</td><td>1 byte</td><td><code>bool</code></td></tr>
    <tr><td><code>void</code></td><td>Absence of value</td><td>-</td><td><code>void</code></td></tr>
  </tbody>
</table>
</div>

<h3>7.3 Fixed-Size Arrays</h3>
<p>Array declarations match C layout. Uninitialized arrays are automatically zeroed:</p>
{make_code_box("rook", """
int values[4]; // Lowered to: int values[4] = {0};
values[0] = 10;
values[1] = 20;

// Iterating over elements with for-in
for item in [100, 200, 300] {
    printf("item: %d\n", item);
}
""", "Arrays and Iteration")}
"""
    add_ch("variables", "7. Variables, Types & Invariants", ch7)

    # ==========================================
    # Chapter 8: Pointers & Pointer Safety
    # ==========================================
    ch8 = f"""
<p>Pointers in Rook provide direct memory manipulation with compile-time checks that prevent syntax ambiguities and unsafe arithmetic.</p>

<h3>8.1 Postfix Pointer Notation</h3>
<p>Rook standardizes on postfix notation (<code>Type*</code>):</p>
{make_code_box("rook", """
int* ptr = &x;     // Postfix notation
const char* name;  // Postfix notation
""", "Pointer Syntax")}

{make_callout("ban", "Prefix Pointer Syntax Banned", """
Prefix notation like <code>*int ptr;</code> is rejected by the parser:
<pre><code>error: invalid pointer syntax '*Type'; Rook standardizes on postfix 'Type*'</code></pre>
""")}

<h3>8.2 Pointer Operations</h3>
{make_code_box("rook", """
#include <stdio.h>

void swap(int* a, int* b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

int main() {
    int x = 10;
    int y = 20;
    swap(&x, &y);
    printf("x=%d, y=%d\n", x, y); // x=20, y=10
    return 0;
}
""", "Address-of and Dereference")}

<h3>8.3 Pointer Arithmetic Rules</h3>
<p>Rook enforces specific rules on pointer calculations:</p>
<ul>
  <li><strong>Typed pointer offsets:</strong> Adding or subtracting integers from typed pointers (<code>p + 1</code>, <code>p++</code>) steps memory by <code>sizeof(*p)</code> bytes.</li>
  <li><strong>Prohibition on void* arithmetic:</strong> Arithmetic on <code>void*</code> is rejected at compile time. Cast explicitly to byte pointers (<code>char*</code> or <code>uint8_t*</code>):
{make_code_box("rook", """
void* buf = get_buffer();
// Compile error: pointer arithmetic on 'void*' is invalid
void* next = buf + 8; 

// Correct:
uint8_t* byte_ptr = (uint8_t*)buf;
uint8_t* next_byte = byte_ptr + 8;
""", "void* Arithmetic Check")}
  </li>
  <li><strong>Prohibition on non-additive operations:</strong> Multiplying, dividing, modulo-ing, or bitwise-shifting pointers is rejected at compile time:
{make_code_box("rook", """
int* p = &x;
p = p * 2;    // Compile error: invalid operand to binary operator
p = p & 0xFF; // Compile error: invalid operand to binary operator
""", "Invalid Pointer Operations")}
  </li>
</ul>
"""
    add_ch("pointers", "8. Pointers & Pointer Safety", ch8)

    # ==========================================
    # Chapter 9: Control Flow & Branching
    # ==========================================
    ch9 = f"""
<p>Rook provides standard control flow constructs with compile-time checks to prevent common logic errors.</p>

<h3>9.1 Conditionals: if and else</h3>
{make_code_box("rook", """
if (status == 200) {
    printf("OK\n");
} else if (status >= 400 && status < 500) {
    printf("Client Error\n");
} else {
    printf("Other\n");
}
""", "Conditional Branches")}

{make_callout("ban", "Assignment in Condition Banned", """
Rook's semantic analyzer rejects assignment expressions inside condition blocks:
<ul>
  <li><code>if (cond)</code></li>
  <li><code>while (cond)</code></li>
  <li><code>for (...; cond; ...)</code></li>
  <li><code>switch (cond)</code></li>
  <li>Ternary operators: <code>(cond) ? a : b</code></li>
</ul>
<pre><code>error: assignment used as condition; did you mean '=='?</code></pre>
""")}

<h3>9.2 Loops: while, do-while, for, and for-in</h3>
{make_code_box("rook", """
// 1. while loop
int i = 0;
while (i < 5) {
    i++;
}

// 2. Standard for loop
for (int idx = 0; idx < 10; idx++) {
    if (idx == 3) continue;
    if (idx == 8) break;
}

// 3. for-in loop over array
for score in [95, 88, 72, 100] {
    printf("score: %d\n", score);
}
""", "Looping Constructs")}

<h3>9.3 Scalar Branching: switch</h3>
<p>For dispatch on integer and enum values using jump tables, Rook supports standard C <code>switch</code>:</p>
{make_code_box("rook", """
switch (opcode) {
    case 1:
        execute_add();
        break;
    case 2:
        execute_sub();
        break;
    default:
        handle_unknown();
        break;
}
""", "Standard switch")}

{make_callout("note", "switch and match", """
The <code>switch</code> statement is intended for scalar values with case labels and explicit <code>break</code>. For pattern matching, destructuring, and sum types, use the <code>match</code> construct (see Chapter 13).
""")}

<h3>9.4 Prohibition of goto</h3>
<p>Rook bans the <code>goto</code> keyword. Control flow must use structured constructs (<code>if</code>, <code>while</code>, <code>for</code>, <code>switch</code>, <code>match</code>, <code>break</code>, <code>continue</code>, and <code>return</code>).</p>
"""
    add_ch("control-flow", "9. Control Flow & Branching", ch9)

    # ==========================================
    # Chapter 10: Functions & Call Semantics
    # ==========================================
    ch10 = f"""
<p>Functions in Rook follow standard C declaration syntax without extra keywords.</p>

<h3>10.1 Function Declarations</h3>
{make_code_box("rook", """
#include <stdio.h>

int multiply(int a, int b) {
    return a * b;
}

void log_message(const char* tag, const char* msg) {
    printf("[%s] %s\n", tag, msg);
}

int main() {
    let result = multiply(6, 7);
    log_message("INFO", "Calculation complete");
    return 0;
}
""", "Function Definitions")}

<h3>10.2 Compiler Validations</h3>
<ul>
  <li><strong>Return validation:</strong> Non-void functions must return a value across all control paths. A path that reaches the end of the function without returning causes a compile error.</li>
  <li><strong>Arity and type verification:</strong> Function calls are statically verified against declarations. Missing or excess arguments produce compile errors.</li>
  <li><strong>Scope rules:</strong> Functions must be declared at the file level. Nested function definitions are rejected.</li>
</ul>

<h3>10.3 External C Functions (extern)</h3>
<p>When calling C functions without including their header file, declare their signature with <code>extern</code>:</p>
{make_code_box("rook", """
extern int puts(const char* s);
extern void* memcpy(void* dest, const void* src, size_t n);

int main() {
    puts("Direct C linkage");
    return 0;
}
""", "extern Declarations")}
"""
    add_ch("functions", "10. Functions & Call Semantics", ch10)

    # ==========================================
    # Chapter 11: Structs & Memory Layout
    # ==========================================
    ch11 = f"""
<p>Structs in Rook represent plain data records that map directly to C struct layout, matching memory alignment, size, and field offsets.</p>

<h3>11.1 Definition and Initialization</h3>
<p>Fields can be declared using either C-style (<code>Type name;</code>) or let-style (<code>let name: Type;</code>):</p>
{make_code_box("rook", """
// 1. C-style field definitions
struct Vector2 {
    float x;
    float y;
};

// 2. Let-style field definitions
struct Vector2Let {
    let x: float;
    let y: float;
};

int main() {
    // Positional initialization
    Vector2 v1 = {10.0f, 20.0f};

    // Designated named initialization
    let v2 = Vector2{ y: 50.0f, x: 25.0f };

    printf("v1: (%.1f, %.1f)\n", v1.x, v1.y);
    printf("v2: (%.1f, %.1f)\n", v2.x, v2.y);
    return 0;
}
""", "Struct Definitions and Initialization")}

<h3>11.2 Field Access</h3>
{make_code_box("rook", """
Vector2 v = Vector2{ x: 1.0f, y: 2.0f };
Vector2* ptr = &v;

// Value field access uses '.'
v.x = 5.0f;

// Pointer field access uses '->'
ptr->y = 10.0f;
""", "Field Access Operators")}

{make_callout("spec", "Transpilation to Compound Literals", """
Designated initializers like <code>Vector2{ y: 50.0f, x: 25.0f }</code> transpile directly to C99 designated compound literals:
<pre><code>((Vector2){.y = 50.0f, .x = 25.0f})</code></pre>
""")}
"""
    add_ch("structs", "11. Structs & Memory Layout", ch11)

    # ==========================================
    # Chapter 12: Objects, Methods & Single Inheritance
    # ==========================================
    ch12 = f"""
<p>Rook supports single inheritance and method dispatch without virtual method tables (vtables) or runtime type information (RTTI).</p>

<h3>12.1 Defining Objects</h3>
<p>An <code>object</code> defines a record type that can inherit from another object. Fields support both C-style (<code>Type name;</code>) and let-style (<code>let name: Type;</code>):</p>
{make_code_box("rook", """
object Entity {
    int id;
    float x;
    float y;
}

// Player inherits all fields from Entity
object Player : Entity {
    int health;
    const char* username;
}
""", "Object Inheritance")}

<h3>12.2 Memory Layout</h3>
<p>When transpiling <code>object Player : Entity</code>, Rook embeds the base object as the first member named <code>_base</code>:</p>
{make_code_box("c", """
// Generated C representation
typedef struct Entity {
    int id;
    float x;
    float y;
} Entity;

typedef struct Player {
    struct Entity _base; // Embedded at offset 0
    int health;
    const char* username;
} Player;
""", "Generated C Struct")}

<p>Because <code>_base</code> is located at offset 0, a pointer to <code>Player</code> can be converted directly to a pointer to <code>Entity</code> under standard C ABI rules.</p>

<h3>12.3 Methods with impl</h3>
<p>Methods are attached to an object using an <code>impl</code> block:</p>
{make_code_box("rook", """
#include <stdio.h>

object Shape {
    float x;
    float y;
}

impl Shape {
    void translate(Shape* self, float dx, float dy) {
        self->x += dx;
        self->y += dy;
    }
}

object Circle : Shape {
    float radius;
}

impl Circle {
    void describe(self) { // 'self' infers Circle*
        printf("Circle at (%.1f, %.1f) radius %.1f\n", self.x, self.y, self.radius);
    }
}

int main() {
    let c = Circle{ x: 10.0f, y: 15.0f, radius: 5.0f };
    c.describe();

    // Call inherited method
    c.translate(5.0f, -5.0f);
    c.describe();
    return 0;
}
""", "Methods and Inheritance")}

{make_callout("tip", "Static Method Dispatch", """
Method calls like <code>c.describe()</code> lower to direct C function calls: <code>Circle_describe(&c)</code>. Inherited calls like <code>c.translate(...)</code> lower to <code>Shape_translate(&c._base, ...)</code>. Dispatch is static and resolved at compile time.
""")}
"""
    add_ch("objects", "12. Objects, Methods & Single Inheritance", ch12)

    # ==========================================
    # Chapter 13: Algebraic Data Types: sum, enum & match
    # ==========================================
    ch13 = f"""
<p>Rook provides <code>sum</code> types (tagged unions) and pattern matching via <code>match</code> for data that can take one of multiple variant shapes.</p>

<h3>13.1 Enumerations: enum</h3>
{make_code_box("rook", """
enum HttpMethod {
    Get,
    Post,
    Put,
    Delete
}
""", "Enum Declaration")}

<h3>13.2 Sum Types (Tagged Unions)</h3>
<p>A <code>sum</code> type defines a value that can hold one of several variant shapes. Variant payloads support both C-style (<code>Type name;</code>) and let-style (<code>let name: Type;</code>):</p>
{make_code_box("rook", """
sum Node {
    Leaf { int value; };
    Branch { Node* left; Node* right; };
    Empty;
}
""", "Sum Type Declaration")}

<p>In memory, Rokade lowers a <code>sum</code> type into a C struct containing an integer discriminator tag and an anonymous union for variant payloads:</p>
{make_code_box("c", """
typedef struct Node {
    int tag; // Identifies active variant
    union {
        struct { int value; } Leaf;
        struct { struct Node* left; struct Node* right; } Branch;
    };
} Node;
""", "Lowered C Tagged Union")}

<h3>13.3 Pattern Matching with match</h3>
<p>The <code>match</code> construct provides destructuring for sum types, enums, and scalar values in both statement and expression forms.</p>

<h4>Statement Form:</h4>
{make_code_box("rook", """
void print_node(Node* n) {
    match (*n) {
        Leaf { value }         => printf("Leaf: %d\n", value),
        Branch { left, right } => printf("Branch\n"),
        Empty                  => printf("Empty\n"),
        _                      => printf("Unknown\n")
    }
}
""", "Statement match")}

<h4>Expression Form:</h4>
{make_code_box("rook", """
sum Shape {
    Circle { float radius; };
    Rectangle { float width; float height; };
    Point;
}

float calculate_area(Shape s) {
    return match (s) {
        Circle { radius }        => 3.14159f * radius * radius,
        Rectangle { width, height } => width * height,
        Point                    => 0.0f,
        _                        => 0.0f
    };
}
""", "Expression match")}

<h3>13.4 Instantiating Sum Types</h3>
{make_code_box("rook", """
#include <stdio.h>

sum Shape {
    Circle { float r; };
    Rect { float w; float h; };
    Point;
}

impl Shape {
    float area(Shape* self) {
        return match (*self) {
            Circle { r }  => 3.14159f * r * r,
            Rect { w, h } => w * h,
            Point         => 0.0f,
            _             => 0.0f,
        };
    }
}

int main() {
    Shape a = Circle{ r: 2.0f };
    Shape b = Rect{ w: 3.0f, h: 4.0f };
    Shape c = Point;

    printf("Circle area: %f\n", a.area());
    printf("Rect area:   %f\n", b.area());
    printf("Point area:  %f\n", c.area());
    return 0;
}
""", "Instantiating Sum Variants")}
"""
    add_ch("sum-match", "13. Algebraic Data Types: sum, enum & match", ch13)

    # ==========================================
    # Chapter 14: Deterministic Cleanup: defer
    # ==========================================
    ch14 = f"""
<p>The <code>defer</code> statement schedules cleanup code to execute at the end of the enclosing lexical scope.</p>

<h3>14.1 Execution Order</h3>
<p>Multiple deferred statements execute in Last-In, First-Out (LIFO) order:</p>
{make_code_box("rook", """
#include <stdio.h>

void trace_lifo() {
    defer printf("1 (runs last)\n");
    defer printf("2 (runs middle)\n");
    printf("Body execution\n");
    defer printf("3 (runs first)\n");
}

int main() {
    trace_lifo();
    return 0;
}
""", "LIFO Execution")}

<p>Output:</p>
{make_code_box("text", """
Body execution
3 (runs first)
2 (runs middle)
1 (runs last)
""", "Execution Output")}

<h3>14.2 Managing Resources with defer</h3>
<p>Place <code>defer</code> immediately after acquiring a resource to ensure cleanup regardless of exit path:</p>
{make_code_box("rook", """
#include <stdio.h>
#include <stdlib.h>

int process_data(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) return -1;
    defer fclose(f); // Guaranteed close

    int* data = (int*)malloc(1000 * sizeof(int));
    if (!data) return -2;
    defer free(data); // Guaranteed free

    return 0;
}
""", "Resource Management with defer")}
"""
    add_ch("cleanup-defer", "14. Deterministic Cleanup: defer", ch14)

    # ==========================================
    # Chapter 15: Modules, Namespaces & Directives
    # ==========================================
    ch15 = f"""
<p>Rook eliminates C header files (<code>.h</code>) for Rook code while maintaining clear rules for importing external C libraries.</p>

<h3>15.1 Directives Overview</h3>
<div class="table-container">
<table>
  <thead>
    <tr><th>Directive</th><th>Usage</th><th>Behavior</th></tr>
  </thead>
  <tbody>
    <tr><td><code>#comprise module</code></td><td>Import local Rook source file</td><td>Parsed into the AST; diamond dependencies deduplicated automatically.</td></tr>
    <tr><td><code>#comprise &lt;std/io&gt;</code></td><td>Import Rook standard library module</td><td>Resolves against the standard library path; deduplicated.</td></tr>
    <tr><td><code>#include &lt;header.h&gt;</code></td><td>Import system C header</td><td>Passed through to generated C output.</td></tr>
    <tr><td><code>#include "header.h"</code></td><td>Import project C header</td><td>Validated against sandbox boundaries.</td></tr>
  </tbody>
</table>
</div>

<h3>15.2 Multi-File Modules</h3>
{make_code_box("rook", """
// src/math.rook
int add(int a, int b) {
    return a + b;
}

int sub(int a, int b) {
    return a - b;
}
""", "src/math.rook")}

{make_code_box("rook", """
// src/main.rook
#include <stdio.h>
#comprise math // Imports src/math.rook

int main() {
    printf("add: %d\n", add(10, 5));
    return 0;
}
""", "src/main.rook")}

{make_callout("tip", "Automatic Deduplication", """
Unlike C where headers require include guards, <code>#comprise</code> deduplicates modules automatically. If multiple modules comprise the same source file, it is parsed and compiled once.
""")}

<h3>15.3 Include Sandboxing</h3>
<p>To prevent directory traversal during compilation, Rokade enforces path validation on inclusion directives:</p>
<ul>
  <li>Paths must reside within the project root, the standard library installation, vendored directories, or system include directories.</li>
  <li>Relative traversal attempts (such as <code>#include "../../../etc/passwd"</code>) are rejected at compile time.</li>
</ul>
"""
    add_ch("modules", "15. Modules, Namespaces & Directives", ch15)

    # ==========================================
    # Chapter 16: Vendored Dependencies & Packages
    # ==========================================
    ch16 = f"""
<p>Rokade manages project dependencies through the <code>vendor/</code> directory and <code>rokade.toml</code>.</p>

<h3>16.1 Declaring Dependencies</h3>
<p>Declare local path dependencies in the <code>[dependencies]</code> table of <code>rokade.toml</code>:</p>
{make_code_box("toml", """
[package]
name = "my_game"
version = "0.1.0"

[build]
kind = "exe"
targets = ["linux"]

[dependencies]
engine = { path = "vendor/engine" }
physics = { path = "vendor/physics" }
""", "Dependencies in rokade.toml")}

<h3>16.2 The vendor/ Directory</h3>
<p>Vendored dependencies are self-contained Rook packages placed in <code>vendor/</code>:</p>
{make_code_box("text", """
my_game/
├── rokade.toml
├── src/
│   └── main.rook
└── vendor/
    └── engine/
        ├── rokade.toml
        └── src/
            └── engine.rook
""", "Vendored Project Layout")}

<h3>16.3 Dependency Resolution</h3>
<p>When compiling, Rokade traverses the dependency graph:</p>
<ol>
  <li>Inspects declared dependencies in <code>rokade.toml</code>.</li>
  <li>Parses each dependency's configuration.</li>
  <li>Resolves transitive dependencies recursively.</li>
  <li>Passes include paths and source files to the compiler command.</li>
</ol>

<p>In source code, comprise the dependency by name:</p>
{make_code_box("rook", """
#comprise engine

int main() {
    engine_init();
    return 0;
}
""", "Comprising Vendored Libraries")}
"""
    add_ch("dependencies", "16. Vendored Dependencies & Packages", ch16)

    # ==========================================
    # Chapter 17: C Interoperability & Integration
    # ==========================================
    ch17 = f"""
<p>Rook is designed for direct interoperability with C. Because Rook code transpiles to standard C with identical memory layouts and calling conventions, you can call C libraries directly without foreign function interface (FFI) bindings or wrapper code.</p>

<h3>17.1 Integrating System Libraries via pkg-config</h3>
<p>Specify system libraries in the <code>pkg-config</code> array of <code>rokade.toml</code>:</p>
{make_code_box("toml", """
[package]
name = "raylib_demo"
version = "0.1.0"

[build]
kind = "exe"
pkg-config = ["raylib"]
""", "rokade.toml with pkg-config")}

<p>Rokade calls <code>pkg-config</code> to discover the compiler and linker flags for the library.</p>

<h3>17.2 Raylib Example</h3>
{make_code_box("rook", """
#include <stdio.h>
#include <raylib.h>

int main() {
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "Rook + Raylib Demo");
    defer CloseWindow();

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);

        DrawText("Welcome to Rook systems programming!", 160, 200, 20, DARKGRAY);
        DrawFPS(10, 10);

        EndDrawing();
    }

    return 0;
}
""", "Raylib Window in Rook")}

<h3>17.3 Embedding Raw C Declarations</h3>
<p>You can embed C declarations, typedefs, and inline helper functions directly in a <code>.rook</code> file:</p>
{make_code_box("rook", """
typedef struct {
    int code;
    char detail[64];
} ErrorContext;

int calculate_hash(const char* str) {
    int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}
""", "Direct C Declarations")}
"""
    add_ch("c-interop", "17. C Interoperability & Integration", ch17)

    # ==========================================
    # Chapter 18: Standard Library Reference (std/)
    # ==========================================
    ch18 = f"""
<p>Rook includes a small standard library in the <code>std/</code> directory. Modules can be imported individually via <code>#comprise &lt;std/name&gt;</code> or together via <code>#comprise &lt;std&gt;</code>.</p>

<h3>18.1 std/io: Console Output</h3>
<p>Import with <code>#comprise &lt;std/io&gt;</code>:</p>
{make_code_box("rook", """
println("Standard output with newline");
print("Standard output without newline");
eprintln("Standard error message");
""", "std/io Usage")}

<h3>18.2 std/str: String Slices</h3>
<p>Import with <code>#comprise &lt;std/str&gt;</code>. <code>Str</code> is a non-owning string view containing a pointer and a length (<code>const char* data; size_t len;</code>):</p>
<div class="table-container">
<table>
  <thead>
    <tr><th>Function / Method</th><th>Description</th></tr>
  </thead>
  <tbody>
    <tr><td><code>str_from_cstr(s)</code></td><td>Creates a <code>Str</code> slice from a null-terminated C string.</td></tr>
    <tr><td><code>s.is_empty()</code></td><td>Returns true if <code>s.len == 0</code>.</td></tr>
    <tr><td><code>s.slice(start, len)</code></td><td>Returns a subslice without allocating new memory.</td></tr>
    <tr><td><code>s.equals(other)</code></td><td>Returns true if both string slices match in content.</td></tr>
    <tr><td><code>s.starts_with(prefix)</code></td><td>Checks for matching prefix.</td></tr>
    <tr><td><code>s.ends_with(suffix)</code></td><td>Checks for matching suffix.</td></tr>
    <tr><td><code>s.trim()</code></td><td>Returns a subslice with leading and trailing whitespace stripped.</td></tr>
    <tr><td><code>s.find(sub)</code></td><td>Returns byte offset of substring, or -1 if not found.</td></tr>
    <tr><td><code>s.contains(sub)</code></td><td>Returns true if substring is present.</td></tr>
    <tr><td><code>s.to_int()</code></td><td>Parses ASCII digits into an integer.</td></tr>
    <tr><td><code>s.to_cstr(buf, cap)</code></td><td>Copies string slice into buffer and null-terminates.</td></tr>
  </tbody>
</table>
</div>

<h3>18.3 std/vec: Dynamic Vector &amp; StringBuilder</h3>
<p>Import with <code>#comprise &lt;std/vec&gt;</code>:</p>
{make_code_box("rook", """
// Dynamic Vector of pointers
Vec v = vec_new(8);
defer v.destroy();

v.push(&item1);
v.push(&item2);
void* val = v.pop();

// String builder
StringBuilder sb = sb_new(64);
defer sb.destroy();

sb.append_cstr("Latency: ");
sb.append_int(42);
sb.append_cstr(" ms\n");
printf("%s", sb.to_cstr());
""", "std/vec Usage")}

<h3>18.4 std/mem: Arena Allocator</h3>
<p>Import with <code>#comprise &lt;std/mem&gt;</code>. The <code>Arena</code> allocator allocates sequentially from a contiguous memory block, allowing batch deallocation with <code>reset()</code> or <code>destroy()</code>:</p>
{make_code_box("rook", """
Arena arena = arena_new(65536); // 64 KB memory pool
defer arena.destroy();

void* block1 = arena.alloc(128);
void* block2 = arena.alloc(512);

arena.reset(); // Resets allocation offset
""", "Arena Allocator Usage")}

<h3>18.5 std/result: Result and Option Types</h3>
<p>Import with <code>#comprise &lt;std/result&gt;</code>:</p>
{make_code_box("rook", """
Result r = result_ok(payload);
if (r.is_ok()) {
    void* data = r.unwrap();
} else {
    const char* err = r.unwrap_err();
}

Option opt = option_some(item);
if (opt.is_some()) {
    void* val = opt.unwrap();
}
""", "std/result Usage")}

<h3>18.6 Additional Modules</h3>
<ul>
  <li><strong>std/fs:</strong> File operations (<code>fs_read_to_string</code>, <code>fs_write_file</code>, <code>fs_exists</code>) and path utilities (<code>path_basename</code>, <code>path_dirname</code>, <code>path_extension</code>).</li>
  <li><strong>std/os:</strong> System utilities (<code>os_getenv</code>, <code>os_setenv</code>, <code>os_time_ms</code>, <code>os_sleep_ms</code>, <code>panic</code>, <code>exit_with</code>).</li>
  <li><strong>std/math:</strong> Vector math (<code>Vec2</code>, <code>Vec3</code>), rectangles (<code>Rect</code>), clamping, and interpolation (<code>lerpf</code>).</li>
</ul>
"""
    add_ch("stdlib", "18. Standard Library Reference (std/)", ch18)

    # ==========================================
    # Chapter 19: Safety Invariants & Compile-Time Checks
    # ==========================================
    ch19 = f"""
<p>Rook checks source code at compile time to eliminate common classes of undefined behavior and bugs found in C.</p>

<div class="table-container">
<table>
  <thead>
    <tr><th>Check</th><th>Standard C Behavior</th><th>Rook Behavior</th><th>Rationale</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>Assignment in condition</strong></td>
      <td>Silently assigns variable and tests truthiness (<code>if (x = 5)</code>).</td>
      <td>Compile error (including with parentheses: <code>if ((x = 5))</code>)</td>
      <td>Almost always a typo for <code>==</code> that introduces logic bugs.</td>
    </tr>
    <tr>
      <td><strong>Uninitialized locals</strong></td>
      <td>Contains arbitrary stack data (undefined behavior).</td>
      <td>Initialized to zero (<code>= {0}</code>)</td>
      <td>Prevents non-deterministic bugs and uninitialized memory reads.</td>
    </tr>
    <tr>
      <td><strong>void* pointer arithmetic</strong></td>
      <td>Compiler extension or undefined behavior.</td>
      <td>Compile error</td>
      <td>The size of <code>void</code> is not defined in standard C; cast to <code>char*</code> or <code>uint8_t*</code>.</td>
    </tr>
    <tr>
      <td><strong>Invalid pointer operations</strong></td>
      <td>Permitted in some expressions (e.g. <code>ptr * 2</code>).</td>
      <td>Compile error</td>
      <td>Multiplication, division, modulo, and bitwise operations have no meaning on addresses.</td>
    </tr>
    <tr>
      <td><strong>Pointer syntax ambiguity</strong></td>
      <td>Permits multiple formats (<code>int *p</code>, <code>int* p</code>).</td>
      <td>Enforces postfix (<code>int* p</code>; bans <code>*int p</code>)</td>
      <td>Ensures consistent parsing.</td>
    </tr>
    <tr>
      <td><strong>Division by zero</strong></td>
      <td>Hardware fault (SIGFPE) or undefined behavior.</td>
      <td>Compile error for literal zero divisors (e.g. <code>x / 0</code>)</td>
      <td>Catches obvious zero division at compile time.</td>
    </tr>
    <tr>
      <td><strong>Arbitrary jumps (goto)</strong></td>
      <td>Permitted within function scope.</td>
      <td>Banned</td>
      <td>Ensures structured control flow and deterministic cleanup for <code>defer</code>.</td>
    </tr>
    <tr>
      <td><strong>Comma operator in expressions</strong></td>
      <td>Discards first expression and evaluates second.</td>
      <td>Banned in expression context</td>
      <td>Reserves commas for argument and element separators.</td>
    </tr>
    <tr>
      <td><strong>Include jail violation</strong></td>
      <td>Allows arbitrary path traversal (e.g. <code>#include "../../../etc/passwd"</code>).</td>
      <td>Compile error</td>
      <td>Restricts file inclusion to the project tree, standard library, and dependencies.</td>
    </tr>
  </tbody>
</table>
</div>

<h3>19.1 Compiler Diagnostic Examples</h3>
{make_code_box("rook", """
// 1. Assignment in condition
if ((x = 5)) { }
// error: assignment used as condition; did you mean '=='?

// 2. Prefix pointer syntax
*int ptr;
// error: invalid pointer syntax '*Type'; Rook standardizes on postfix 'Type*'

// 3. void* pointer arithmetic
void* buffer = get_raw();
void* next = buffer + 4;
// error: pointer arithmetic on 'void*' is invalid

// 4. Invalid pointer operations
int* p = &value;
p = p * 2;
// error: invalid operand to binary operator

// 5. Literal division by zero
int a = 10 / 0;
// error: division or modulo by zero

// 6. goto keyword
goto exit_label;
// error: 'goto' is not supported in Rook

// 7. Non-void function missing return
int calculate(int x) {
    if (x > 0) return x * 2;
    // Missing return on else branch
}
// error: control reaches end of non-void function
""", "Compiler Diagnostics")}
"""
    add_ch("safety", "19. Safety Invariants & Compile-Time Checks", ch19)

    # ==========================================
    # Chapter 20: Configuration & Multi-Target Builds (rokade.toml)
    # ==========================================
    ch20 = f"""
<p>Projects are configured using a declarative <code>rokade.toml</code> file.</p>

<h3>20.1 Schema Example</h3>
{make_code_box("toml", """
[package]
name = "service_engine"
version = "0.5.0"
authors = ["Engineering Team <dev@example.com>"]
description = "Telemetry daemon"

[build]
kind = "exe"                   # "exe", "shared-lib", or "static-lib"
standard = "c2x"               # "c11", "c17", "c2x", "gnu23"
backend = "c"                  # "c" (default) or "llvm"
targets = ["linux", "windows"] # Active targets
pkg-config = ["sqlite3"]       # System library dependencies

[dependencies]
network_core = { path = "vendor/network_core" }

[target.linux]
kind = "exe"
cflags = "-O3 -march=native"

[target.windows]
kind = "exe"
cflags = "-O2"

[target.android]
kind = "shared-lib"
api = 24
arch = ["arm64-v8a", "x86_64"]
cflags = "-fPIC -O3"
""", "rokade.toml Schema")}

<h3>20.2 Cross-Compilation Targets</h3>
<p>Rokade supports cross-compilation targets:</p>
<ul>
  <li><strong>Linux:</strong> Compiles with the host <code>gcc</code> or <code>clang</code>.</li>
  <li><strong>Windows:</strong> Cross-compiles using MinGW (<code>x86_64-w64-mingw32-gcc</code>), producing a Windows <code>.exe</code>.</li>
  <li><strong>Android:</strong> Uses installed Android NDK toolchains to compile shared libraries (<code>.so</code>) for specified architectures (e.g. <code>arm64-v8a</code>, <code>x86_64</code>).</li>
</ul>

{make_code_box("bash", """
# Build Windows target
rokade build --target=windows

# Build Android target
rokade build --target=android

# Build all configured targets
rokade build --all
""", "Target Build Commands")}
"""
    add_ch("config", "20. Configuration & Multi-Target Builds (rokade.toml)", ch20)

    # ==========================================
    # Chapter 21: Compiler CLI & Architecture Reference
    # ==========================================
    ch21 = f"""
<p>The <code>rokade</code> executable is a command-line tool that handles project scaffolding, compilation, testing, and diagnostics.</p>

<h3>21.1 Command-Line Reference</h3>
<div class="table-container">
<table>
  <thead>
    <tr><th>Command / Option</th><th>Description</th></tr>
  </thead>
  <tbody>
    <tr><td><code>rokade new [--lib] &lt;name&gt;</code></td><td>Creates a new application or library project.</td></tr>
    <tr><td><code>rokade build [--all] [--target=T]</code></td><td>Builds the project according to <code>rokade.toml</code>.</td></tr>
    <tr><td><code>rokade run [--jit] [file.rook]</code></td><td>Compiles and runs the project or a single file. <code>--jit</code> runs in memory.</td></tr>
    <tr><td><code>rokade doctor</code></td><td>Runs environment and toolchain checks.</td></tr>
    <tr><td><code>rokade toolchain</code></td><td>Displays detected C compiler, version, and default flags.</td></tr>
    <tr><td><code>rokade --emit-c &lt;file.rook&gt;</code></td><td>Transpiles file and prints C code to stdout.</td></tr>
    <tr><td><code>rokade --emit-llvm &lt;file.rook&gt;</code></td><td>Emits LLVM Intermediate Representation (LLVM IR).</td></tr>
    <tr><td><code>rokade --emit-obj &lt;file.rook&gt;</code></td><td>Compiles directly to native object file (<code>.o</code>) via LLVM.</td></tr>
    <tr><td><code>rokade --ast &lt;file.rook&gt;</code></td><td>Prints AST for debugging.</td></tr>
    <tr><td><code>rokade --check &lt;file.rook&gt;</code></td><td>Performs parser verification.</td></tr>
    <tr><td><code>rokade --check-dir &lt;dir&gt;</code></td><td>Validates all <code>.rook</code> files within a directory.</td></tr>
    <tr><td><code>rokade --diagnostics &lt;file.rook&gt;</code></td><td>Emits JSON diagnostics for editor integration.</td></tr>
    <tr><td><code>rokade --def-at &lt;file&gt; &lt;line&gt; &lt;col&gt;</code></td><td>Finds symbol definition location.</td></tr>
    <tr><td><code>rokade --symbols &lt;file.rook&gt;</code></td><td>Emits JSON list of top-level functions, structs, and objects.</td></tr>
  </tbody>
</table>
</div>

<h3>21.2 Process Execution Model</h3>
<p>When invoking compilers and linkers, Rokade avoids shell execution (such as <code>system()</code> or <code>popen("sh -c ...")</code>):</p>
<ul>
  <li>Commands and arguments are passed directly as structured argument arrays (<code>char* argv[]</code>) to POSIX <code>fork()</code> and <code>execv()</code>.</li>
  <li>Shell metacharacters in flags or paths are treated as literal strings and cannot execute shell commands.</li>
  <li>Process return codes are checked directly, aborting on non-zero exit codes.</li>
</ul>
"""
    add_ch("cli", "21. Compiler CLI & Architecture Reference", ch21)

    return chapters

if __name__ == "__main__":
    def dummy_box(lang, code, title=""): return f"[BOX {lang} {title}]"
    def dummy_call(kind, title, body): return f"[CALL {kind} {title}]"
    chs = get_guide_chapters(dummy_box, dummy_call)
    print(f"Loaded {len(chs)} guide chapters successfully!")
