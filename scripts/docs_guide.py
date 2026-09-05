#!/usr/bin/env python3
"""
docs_guide.py
Technical reference manual for the Rook programming language and Rokade compiler (v0.4.2).
Contains Chapters 1 to 21.
"""

def get_guide_chapters(make_code_box, make_callout):
    chapters = []

    def add_ch(cid, title, content):
        chapters.append((cid, title, content))

    # ==========================================
    # Chapter 1: The Rook Philosophy & Credits
    # ==========================================
    ch1 = """
<p>The systems programming landscape is one of the most foundational and demanding areas of software engineering. For over fifty years, <strong>C</strong> has been the bedrock of computing: operating systems, kernel drivers, real-time engines, audio processors, language runtimes, and embedded microcontrollers are almost universally exposed through the C Application Binary Interface (ABI). If software must interface directly with hardware, kernels, or existing shared libraries, C is the inescapable lingua franca.</p>

<p>Yet programming directly in standard C carries well-known ergonomic and reliability challenges that demand intense vigilance from developers:</p>
<ul>
  <li><strong>Header Synchronization Overhead:</strong> Manually synchronizing declarations in <code>.h</code> header files with definitions in <code>.c</code> files slows development and creates fragile dependencies across large codebases.</li>
  <li><strong>Uninitialized Memory &amp; Undefined Behavior:</strong> In standard C, uninitialized local variables contain unpredictable stack garbage, accidental assignment inside conditional statements (<code>if (x = 5)</code>) silently corrupts state, and unchecked pointer arithmetic introduces exploitable security vulnerabilities.</li>
  <li><strong>Boilerplate for Basic Encapsulation:</strong> C lacks native mechanisms for method dispatch, single inheritance, and tagged unions. Emulating these patterns requires nested structs, manual pointer casts, and fragile naming conventions.</li>
</ul>

<h3>Standing on the Shoulders of Giants: Prior Art &amp; Credits</h3>
<p>Modern software engineering has been profoundly enriched by exceptional programming languages, each advancing the art of systems development in monumental ways:</p>
<ul>
  <li><strong>C (Dennis Ritchie &amp; Bell Labs):</strong> The foundation upon which modern computing was built. C's elegance, hardware transparency, and universal ABI remain unparalleled. Rook owes its entire existence and memory model to the foundational genius of C.</li>
  <li><strong>C++ (Bjarne Stroustrup):</strong> Pioneered modern systems abstractions, introducing Resource Acquisition Is Initialization (RAII), zero-overhead object modeling, and powerful generic programming that continue to power high-performance game engines, web browsers, and scientific computing worldwide.</li>
  <li><strong>Rust (Graydon Hoare &amp; The Rust Foundation):</strong> Achieved a historic breakthrough in software reliability by proving that compile-time borrow checking, affine types, and fearless concurrency can eliminate entire categories of memory safety bugs without needing a garbage collector.</li>
  <li><strong>Zig (Andrew Kelley) &amp; Odin (Ginger Bill):</strong> Invaluable inspirations in pragmatic systems programming, proving that systems development can be clean, joyful, and devoid of hidden control flow, championing explicit memory allocation, modern compilation toolchains, and refreshing syntactic clarity.</li>
  <li><strong>Go (Robert Griesemer, Rob Pike, Ken Thompson):</strong> Demonstrated the profound engineering value of simplicity, fast compilation speeds, clean standard tooling, and pragmatic language design that prioritizes human readability.</li>
</ul>

<h3>The Modest Scope of Rook</h3>
<p>Rook does <strong>not</strong> seek to replace or compete with these monumental languages. Rook has a very narrow, humble mission:</p>
""" + make_callout("spec", "The Rook Thesis", """
<strong>For developers who specifically want or need to work within the C ecosystem</strong>, Rook provides a lightweight, header-less C dialect that transpiles directly to clean, standard C99/C11/C23 code. It adds a small set of practical guardrails—guaranteed zero-initialization, single-file module imports, zero-overhead single inheritance, first-class algebraic sum types, and deterministic <code>defer</code> cleanup—without introducing a heavy runtime, garbage collection, or breaking 1:1 C ABI interoperability.
""") + """
<h3>Target Use Cases</h3>
<ul>
  <li><strong>Systems &amp; Embedded Tools:</strong> Compilers, command-line utilities, daemons, and embedded firmware where runtime overhead, garbage collection pauses, and hidden allocations are unacceptable.</li>
  <li><strong>Game Engines &amp; Graphics:</strong> Native integration with C libraries like Raylib, SDL2, OpenGL, or Vulkan without FFI overhead.</li>
  <li><strong>Modernizing Legacy C Codebases:</strong> Replace brittle C files with modular <code>.rook</code> units that seamlessly link with existing C object files and Makefiles.</li>
</ul>
"""
    add_ch("philosophy", "1. The Rook Philosophy & Credits", ch1)

    # ==========================================
    # Chapter 2: Architecture & Compilation Model
    # ==========================================
    ch2 = """
<p>Rook is designed around a transparent, pragmatic compilation pipeline managed by the <code>rokade</code> compiler, written entirely in portable C11.</p>

<div class="arch-diagram">
  <div class="arch-box">Source Files<br><code>.rook</code></div>
  <div class="arch-arrow">➔</div>
  <div class="arch-box">AST &amp; Sema Checks<br><code>rokade</code> frontend</div>
  <div class="arch-arrow">➔</div>
  <div class="arch-split">
    <div class="arch-box branch"><strong>C Backend (Default &amp; Primary)</strong><br>Stable &amp; Battle-Tested<br>Emits clean C to <code>build/generated/main.c</code></div>
    <div class="arch-box branch"><strong>LLVM Backend (Experimental)</strong><br>Active Development<br>Direct <code>.o</code> emission &amp; JIT (<code>--jit</code>)</div>
  </div>
</div>

<h3>2.1 Compiler Backends: Pragmatic Realism</h3>
<p>Rokade provides two distinct code generation backends, designed with clear real-world trade-offs in mind:</p>

<div class="table-container">
<table>
  <thead>
    <tr><th>Backend</th><th>Status</th><th>Strengths &amp; Recommended Use</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>C Backend (Default)</strong></td>
      <td><span class="badge highlight">Primary &amp; Production-Ready</span></td>
      <td>Transpiles <code>.rook</code> code into clean, formatted C99/C11/C23 and invokes host GCC or Clang directly via <code>fork()</code>/<code>execv()</code>. 100% C ABI parity, seamless debugging with standard tools (GDB, LLDB, Valgrind), and zero friction with external C libraries. <strong>Recommended for all real-world development.</strong></td>
    </tr>
    <tr>
      <td><strong>LLVM Backend</strong></td>
      <td><span class="badge" style="background:#fef3c7; border-color:#f59e0b; color:#b45309;">Experimental (WIP)</span></td>
      <td>Leverages LLVM libraries to emit object files (<code>.o</code>) directly and provide in-memory JIT execution (<code>rokade --jit</code>). Supports unsigned math, pointer arithmetic, sum types, and bounds checking. <strong>Currently experimental and actively evolving; not yet as mature, robust, or battle-tested as the C backend.</strong></td>
    </tr>
  </tbody>
</table>
</div>

<h3>2.2 The Lowering Pipeline</h3>
<ol>
  <li><strong>Lexing &amp; Parsing:</strong> Tokenizes source files and constructs an Abstract Syntax Tree (AST). Recursive module inclusion (<code>#comprise</code>) resolves and deduplicates file imports into a unified compilation unit.</li>
  <li><strong>Semantic Analysis (<code>sema.c</code>):</strong> Validates type safety, verifies function arity, checks branch exhaustiveness, enforces pointer arithmetic rules, and rejects banned constructs (e.g. assignments in conditions, <code>goto</code>, division by zero).</li>
  <li><strong>Code Generation:</strong>
    <ul>
      <li><strong>C Backend (Default):</strong> Emits highly readable, formatted C99/C11/C2x code into <code>build/generated/main.c</code>. Every Rook construct maps deterministically to a standard C counterpart.</li>
      <li><strong>LLVM Backend:</strong> Emits LLVM Intermediate Representation (<code>.ll</code>) and drives LLVM target code generators to emit native machine object files (<code>.o</code>) or execute directly via an in-process JIT engine.</li>
    </ul>
  </li>
  <li><strong>Zero-Shell Direct Toolchain Execution:</strong> Rokade invokes the host C compiler (<code>gcc</code> or <code>clang</code>) and linker directly using POSIX <code>fork()</code>/<code>execv()</code> (or <code>CreateProcess</code> on Windows). It avoids shell wrappers (<code>sh -c</code>), preventing command-injection attacks and ensuring exact argument propagation.</li>
</ol>
""" + make_callout("note", "Transparent Output Inspection", """
Because Rokade's primary backend generates clean C in <code>build/generated/main.c</code>, you can inspect the generated code at any time with <code>rokade --emit-c src/main.rook</code>. There are no proprietary bytecode formats or obfuscated runtime libraries.
""") + """
<h3>2.3 ABI Parity with C</h3>
<p>Rook guarantees 1:1 ABI parity with the host C compiler:</p>
<ul>
  <li>Primitive types (<code>int</code>, <code>size_t</code>, <code>float</code>, <code>double</code>, <code>char*</code>) have identical size, alignment, and calling conventions.</li>
  <li>Rook <code>struct</code> and <code>object</code> types match standard C struct memory layout with identical field offsets and padding.</li>
  <li>Functions defined in Rook can be called directly from C, and C functions can be called directly from Rook without wrappers or marshaling.</li>
</ul>
"""
    add_ch("architecture", "2. Architecture & Compilation Model", ch2)

    # ==========================================
    # Chapter 3: Direct Comparison: Standard C vs. Rook
    # ==========================================
    ch3 = """
<p>For systems engineers and C programmers evaluating Rook, this chapter provides a direct, comprehensive comparison between standard C and Rook across eight essential architectural and day-to-day programming patterns. Rook is designed to feel immediately natural to C developers while eliminating common failure modes at compile time.</p>

<h3>3.1 Modular Code vs. Header Synchronization</h3>
<p>In standard C, sharing code between multiple files requires manually maintaining separate header declarations (<code>.h</code>) with preprocessor include guards, and source definitions (<code>.c</code>). Rook uses single-file modules imported with <code>#comprise</code>; the compiler extracts declarations and prevents symbol collisions automatically.</p>

<div class="table-container">
<table>
  <thead>
    <tr><th>Feature Dimension</th><th>Standard C</th><th>Rook</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>Source Organization</strong></td>
      <td>Dual-file maintenance: <code>math.h</code> (declarations) and <code>math.c</code> (definitions).</td>
      <td>Single-file modules: <code>math.rook</code> contains both interface and implementation.</td>
    </tr>
    <tr>
      <td><strong>Import Semantics</strong></td>
      <td><code>#include "math.h"</code> textually pastes header contents; requires <code>#ifndef</code> guard macros.</td>
      <td><code>#comprise math</code> parses module AST once and deduplicates symbols cleanly.</td>
    </tr>
    <tr>
      <td><strong>Signature Drift</strong></td>
      <td>Changing a function signature requires editing both <code>.h</code> and <code>.c</code>; mismatches cause linker errors.</td>
      <td>Single source of truth permanently eliminates declaration/definition drift.</td>
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
""", "Standard C: Header + Implementation") + make_code_box("rook", """
// src/math.rook
fn add(a: int, b: int) -> int {
    return a + b;
}

// src/main.rook
#comprise math
#include <stdio.h>

int main() {
    printf("Sum: %d\n", add(10, 20));
    return 0;
}
""", "Rook: Clean Modular Import") + """
<h3>3.2 Variable Initialization &amp; Stack Safety</h3>
<p>In standard C, local variables declared without an initializer inherit whatever arbitrary bit patterns existed on the CPU stack. Reading an uninitialized variable is undefined behavior (UB) and a frequent source of security vulnerabilities.</p>

<div class="table-container">
<table>
  <thead>
    <tr><th>Condition</th><th>Standard C</th><th>Rook</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>Uninitialized Local</strong></td>
      <td><code>int x;</code> has indeterminate value (stack garbage).</td>
      <td><code>int x;</code> is guaranteed zero-initialized (lowered to <code>int x = {0};</code>).</td>
    </tr>
    <tr>
      <td><strong>Array Initialization</strong></td>
      <td><code>int arr[100];</code> contains 100 garbage integers.</td>
      <td><code>int arr[100];</code> is guaranteed zeroed out at declaration.</td>
    </tr>
    <tr>
      <td><strong>Struct Initialization</strong></td>
      <td><code>Point p;</code> has uninitialized padding and fields.</td>
      <td><code>Point p;</code> has all fields cleanly initialized to zero.</td>
    </tr>
  </tbody>
</table>
</div>

<h3>3.3 Accidental Assignment in Conditional Expressions</h3>
<p>In standard C, mistakenly typing a single equals sign (<code>=</code>) instead of a double equals sign (<code>==</code>) inside an <code>if</code> condition silently overwrites the variable and evaluates the assigned value as a truth condition. Rook detects assignments inside conditionals at compile time and immediately halts compilation with an actionable diagnostic.</p>
""" + make_code_box("c", """
// Standard C: Compiles silently with a warning (or none), mutating state!
if (status = 0) { // Bug: status is set to 0, block never executes
    handle_success();
}
""", "Standard C (Dangerous Silent Mutation)") + make_code_box("rook", """
// Rook: Compile-time error!
if (status = 0) { // Compile Error: assignment in condition is banned
    handle_success();
}
// Fix: use equality operator ==
if (status == 0) {
    handle_success();
}
""", "Rook (Compile-Time Enforcement)") + """
<h3>3.4 Encapsulation &amp; Single Inheritance</h3>
<p>Standard C has no native concept of methods or object inheritance; programmers must manually nest structs and cast pointers, losing type safety. Rook introduces the <code>object</code> keyword and <code>impl</code> blocks, producing <strong>zero runtime overhead</strong> with 1:1 C struct compatibility.</p>
""" + make_code_box("c", """
// Standard C: Manual struct nesting & pointer casts
typedef struct { float x, y; } Point;
typedef struct { Point base; float radius; } Circle;

void point_move(Point* p, float dx, float dy) { p->x += dx; p->y += dy; }

Circle c = { {10.0f, 20.0f}, 5.0f };
point_move((Point*)&c, 1.0f, 2.0f); // Manual, unverified cast
""", "Standard C: Emulated Inheritance") + make_code_box("rook", """
// Rook: First-class object inheritance with zero overhead
object Point {
    x: float;
    y: float;
}

object Circle : Point {
    radius: float;
}

impl Point {
    fn move(self, dx: float, dy: float) {
        self.x += dx;
        self.y += dy;
    }
}

Circle c = Circle{ x: 10.0f, y: 20.0f, radius: 5.0f };
c.move(1.0f, 2.0f); // Type-safe, calls point_move(&c._base, 1.0f, 2.0f)
""", "Rook: Clean Single Inheritance") + """
<h3>3.5 Tagged Unions &amp; Algebraic Sum Types</h3>
<p>Representing data that can take one of several shapes in standard C requires an <code>enum</code> tag, a <code>union</code>, and disciplined manual bookkeeping. Rook provides first-class <code>sum</code> types with compile-time pattern matching.</p>
""" + make_code_box("rook", """
sum NetworkEvent {
    Connected{ client_id: int; ip: const char*; };
    DataReceived{ client_id: int; bytes: int; };
    Disconnected{ client_id: int; };
}

void handle_event(NetworkEvent* ev) {
    match (*ev) {
        Connected(c) => printf("Client %d connected from %s\n", c.client_id, c.ip),
        DataReceived(d) => printf("Client %d sent %d bytes\n", d.client_id, d.bytes),
        Disconnected(q) => printf("Client %d disconnected\n", q.client_id),
    }
}
""", "Rook: Algebraic Sum Types & Pattern Matching") + """
<h3>3.6 Resource Management: <code>goto cleanup;</code> vs. <code>defer</code></h3>
<p>Robust error handling in C typically requires manual cleanup labels and <code>goto</code> ladders. Rook provides <code>defer</code>, which schedules execution at the end of the enclosing scope in Last-In, First-Out (LIFO) order.</p>
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
""", "Standard C: Error Cleanup Ladder") + make_code_box("rook", """
// Rook: Deterministic LIFO defer
int process(const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) return -1;
    defer fclose(fp); // Guaranteed cleanup on any return path

    char* buf = (char*)malloc(1024);
    if (!buf) return -2;
    defer free(buf); // Runs before fclose(fp)

    if (do_work(fp, buf) < 0) return -3;
    return 0;
}
""", "Rook: Clean defer Cleanup") + """
<h3>3.7 Pointer Safety &amp; Explicit Arithmetic</h3>
<p>Standard C allows pointer arithmetic on untyped <code>void*</code> pointers via compiler extensions. Rook enforces strict type checking: arithmetic on <code>void*</code> is banned at compile time, requiring explicit casts to byte pointers (<code>char*</code> or <code>u8*</code>).</p>

<h3>3.8 Toolchain, Build System &amp; Execution Security</h3>
<p>C projects often rely on Makefiles or custom shell scripts where unquoted variables or external flags can trigger shell command injection. The Rokade build system parses declarative <code>rokade.toml</code> files and invokes compilers directly using POSIX <code>fork()</code>/<code>execv()</code> without shell wrappers (<code>sh -c</code>).</p>

<div class="table-container">
<table>
  <thead>
    <tr><th>Category</th><th>Standard C Ecosystem</th><th>Rook &amp; Rokade</th></tr>
  </thead>
  <tbody>
    <tr><td><strong>Build Config</strong></td><td>Complex Makefiles, CMakeLists.txt, Ninja scripts</td><td>Declarative <code>rokade.toml</code> package manifest</td></tr>
    <tr><td><strong>Toolchain Execution</strong></td><td>Often wrapped via <code>system()</code> or shell scripts</td><td>Direct POSIX <code>fork()</code>/<code>execv()</code> (zero shell injection)</td></tr>
    <tr><td><strong>Cross-Compilation</strong></td><td>Manual toolchain prefix management</td><td>Native <code>--target=&lt;triple&gt;</code> support and auto-detection</td></tr>
    <tr><td><strong>Diagnostics</strong></td><td>Text output varying across GCC, Clang, MSVC</td><td>Structured ANSI terminal diagnostics + JSON for IDEs</td></tr>
  </tbody>
</table>
</div>
"""
    add_ch("c-comparison", "3. Direct Comparison: Standard C vs. Rook", ch3)

    # ==========================================
    # Chapter 4: Mental Model & Memory Semantics
    # ==========================================
    ch4 = f"""
<p>Unlike languages that impose managed runtimes, tracing garbage collection, or complex reference counting, Rook adopts the <strong>flat, transparent memory model of C</strong>, augmented by deterministic safety guarantees.</p>

<h3>4.1 The Stack &amp; Automatic Zero-Initialization</h3>
<p>In standard C, uninitialized local stack variables contain arbitrary leftover memory contents, leading to undefined behavior and security exploits:</p>
{make_code_box("c", """
// Standard C: Undefined Behavior
void compute(void) {
    int counter;      // Stack garbage! Could be 0, could be -19827312
    if (ready) counter++; // Undefined behavior
}
""", "Standard C (Dangerous)")}

<p>In Rook, <strong>every local variable declaration without an initializer is guaranteed to be zero-initialized</strong> at compile time:</p>
{make_code_box("rook", """
int compute() {
    int counter; // Rokade lowers this to: int counter = {0};
    printf("%d\\n", counter); // Guaranteed to print 0
    return 0;
}
""", "Rook (Safe by Default)")}

<h3>4.2 The Heap &amp; Deterministic Lifecycle</h3>
<p>Heap memory in Rook is explicit. You allocate memory using standard functions (such as <code>malloc</code> / <code>calloc</code>) or specialized memory structures like <code>std/mem</code> bump arenas. Rook provides no background garbage collector; heap allocations live until explicitly reclaimed.</p>

<p>To eliminate memory leaks and ensure resources are properly released along all code paths (including early returns and error conditions), Rook introduces the <code>defer</code> statement:</p>
{make_code_box("rook", """
#include <stdio.h>
#include <stdlib.h>

int process_file(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return -1;
    defer fclose(fp); // Guaranteed to execute on any function return

    char* buffer = (char*)malloc(4096);
    if (!buffer) return -2;
    defer free(buffer); // Executes before fclose(fp)

    // Read and process data...
    return 0; // Both buffer and fp are automatically cleaned up here
}
""", "Deterministic Scope Cleanup")}

<h3>4.3 Object Memory Layout</h3>
<p>Rook's object system provides single inheritance with <strong>zero runtime overhead</strong>. An object inheriting from a base type embeds the base struct as its first member (<code>struct Base _base;</code>). There are no virtual method tables (vtables), no runtime type indicators, and no pointer indirections. A pointer to a derived object can be safely cast and passed to any function expecting a pointer to its base type.</p>
"""
    add_ch("mental-model", "4. Mental Model & Memory Semantics", ch4)

    # ==========================================
    # Chapter 5: Installation, Setup & Diagnostics
    # ==========================================
    ch5 = f"""
<p>Rook is distributed as source and installs cleanly into user space without requiring root privileges (<code>sudo</code>).</p>

<h3>5.1 System Prerequisites</h3>
<ul>
  <li><strong>Host C Compiler:</strong> <code>gcc</code> (version 9+) or <code>clang</code> (version 10+)</li>
  <li><strong>Build System:</strong> <code>cmake</code> (version 3.16+)</li>
  <li><strong>Optional Language Server:</strong> Rust <code>cargo</code> (required only if building <code>rook-lsp</code>)</li>
</ul>

<h3>5.2 Quick Installation</h3>
<p><strong>Linux &amp; macOS:</strong></p>
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

<h3>5.3 Toolchain Verification: <code>rokade doctor</code></h3>
<p>Rokade includes a built-in diagnostics command that verifies your compiler setup, include search paths, standard library availability, and toolchain dependencies:</p>
{make_code_box("bash", """
rokade doctor
""", "Run Diagnostics")}

<p>Sample output:</p>
{make_code_box("text", """
=== Rokade System Diagnostics ===
[PASS] Host OS: linux (x86_64)
[PASS] C Compiler: /usr/bin/gcc (GNU 13.2.0)
[PASS] Standard Library: /home/bknsehan/.local/rook/std/std.rook
[PASS] Include Jail: secure sandboxing active
[PASS] pkg-config: /usr/bin/pkg-config (found)
[PASS] LLVM Backend: enabled (LLVM 18.1.3)
[PASS] LSP Server: /home/bknsehan/.local/rook/bin/rook-lsp
11 checks passed, 0 warnings, 0 failures. System ready.
""", "rokade doctor Output")}
"""
    add_ch("installation", "5. Installation, Setup & Diagnostics", ch5)

    # ==========================================
    # Chapter 6: Project Lifecycle & Quickstart
    # ==========================================
    ch6 = f"""
<p>Rokade provides a unified project management interface for scaffolding, configuring, building, and running Rook projects.</p>

<h3>6.1 Project Scaffolding</h3>
<p>To scaffold a new executable project:</p>
{make_code_box("bash", """
rokade new my_service
cd my_service
""", "Scaffold Application")}

<p>To scaffold a library project suitable for sharing or vendoring:</p>
{make_code_box("bash", """
rokade new --lib math_utils
""", "Scaffold Library")}

<h3>6.2 Directory Layout</h3>
<p>A standard Rook project follows a clean, predictable structure:</p>
{make_code_box("text", """
my_service/
├── rokade.toml          # Project manifest & build configuration
├── src/
│   └── main.rook        # Application entry point
├── vendor/              # Third-party Rook and C dependencies
│   └── math_utils/      # Vendored subproject
└── build/               # Build artifacts (generated on compile)
    ├── generated/
    │   └── main.c       # Transpiled C code
    └── linux/
        └── my_service   # Final compiled native executable
""", "Project Structure")}

<h3>6.3 The Standard Workflow</h3>
{make_code_box("bash", """
# Build the project using configuration in rokade.toml
rokade build

# Build and immediately execute
rokade run

# Transpile directly to stdout to inspect the emitted C
rokade --emit-c src/main.rook

# Run instantly in memory using the LLVM JIT backend
rokade run --jit
""", "Core CLI Workflow")}
"""
    add_ch("quickstart", "6. Project Lifecycle & Quickstart", ch6)

    # ==========================================
    # Chapter 7: Variables, Types & Invariants
    # ==========================================
    ch7 = f"""
<p>Rook provides concise, expressive variable declarations while maintaining strict static typing.</p>

<h3>7.1 Declaration Forms in v0.4.1</h3>
<p>Rook supports three primary declaration forms:</p>
{make_code_box("rook", """
#include <stdio.h>

int main() {
    // 1. Type inference with 'let'
    let count = 42;             // Infers int
    let greeting = "Hello";     // Infers const char*

    // 2. Explicitly typed 'let'
    let threshold: int = 100;
    let ratio: float = 3.14159f;

    // 3. Classic C declaration syntax
    int limit = 500;
    const char* user = "admin";

    printf("%s: count=%d, limit=%d\\n", greeting, count, limit);
    return 0;
}
""", "Variable Declarations")}

{make_callout("ban", "Syntax Deprecation in v0.4.1", """
Previous experimental versions of Rook allowed bare colon declarations of the form <code>x: int = 10;</code>. 
In Rook v0.4.1, bare colon declarations have been <strong>completely removed</strong> from the grammar. 
This eliminates syntactic ambiguities with labels, ternary operators, and struct initializer key-value pairs. 
Use either <code>let x = 10;</code>, <code>let x: int = 10;</code>, or C-style <code>int x = 10;</code>.
""")}

<h3>7.2 Primitive Types &amp; Sizes</h3>
<div class="table-container">
<table>
  <thead>
    <tr><th>Type</th><th>Description</th><th>Typical Size (x86_64)</th><th>C Equivalent</th></tr>
  </thead>
  <tbody>
    <tr><td><code>int</code></td><td>Standard signed integer</td><td>4 bytes</td><td><code>int32_t</code> / <code>int</code></td></tr>
    <tr><td><code>unsigned int</code></td><td>Standard unsigned integer</td><td>4 bytes</td><td><code>uint32_t</code> / <code>unsigned int</code></td></tr>
    <tr><td><code>short</code> / <code>long</code></td><td>Short / Long integers</td><td>2 / 8 bytes</td><td><code>short</code> / <code>long</code></td></tr>
    <tr><td><code>size_t</code></td><td>Unsigned size / index type</td><td>8 bytes</td><td><code>size_t</code></td></tr>
    <tr><td><code>float</code></td><td>Single-precision IEEE 754 float</td><td>4 bytes</td><td><code>float</code></td></tr>
    <tr><td><code>double</code></td><td>Double-precision IEEE 754 float</td><td>8 bytes</td><td><code>double</code></td></tr>
    <tr><td><code>char</code></td><td>Byte / character value</td><td>1 byte</td><td><code>char</code> / <code>int8_t</code></td></tr>
    <tr><td><code>const char*</code></td><td>Pointer to null-terminated C string</td><td>8 bytes</td><td><code>const char*</code></td></tr>
    <tr><td><code>bool</code></td><td>Boolean flag (<code>true</code> / <code>false</code>)</td><td>1 byte</td><td><code>bool</code> (stdbool.h)</td></tr>
    <tr><td><code>void</code></td><td>Incomplete type / absence of value</td><td>-</td><td><code>void</code></td></tr>
  </tbody>
</table>
</div>

<h3>7.3 Fixed-Size Arrays</h3>
<p>Array declarations follow C memory layout. Uninitialized arrays are automatically zero-filled:</p>
{make_code_box("rook", """
int values[4]; // Lowered to: int values[4] = {0};
values[0] = 10;
values[1] = 20;

// Array iteration using for-in loop
for item in [100, 200, 300] {
    printf("item: %d\\n", item);
}
""", "Arrays & Iteration")}
"""
    add_ch("variables", "7. Variables, Types & Invariants", ch7)

    # ==========================================
    # Chapter 8: Pointers & Pointer Safety
    # ==========================================
    ch8 = f"""
<p>Pointers are fundamental to systems programming. Rook preserves direct pointer access and low-level memory manipulation, but eliminates syntactical confusion and enforces strict compile-time safety checks.</p>

<h3>8.1 Standardized Postfix Notation</h3>
<p>In standard C, whitespace variability creates confusing pointer declarations (e.g. <code>int *p</code> vs <code>int* p</code>). Rook standardizes strictly on postfix notation:</p>
{make_code_box("rook", """
int* ptr = &x;     // Correct in Rook
const char* name;  // Correct in Rook
""", "Valid Pointer Syntax")}

{make_callout("ban", "Prefix Pointer Syntax Banned", """
Prefix notation like <code>*int ptr;</code> is strictly rejected by the Rook parser:
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
    printf("x=%d, y=%d\\n", x, y); // x=20, y=10
    return 0;
}
""", "Address-of & Dereference")}

<h3>8.3 Enforced Pointer Arithmetic Rules</h3>
<p>Rook's semantic analyzer enforces rigorous safety rules around pointer calculations:</p>
<ul>
  <li><strong>Typed Pointer Offsets:</strong> Adding or subtracting integers from typed pointers (<code>p + 1</code>, <code>p++</code>) is permitted and steps memory by <code>sizeof(*p)</code>.</li>
  <li><strong>Prohibition on <code>void*</code> Arithmetic:</strong> In C, GCC permits arithmetic on <code>void*</code> as an extension, treating <code>sizeof(void)</code> as 1. Rook considers this dangerous and rejects it at compile time:
{make_code_box("rook", """
void* buf = get_buffer();
// COMPILE ERROR: pointer arithmetic on 'void*' is invalid
void* next = buf + 8; 

// CORRECT: cast explicitly to a byte-oriented pointer
uint8_t* byte_ptr = (uint8_t*)buf;
uint8_t* next_byte = byte_ptr + 8; // Valid
""", "void* Arithmetic Enforcement")}
  </li>
  <li><strong>Prohibition of Non-Additive Operations:</strong> Multiplying, dividing, modulo-ing, or performing bitwise shifts on pointers is illegal and rejected at compile time:
{make_code_box("rook", """
int* p = &x;
p = p * 2; // COMPILE ERROR: invalid operand to binary operator
p = p & 0xFF; // COMPILE ERROR: invalid operand to binary operator
""", "Illegal Pointer Math Banned")}
  </li>
</ul>
"""
    add_ch("pointers", "8. Pointers & Pointer Safety", ch8)

    # ==========================================
    # Chapter 9: Control Flow & Branching
    # ==========================================
    ch9 = f"""
<p>Rook provides structured control flow primitives familiar to C developers, with compile-time enforcements that prevent classic logical errors.</p>

<h3>9.1 Conditionals: if / else</h3>
{make_code_box("rook", """
if (status == 200) {
    printf("OK\\n");
} else if (status >= 400 && status < 500) {
    printf("Client Error\\n");
} else {
    printf("Other\\n");
}
""", "Standard Conditional")}

{make_callout("ban", "Assignment in Condition Strictly Banned", """
One of the most frequent bugs in C is accidentally typing <code>if (x = 5)</code> instead of <code>if (x == 5)</code>. 
Rook's semantic analyzer recursively inspects condition expressions (stripping parentheses) and rejects assignment expressions in:
<ul>
  <li><code>if (cond)</code></li>
  <li><code>while (cond)</code></li>
  <li><code>for (...; cond; ...)</code></li>
  <li><code>switch (cond)</code></li>
  <li>Ternary operators: <code>(cond) ? a : b</code></li>
</ul>
<pre><code>error: assignment used as condition; did you mean '=='?</code></pre>
""")}

<h3>9.2 Iteration: while, do-while, for, and for-in</h3>
{make_code_box("rook", """
// 1. Classic while loop
int i = 0;
while (i < 5) {
    i++;
}

// 2. Standard C-style for loop
for (int idx = 0; idx < 10; idx++) {
    if (idx == 3) continue;
    if (idx == 8) break;
}

// 3. Range-based for-in loop
for score in [95, 88, 72, 100] {
    printf("score: %d\\n", score);
}
""", "Looping Constructs")}

<h3>9.3 Scalar Branching: C switch</h3>
<p>For scalar dispatch (integers and enums) using jump tables, Rook supports standard C <code>switch</code>:</p>
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
""", "Classic C switch")}

{make_callout("note", "Consolidation of switch and match in v0.4.1", """
Earlier builds contained an experimental arrow syntax inside switch (<code>case 1 -> action();</code>). 
In Rook v0.4.1, arrow switch was removed to keep <code>switch</code> dedicated exclusively to traditional C semantics. 
All modern pattern matching, destructuring, and expression branching are consolidated into the <code>match</code> construct (see Chapter 12).
""")}

<h3>9.4 Prohibition of goto</h3>
<p>Rook explicitly bans the <code>goto</code> keyword. All control flow must be expressed via structured constructs (<code>if</code>, <code>while</code>, <code>for</code>, <code>switch</code>, <code>match</code>, <code>break</code>, <code>continue</code>, and <code>return</code>).</p>
"""
    add_ch("control-flow", "9. Control Flow & Branching", ch9)

    # ==========================================
    # Chapter 10: Functions & Call Semantics
    # ==========================================
    ch10 = f"""
<p>Functions in Rook follow standard C declaration syntax, avoiding redundant keywords like <code>fn</code>, <code>def</code>, or <code>function</code>.</p>

<h3>10.1 Signature &amp; Implementation</h3>
{make_code_box("rook", """
#include <stdio.h>

// Return type, identifier, typed parameter list
int multiply(int a, int b) {
    return a * b;
}

void log_message(const char* tag, const char* msg) {
    printf("[%s] %s\\n", tag, msg);
}

int main() {
    let result = multiply(6, 7);
    log_message("INFO", "Computation complete");
    return 0;
}
""", "Function Definitions")}

<h3>10.2 Compile-Time Enforcements</h3>
<ul>
  <li><strong>Strict Return Validation:</strong> Any non-void function must return an appropriate value across all execution paths. A function falling through without a <code>return</code> triggers a compile-time error.</li>
  <li><strong>Arity &amp; Type Verification:</strong> Function calls are statically verified against declarations. Calling a function with missing or extraneous arguments fails immediately.</li>
  <li><strong>Top-Level Scope Enforcement:</strong> Nested function declarations (defining a function inside another function body) are illegal and rejected.</li>
</ul>

<h3>10.3 Foreign Function Declarations (extern)</h3>
<p>When calling external C functions that are not provided via header includes, declare their prototype with <code>extern</code>:</p>
{make_code_box("rook", """
extern int puts(const char* s);
extern void* memcpy(void* dest, const void* src, size_t n);

int main() {
    puts("Direct C linkage");
    return 0;
}
""", "extern Function Declarations")}
"""
    add_ch("functions", "10. Functions & Call Semantics", ch10)

    # ==========================================
    # Chapter 11: Structs & Memory Layout
    # ==========================================
    ch11 = f"""
<p>Structs in Rook represent Plain Old Data (POD). They have direct, transparent mapping to C struct layout, ensuring exact memory alignment, size, and field offsets.</p>

<h3>11.1 Definition &amp; Initialization</h3>
<p>Rook supports both C-style field definitions and colon-separated field syntax:</p>
{make_code_box("rook", """
// Definition
struct Vector2 {
    float x;
    float y;
};

int main() {
    // 1. Positional initialization
    Vector2 v1 = {10.0f, 20.0f};

    // 2. Designated / Named initialization (Order independent!)
    let v2 = Vector2{ y: 50.0f, x: 25.0f };

    printf("v1: (%.1f, %.1f)\\n", v1.x, v1.y);
    printf("v2: (%.1f, %.1f)\\n", v2.x, v2.y);
    return 0;
}
""", "Struct Definitions & Designated Initializers")}

<h3>11.2 Field Access &amp; Pointers</h3>
{make_code_box("rook", """
Vector2 v = Vector2{ x: 1.0f, y: 2.0f };
Vector2* ptr = &v;

// Value access uses '.'
v.x = 5.0f;

// Pointer dereference uses '->'
ptr->y = 10.0f;
""", "Member Access Operators")}

{make_callout("spec", "Transpilation Mechanics", """
Designated initializers like <code>Vector2{ y: 50.0f, x: 25.0f }</code> are transpiled directly to standard C99 designated compound literals:
<pre><code>((Vector2){.y = 50.0f, .x = 25.0f})</code></pre>
This ensures zero runtime copying or initialization overhead.
""")}
"""
    add_ch("structs", "11. Structs & Memory Layout", ch11)

    # ==========================================
    # Chapter 12: Objects, Methods & Single Inheritance
    # ==========================================
    ch12 = f"""
<p>Rook provides an elegant object model featuring <strong>single inheritance and method dispatch with zero runtime cost</strong>. Unlike C++, Rook introduces no virtual method tables (vtables), no runtime type information (RTTI), and no hidden pointer indirection.</p>

<h3>12.1 The object Keyword</h3>
<p>An <code>object</code> defines a record type that can participate in single inheritance:</p>
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

<h3>12.2 Memory Lowering &amp; ABI Parity</h3>
<p>When Rook transpiles <code>object Player : Entity</code>, it lowers the inheritance hierarchy by embedding the base type as the first struct member named <code>_base</code>:</p>
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
""", "Lowered C Struct Representation")}

<p>Because <code>_base</code> is located at offset 0 of <code>Player</code>, a pointer to <code>Player</code> is binary-compatible with a pointer to <code>Entity</code> under standard C ABI rules. Any function accepting <code>Entity*</code> can receive <code>(Entity*)player_ptr</code> without overhead.</p>

<h3>12.3 Method Dispatch with impl</h3>
<p>Methods are defined outside the struct definition using <code>impl</code> blocks:</p>
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
    void describe(self) { // 'self' shorthand automatically types to Circle*
        printf("Circle at (%.1f, %.1f) with radius %.1f\\n", self.x, self.y, self.radius);
    }
}

int main() {
    // Flat initialization across parent and child fields
    let c = Circle{ x: 10.0f, y: 15.0f, radius: 5.0f };
    
    // Call method on derived type
    c.describe();

    // Call inherited method from base type
    c.translate(5.0f, -5.0f);
    c.describe();
    return 0;
}
""", "Methods & Inheritance in Action")}

{make_callout("tip", "Zero-Cost Method Lowering", """
Method invocations such as <code>c.describe()</code> lower to direct C function calls: <code>Circle_describe(&c)</code>. 
Inherited calls like <code>c.translate(...)</code> automatically lower to <code>Shape_translate(&c._base, ...)</code>. 
Dispatch is 100% static, inlined by the C compiler, and incurs zero vtable indirection.
""")}
"""
    add_ch("objects", "12. Objects, Methods & Single Inheritance", ch12)

    # ==========================================
    # Chapter 13: Algebraic Data Types: sum, enum & match
    # ==========================================
    ch13 = f"""
<p>Modern systems software requires robust representation of polymorphic data without complex class hierarchies. Rook provides first-class <strong>sum types (tagged unions)</strong> and pattern matching via <code>match</code>.</p>

<h3>13.1 Enumerations: enum</h3>
{make_code_box("rook", """
enum HttpMethod {
    Get,
    Post,
    Put,
    Delete
}
""", "Standard Enum")}

<h3>13.2 Sum Types (Tagged Unions)</h3>
<p>A <code>sum</code> type represents a value that can hold one of several distinct variant shapes. Variants may be unit variants (bearing no payload) or data variants:</p>
{make_code_box("rook", """
sum Node {
    Leaf { value: int; };
    Branch { left: Node*; right: Node*; };
    Empty;
}
""", "Sum Type Declaration")}

<p>In memory, Rokade lowers a <code>sum</code> type into a C struct containing an integer discriminator tag and an anonymous union storing the variant payloads:</p>
{make_code_box("c", """
typedef struct Node {
    int tag; // Identifies which variant is currently active
    union {
        struct { int value; } Leaf;
        struct { struct Node* left; struct Node* right; } Branch;
    };
} Node;
""", "Lowered Tagged Union")}

<h3>13.3 Pattern Matching: match</h3>
<p>The <code>match</code> construct provides comprehensive destructuring for sum types, enums, and scalar values. It is supported in both statement and expression forms.</p>

<h4>Statement Form:</h4>
{make_code_box("rook", """
void print_node(Node* n) {
    match (*n) {
        Leaf { value } => printf("Leaf: %d\\n", value),
        Branch { left, right } => printf("Branch\\n"),
        Empty => printf("Empty\\n"),
        _ => printf("Unknown\\n")
    }
}
""", "Statement Pattern Match")}

<h4>Expression Form:</h4>
{make_code_box("rook", """
sum Shape {
    Circle { radius: float; };
    Rectangle { width: float; height: float; };
    Point;
}

float calculate_area(Shape s) {
    return match (s) {
        Circle { radius } => 3.14159f * radius * radius,
        Rectangle { width, height } => width * height,
        Point => 0.0f,
        _ => 0.0f
    };
}
""", "Expression Pattern Match")}

<h3>13.4 Instantiating Sum Types &amp; Methods with impl</h3>
<p>To instantiate a sum type, specify the sum type name as the variable type (or use explicit type annotation with <code>let</code>) and provide the variant constructor:</p>
{make_code_box("rook", """
#include <stdio.h>

sum Shape {
    Circle { r: float; };
    Rect { w: float; h: float; };
    Point;
}

// Methods can be attached to sum types via impl
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

    printf("Circle area: %f\\n", a.area()); // 12.566360
    printf("Rect area:   %f\\n", b.area()); // 12.000000
    printf("Point area:  %f\\n", c.area()); // 0.000000
    return 0;
}
""", "Sum Type Instantiation & Methods")}
"""
    add_ch("sum-match", "13. Algebraic Data Types: sum, enum & match", ch13)

    # ==========================================
    # Chapter 14: Deterministic Cleanup: defer
    # ==========================================
    ch14 = f"""
<p>Manual resource cleanup is the primary source of memory leaks and descriptor exhaustion in C. Rook solves this with the <code>defer</code> statement, providing deterministic scope-based resource reclamation.</p>

<h3>14.1 defer Mechanics</h3>
<p>When <code>defer &lt;stmt&gt;;</code> is encountered, the deferred statement is scheduled to run when the surrounding lexical scope terminates. Multiple deferred statements execute in <strong>Last-In, First-Out (LIFO)</strong> order (stack unwinding):</p>
{make_code_box("rook", """
#include <stdio.h>

void trace_lifo() {
    defer printf("First deferred (runs last)\\n");
    defer printf("Second deferred (runs middle)\\n");
    printf("Function body execution\\n");
    defer printf("Third deferred (runs first)\\n");
}

int main() {
    trace_lifo();
    return 0;
}
""", "LIFO Execution Order")}

<p>Output:</p>
{make_code_box("text", """
Function body execution
Third deferred (runs first)
Second deferred (runs middle)
First deferred (runs last)
""", "Execution Output")}

<h3>14.2 Resource Pairing Idioms</h3>
<p>Place <code>defer</code> immediately after acquiring any resource to guarantee deallocation, regardless of whether the function returns early or encounters an error:</p>
{make_code_box("rook", """
#include <stdio.h>
#include <stdlib.h>

int process_data(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) return -1;
    defer fclose(f); // Guaranteed closure

    int* data = (int*)malloc(1000 * sizeof(int));
    if (!data) return -2; // f is closed automatically!
    defer free(data);     // Guaranteed deallocation

    // Process file...
    if (read_error()) {
        return -3; // Both data is freed and f is closed!
    }

    return 0; // Both data is freed and f is closed!
}
""", "Safe Resource Management")}
"""
    add_ch("cleanup-defer", "14. Deterministic Cleanup: defer", ch14)

    # ==========================================
    # Chapter 15: Modules, Namespaces & Directives
    # ==========================================
    ch15 = f"""
<p>Rook modernizes code organization by eliminating C header files (<code>.h</code>) entirely for Rook code, while providing a clear distinction between native Rook modules and external C headers.</p>

<h3>15.1 Directives Overview</h3>
<div class="table-container">
<table>
  <thead>
    <tr><th>Directive</th><th>Usage</th><th>Semantics</th></tr>
  </thead>
  <tbody>
    <tr><td><code>#comprise module</code></td><td>Import local Rook source file</td><td>Parsed and merged into the AST; diamond dependencies automatically deduplicated.</td></tr>
    <tr><td><code>#comprise &lt;std/io&gt;</code></td><td>Import Rook standard library module</td><td>Resolves against Rook's stdlib directory; deduplicated.</td></tr>
    <tr><td><code>#include &lt;header.h&gt;</code></td><td>Import standard C system header</td><td>Passed directly through to the generated C code.</td></tr>
    <tr><td><code>#include "header.h"</code></td><td>Import local C library header</td><td>Subject to path validation under the Include Jail.</td></tr>
  </tbody>
</table>
</div>

<h3>15.2 Modular Multi-File Organization</h3>
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
#comprise math // Resolves to src/math.rook

int main() {
    printf("add: %d\\n", add(10, 5));
    return 0;
}
""", "src/main.rook")}

{make_callout("tip", "Automatic Deduplication (No Include Guards)", """
Unlike C where header files require <code>#ifndef HEADER_H</code> include guards to prevent duplicate definitions, Rook's <code>#comprise</code> directive automatically deduplicates modules. If Module A and Module B both comprise Module C, Module C is parsed and compiled exactly once.
""")}

<h3>15.3 Security Sandboxing: The Include Jail</h3>
<p>To defend against directory traversal attacks and unauthorized filesystem reading during compilation, Rokade implements an <strong>Include Jail</strong>. All file inclusion directives are resolved and validated against a sandboxed boundary:</p>
<ul>
  <li>Paths must reside within the project root, the Rook standard library installation, vendored package directories, or verified system include directories.</li>
  <li>Path traversal attempts (such as <code>#include "../../../etc/shadow"</code>) are rejected immediately at compile time.</li>
</ul>
"""
    add_ch("modules", "15. Modules, Namespaces & Directives", ch15)

    # ==========================================
    # Chapter 16: Vendored Dependencies & Packages
    # ==========================================
    ch16 = f"""
<p>Modern software relies on shared libraries. Rokade provides a built-in dependency and vendoring system that resolves packages without external package managers or centralized registries.</p>

<h3>16.1 Defining Dependencies in rokade.toml</h3>
<p>Dependencies are declared in the <code>[dependencies]</code> table of your project's <code>rokade.toml</code> using relative paths:</p>
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
""", "rokade.toml Dependencies")}

<h3>16.2 The vendor/ Directory Hierarchy</h3>
<p>Vendored dependencies are self-contained Rook projects located within the <code>vendor/</code> directory:</p>
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

<h3>16.3 Transitive Dependency Resolution</h3>
<p>When building your project, Rokade automatically performs dependency graph resolution:</p>
<ol>
  <li>Inspects each dependency declared in <code>[dependencies]</code>.</li>
  <li>Safely verifies paths and parses the dependency's <code>rokade.toml</code>.</li>
  <li>Recursively discovers transitive dependencies (e.g. if <code>engine</code> depends on <code>mathlib</code>).</li>
  <li>Automatically adds the include directories and source files of all dependencies into the host compiler command arguments.</li>
</ol>

<p>In your source code, simply comprise the library by name:</p>
{make_code_box("rook", """
#comprise engine

int main() {
    engine_init();
    return 0;
}
""", "Using Vendored Dependencies")}
"""
    add_ch("dependencies", "16. Vendored Dependencies & Packages", ch16)

    # ==========================================
    # Chapter 17: C Interoperability & Integration
    # ==========================================
    ch17 = f"""
<p>Rook's ultimate strength is its <strong>seamless, zero-overhead interoperability with C</strong>. Because Rook transpiles to standard C and shares identical calling conventions, any C library can be used immediately without foreign function interfaces (FFI), code generators, or runtime bridges.</p>

<h3>17.1 Integrating System Libraries via pkg-config</h3>
<p>To integrate popular C libraries (such as Raylib, SDL2, SQLite3, or GTK), declare them in the <code>pkg-config</code> array in <code>rokade.toml</code>:</p>
{make_code_box("toml", """
[package]
name = "raylib_demo"
version = "0.1.0"

[build]
kind = "exe"
pkg-config = ["raylib"]
""", "rokade.toml with pkg-config")}

<p>Rokade automatically executes <code>pkg-config --cflags --libs raylib</code> using safe argument vectors and configures the compilation and linking pipelines.</p>

<h3>17.2 Concrete Example: Complete Raylib Window</h3>
{make_code_box("rook", """
#include <stdio.h>
#include <raylib.h>

int main() {
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "Rook + Raylib Modern Demo");
    defer CloseWindow(); // Guaranteed cleanup

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
""", "src/main.rook (Raylib GUI)")}

<h3>17.3 Mixing Raw C Code Directly in .rook Files</h3>
<p>You can embed C declarations, typedefs, and inline C helpers directly in a <code>.rook</code> file. Rokade recognizes standard C syntax and passes it through directly to the generated C source:</p>
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
""", "Embedding Raw C")}
"""
    add_ch("c-interop", "17. C Interoperability & Integration", ch17)

    # ==========================================
    # Chapter 18: Standard Library Reference (std/)
    # ==========================================
    ch18 = f"""
<p>Rook includes a lightweight, modular standard library located in the <code>std/</code> directory. Standard library modules can be imported individually via <code>#comprise &lt;std/name&gt;</code> or all at once via <code>#comprise &lt;std&gt;</code>.</p>

<h3>18.1 std/io — Fast Console I/O</h3>
<p>Import with <code>#comprise &lt;std/io&gt;</code></p>
{make_code_box("rook", """
println("Standard output with newline");
print("Standard output without newline");
eprintln("Standard error log");
""", "std/io Example")}

<h3>18.2 std/str — Non-Owning String Slice (Str)</h3>
<p>Import with <code>#comprise &lt;std/str&gt;</code>. <code>Str</code> is a lightweight struct containing a pointer and a length (<code>const char* data; size_t len;</code>). It provides zero-allocation string slicing and inspections:</p>
<div class="table-container">
<table>
  <thead>
    <tr><th>Method / Function</th><th>Description</th></tr>
  </thead>
  <tbody>
    <tr><td><code>str_from_cstr(s)</code></td><td>Creates a <code>Str</code> slice from a null-terminated C string.</td></tr>
    <tr><td><code>s.is_empty()</code></td><td>Returns true if <code>s.len == 0</code>.</td></tr>
    <tr><td><code>s.slice(start, len)</code></td><td>Returns a subslice without memory allocation.</td></tr>
    <tr><td><code>s.equals(other)</code></td><td>Returns true if both string slices match in content.</td></tr>
    <tr><td><code>s.starts_with(prefix)</code></td><td>Checks for matching prefix.</td></tr>
    <tr><td><code>s.ends_with(suffix)</code></td><td>Checks for matching suffix.</td></tr>
    <tr><td><code>s.trim()</code></td><td>Returns subslice with leading and trailing whitespace stripped.</td></tr>
    <tr><td><code>s.find(sub)</code></td><td>Returns byte offset of substring, or -1 if not found.</td></tr>
    <tr><td><code>s.contains(sub)</code></td><td>Returns true if substring is present.</td></tr>
    <tr><td><code>s.to_int()</code></td><td>Parses ASCII digits into an integer.</td></tr>
    <tr><td><code>s.to_cstr(buf, cap)</code></td><td>Copies string slice into buffer and null-terminates.</td></tr>
  </tbody>
</table>
</div>

<h3>18.3 std/vec — Dynamic Vector &amp; StringBuilder</h3>
<p>Import with <code>#comprise &lt;std/vec&gt;</code></p>
{make_code_box("rook", """
// Dynamic Vector of pointers
Vec v = vec_new(8);
defer v.destroy();

v.push(&item1);
v.push(&item2);
void* val = v.pop();

// Efficient String Construction
StringBuilder sb = sb_new(64);
defer sb.destroy();

sb.append_cstr("Latency: ");
sb.append_int(42);
sb.append_cstr(" ms\\n");
printf("%s", sb.to_cstr());
""", "std/vec Example")}

<h3>18.4 std/mem — Bump Arena Allocator</h3>
<p>Import with <code>#comprise &lt;std/mem&gt;</code>. The <code>Arena</code> allocator allocates memory sequentially from a fixed contiguous block, allowing hundreds of small allocations to be freed instantly with a single call:</p>
{make_code_box("rook", """
Arena arena = arena_new(65536); // 64 KB memory pool
defer arena.destroy();

// Allocate within arena (no individual free calls needed)
void* block1 = arena.alloc(128);
void* block2 = arena.alloc(512);

arena.reset(); // Instantly clears all allocations
""", "std/mem Arena Allocator")}

<h3>18.5 std/result — Result and Option Types</h3>
<p>Import with <code>#comprise &lt;std/result&gt;</code>. Provides algebraic return types for explicit error handling:</p>
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
""", "std/result Example")}

<h3>18.6 Other Standard Modules</h3>
<ul>
  <li><strong>std/fs:</strong> File operations (<code>fs_read_to_string</code>, <code>fs_write_file</code>, <code>fs_exists</code>) and path utilities (<code>path_basename</code>, <code>path_dirname</code>, <code>path_extension</code>).</li>
  <li><strong>std/os:</strong> System utilities (<code>os_getenv</code>, <code>os_setenv</code>, <code>os_time_ms</code>, <code>os_sleep_ms</code>, <code>panic</code>, <code>exit_with</code>).</li>
  <li><strong>std/math:</strong> Vector math (<code>Vec2</code>, <code>Vec3</code>), geometric bounding boxes (<code>Rect</code>), clamping, interpolation (<code>lerpf</code>), and trigonometry.</li>
</ul>
"""
    add_ch("stdlib", "18. Standard Library Reference (std/)", ch18)

    # ==========================================
    # Chapter 19: Safety Invariants & Compile-Time Checks
    # ==========================================
    ch19 = f"""
<p>Rook enforces strict compile-time checks on <code>.rook</code> source code to eliminate the most common classes of undefined behavior and bugs inherent in C.</p>

<div class="table-container">
<table>
  <thead>
    <tr><th>Bug Class</th><th>Behavior in Standard C</th><th>Behavior in Rook</th><th>Rationale</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>Assignment in Condition</strong></td>
      <td>Silently assigns variable and tests truthiness (<code>if (x = 5)</code>).</td>
      <td><strong>Compile Error</strong> (even with parens: <code>if ((x = 5))</code>)</td>
      <td>Almost invariably a typographical error for <code>==</code> that corrupts program logic.</td>
    </tr>
    <tr>
      <td><strong>Uninitialized Locals</strong></td>
      <td>Contains arbitrary stack garbage (Undefined Behavior).</td>
      <td><strong>Guaranteed Zero-Init</strong> (<code>= {0}</code>)</td>
      <td>Eliminates non-deterministic crashes, security information leaks, and uninitialized reads.</td>
    </tr>
    <tr>
      <td><strong>void* Pointer Arithmetic</strong></td>
      <td>GCC extension or undefined behavior.</td>
      <td><strong>Compile Error</strong></td>
      <td>The byte stride of <code>void</code> is formally undefined. Developers must explicitly cast to <code>uint8_t*</code> or <code>char*</code>.</td>
    </tr>
    <tr>
      <td><strong>Invalid Pointer Operations</strong></td>
      <td>Allows ambiguous or meaningless operations (e.g. <code>ptr * 2</code>).</td>
      <td><strong>Compile Error</strong></td>
      <td>Multiplication, division, modulo, and bitwise logic have no valid mathematical meaning on memory addresses.</td>
    </tr>
    <tr>
      <td><strong>Pointer Syntax Ambiguity</strong></td>
      <td>Permits varying styles (<code>int *p</code>, <code>int* p</code>).</td>
      <td><strong>Enforces Postfix</strong> (<code>int* p</code>; bans <code>*int p</code>)</td>
      <td>Ensures consistent, unambiguous parsing across codebases.</td>
    </tr>
    <tr>
      <td><strong>Division by Zero</strong></td>
      <td>Immediate hardware trap (SIGFPE) or undefined behavior.</td>
      <td><strong>Compile Error</strong> for literal / const-folded zeroes (e.g. <code>x / 0</code>, <code>x / (2 - 2)</code>)</td>
      <td>Traps obvious arithmetic faults at compile time.</td>
    </tr>
    <tr>
      <td><strong>Arbitrary Jumps (goto)</strong></td>
      <td>Permitted anywhere within function scope.</td>
      <td><strong>Banned</strong></td>
      <td>Prevents spaghetti code and ensures deterministic scope entry and exit for <code>defer</code>.</td>
    </tr>
    <tr>
      <td><strong>Comma Operator in Expressions</strong></td>
      <td>Permits sequential evaluation (<code>a, b</code>).</td>
      <td><strong>Banned</strong> in expression contexts</td>
      <td>Obscures control flow and hides side effects; commas are strictly reserved for argument separators.</td>
    </tr>
    <tr>
      <td><strong>Include Jail Violation</strong></td>
      <td>Allows arbitrary path traversal (e.g. <code>#include "../../../etc/passwd"</code>).</td>
      <td><strong>Compile Error</strong></td>
      <td>Enforces strict sandbox boundaries around the project tree, stdlib, and dependencies.</td>
    </tr>
  </tbody>
</table>
</div>

<h3>19.1 Catalog of Concrete Compiler Diagnostics</h3>
<p>Below are representative examples of illegal constructs and the precise compiler errors emitted by Rokade's semantic analyzer (<code>sema.c</code>):</p>

{make_code_box("rook", """
// 1. Assignment in Condition (Recursively Strips Parentheses)
if ((x = 5)) { }
// Error: assignment used as condition; did you mean '=='?

// 2. Prefix Pointer Notation
*int ptr;
// Error: invalid pointer syntax '*Type'; Rook standardizes on postfix 'Type*'

// 3. void* Pointer Arithmetic
void* buffer = get_raw();
void* next = buffer + 4;
// Error: pointer arithmetic on 'void*' is invalid

// 4. Invalid Pointer Operations
int* p = &value;
p = p * 2;
// Error: invalid operand to binary operator

// 5. Literal and Const-Folded Division by Zero
int a = 10 / 0;
int b = 10 / (5 - 5);
// Error: division or modulo by zero

// 6. Arbitrary Jump Keyword
goto exit_label;
// Error: 'goto' is not supported in Rook

// 7. Non-Void Function Missing Return
int calculate(int x) {
    if (x > 0) return x * 2;
    // Missing return on else path!
}
// Error: control reaches end of non-void function
""", "Compile-Time Diagnostic Catalog")}
"""
    add_ch("safety", "19. Safety Invariants & Compile-Time Checks", ch19)

    # ==========================================
    # Chapter 20: Configuration & Multi-Target Builds (rokade.toml)
    # ==========================================
    ch20 = f"""
<p>Every Rook project is configured via a declarative <code>rokade.toml</code> manifest. Rokade handles multi-target cross-compilation without requiring manual Makefiles or complex CMake scripts.</p>

<h3>20.1 Complete rokade.toml Schema</h3>
{make_code_box("toml", """
[package]
name = "service_engine"
version = "0.4.1"
authors = ["Engineering Team <dev@example.com>"]
description = "High-throughput telemetry daemon"

[build]
kind = "exe"                   # Options: "exe", "shared-lib", "static-lib"
standard = "c2x"               # Options: "c11", "c17", "c2x", "gnu23"
backend = "c"                  # Options: "c" (default), "llvm"
targets = ["linux", "windows"] # Active target builds
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
""", "Complete rokade.toml Schema")}

<h3>20.2 Cross-Compilation Support</h3>
<p>Rokade includes built-in cross-compilation target drivers:</p>
<ul>
  <li><strong>Linux:</strong> Compiles natively via host <code>gcc</code> or <code>clang</code>.</li>
  <li><strong>Windows:</strong> Cross-compiles from Linux using MinGW (<code>x86_64-w64-mingw32-gcc</code>), emitting a Windows <code>.exe</code>.</li>
  <li><strong>Android:</strong> Auto-detects installed Android NDK toolchains and compiles shared libraries (<code>.so</code>) for specified ABIs (e.g. <code>arm64-v8a</code>, <code>x86_64</code>).</li>
</ul>

{make_code_box("bash", """
# Build specifically for Windows target
rokade build --target=windows

# Build specifically for Android target
rokade build --target=android

# Build all configured targets in parallel
rokade build --all
""", "Target Build Commands")}
"""
    add_ch("config", "20. Configuration & Multi-Target Builds (rokade.toml)", ch20)

    # ==========================================
    # Chapter 21: Compiler CLI & Architecture Reference
    # ==========================================
    ch21 = f"""
<p>The <code>rokade</code> executable is a self-contained command-line driver providing build orchestration, static analysis, code generation, and diagnostics.</p>

<h3>21.1 Command-Line Reference</h3>
<div class="table-container">
<table>
  <thead>
    <tr><th>Command / Option</th><th>Description</th></tr>
  </thead>
  <tbody>
    <tr><td><code>rokade new [--lib] &lt;name&gt;</code></td><td>Scaffolds a new application or library project.</td></tr>
    <tr><td><code>rokade build [--all] [--target=T]</code></td><td>Builds the project according to <code>rokade.toml</code>.</td></tr>
    <tr><td><code>rokade run [--jit] [file.rook]</code></td><td>Compiles and executes the project or a single file. <code>--jit</code> runs in-memory.</td></tr>
    <tr><td><code>rokade doctor</code></td><td>Executes full diagnostic suite inspecting toolchain and paths.</td></tr>
    <tr><td><code>rokade toolchain</code></td><td>Displays detected host C compiler, version, and default flags.</td></tr>
    <tr><td><code>rokade --emit-c &lt;file.rook&gt;</code></td><td>Transpiles file and prints generated C code to stdout.</td></tr>
    <tr><td><code>rokade --emit-llvm &lt;file.rook&gt;</code></td><td>Emits LLVM Intermediate Representation (LLVM IR).</td></tr>
    <tr><td><code>rokade --emit-obj &lt;file.rook&gt;</code></td><td>Compiles directly to native host object file (<code>.o</code>) via LLVM.</td></tr>
    <tr><td><code>rokade --ast &lt;file.rook&gt;</code></td><td>Prints pretty-printed AST representation for compiler debugging.</td></tr>
    <tr><td><code>rokade --check &lt;file.rook&gt;</code></td><td>Performs round-trip parser verification.</td></tr>
    <tr><td><code>rokade --check-dir &lt;dir&gt;</code></td><td>Recursively validates all <code>.rook</code> files within directory.</td></tr>
    <tr><td><code>rokade --diagnostics &lt;file.rook&gt;</code></td><td>Emits machine-readable JSON diagnostics for IDE/LSP integration.</td></tr>
    <tr><td><code>rokade --def-at &lt;file&gt; &lt;line&gt; &lt;col&gt;</code></td><td>Returns symbol definition location for editor navigation.</td></tr>
    <tr><td><code>rokade --symbols &lt;file.rook&gt;</code></td><td>Emits JSON list of top-level functions, structs, and objects.</td></tr>
  </tbody>
</table>
</div>

<h3>21.2 Zero-Shell Process Execution Architecture</h3>
<p>To ensure security in automated CI/CD and multi-user environments, Rokade completely eschews shell execution (e.g. <code>system()</code> or <code>popen("sh -c ...")</code>) when invoking compiler tools:</p>
<ul>
  <li>All compiler commands, flags, and library parameters are passed as structured argument vectors (<code>char* argv[]</code>) directly to POSIX <code>fork()</code> and <code>execv()</code>.</li>
  <li>Shell metacharacters (such as semicolons, pipes, or command substitutions in <code>cflags</code>) are treated as literal strings and cannot execute arbitrary shell code.</li>
  <li>Compilation and linker return codes are strictly verified; any non-zero exit code immediately aborts the build process with informative error output.</li>
</ul>
"""
    add_ch("cli", "21. Compiler CLI & Architecture Reference", ch21)

    return chapters

if __name__ == "__main__":
    def dummy_box(lang, code, title=""): return f"[BOX {lang} {title}]"
    def dummy_call(kind, title, body): return f"[CALL {kind} {title}]"
    chs = get_guide_chapters(dummy_box, dummy_call)
    print(f"Loaded {len(chs)} guide chapters successfully!")
