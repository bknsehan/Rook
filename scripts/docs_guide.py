#!/usr/bin/env python3
"""
docs_guide.py
Technical reference manual for the Rook programming language and Rokade compiler (v0.6.1).
Contains Chapters 1 to 21, written with strict technical accuracy and zero marketing language.
"""

def get_guide_chapters(make_code_box, make_callout):
    chapters = []

    def add_ch(cid, title, content):
        chapters.append((cid, title, content))

    # ==========================================
    # Chapter 1: The Rook Philosophy & Technical Constraints
    # ==========================================
    ch1 = f"""
<p>Rook is an explicit systems programming language designed to eliminate the structural maintenance burdens and common undefined behavior patterns of C while maintaining 100% ABI parity and zero runtime overhead. Rook is implemented in C11 and compiles directly to standard C (C11/C23) and native LLVM IR.</p>

<h3>1.1 Design Problems in Standard C</h3>
<p>In standard C (ISO/IEC 9899), systems developers face recurring architectural and safety challenges inherent to the language grammar and compilation model:</p>
<ul>
  <li><strong>Header and Implementation Divergence:</strong> C separates declarations into <code>.h</code> header files and definitions into <code>.c</code> translation units. When function prototypes, struct definitions, or macro signatures change in headers without corresponding updates in source files, silent ABI mismatches or linker failures occur.</li>
  <li><strong>Uninitialized Memory by Default:</strong> Local variables declared without initializers in C inherit indeterminate values from the call stack. Reading uninitialized stack values leads to undefined behavior and security exploits.</li>
  <li><strong>Syntactic Ambiguity in Conditionals:</strong> In C grammar, assignment is an expression. An assignment written where an equality check was intended (e.g., <code>if (x = 5)</code>) compiles without error, causing silent runtime corruption.</li>
  <li><strong>Boilerplate for Abstractions:</strong> Implementing basic data structures such as single inheritance or tagged unions requires manual void pointer casting, manual tag tracking, and nested struct layouts without compiler-enforced exhaustiveness.</li>
</ul>

<h3>1.2 Technical Invariants of Rook</h3>
<ul>
  <li><strong>Deterministic Zero Initialization:</strong> All local variables, arrays, and structs allocated on the stack are guaranteed by the compiler to be initialized to zero unless explicitly initialized with an expression.</li>
  <li><strong>Single Source of Truth:</strong> Rook source files define both declarations and implementations in single <code>.rook</code> units. External C headers are parsed dynamically at compile time via libclang without manual bindings.</li>
  <li><strong>Guaranteed C ABI Parity:</strong> Every Rook struct, function, and primitive type maps 1:1 to host C ABI rules (System V AMD64 ABI, Microsoft x64 ABI, or ARM AAPCS64). Rook binaries can call C libraries and be called by C libraries with zero overhead.</li>
  <li><strong>Syntactic Rejection of Assignment in Conditions:</strong> Assignment statements (<code>x = 5</code>) cannot be used as conditions. Expressions like <code>if (x = 5)</code> produce a hard compile-time error.</li>
  <li><strong>Static Single Inheritance with Prefix Subtyping:</strong> Inherited structs embed parent fields at byte offset 0. A pointer to a child struct can be cast to a pointer to a parent struct without pointer offset adjustments.</li>
</ul>

<h3>1.3 Explicit Non-Goals and Constraints</h3>
<ul>
  <li><strong>No Borrow Checker:</strong> Rook does not analyze object lifetimes or lifetimes of references at compile time. Memory management is manual and explicit (stack allocations, <code>malloc</code>, <code>free</code>, and custom memory arenas).</li>
  <li><strong>No Garbage Collector:</strong> Rook has no runtime garbage collector, no reference counting headers, and no runtime engine. Program execution overhead is identical to optimized C.</li>
  <li><strong>No Hidden Control Flow:</strong> Rook does not support operator overloading, implicit type conversion between unrelated types, copy constructors, or exceptions. Control flow transitions are strictly visible in source code.</li>
  <li><strong>Static Dispatch Only:</strong> Method calls (<code>impl</code>) resolve statically at compile time to mangled symbol names. There are no virtual method tables (vtables) or dynamic dispatch overhead unless manually implemented via function pointer fields.</li>
</ul>
"""
    add_ch("philosophy", "1. The Rook Philosophy & Technical Constraints", ch1)

    # ==========================================
    # Chapter 2: Architecture & The Three Compiler Backends
    # ==========================================
    ch2 = f"""
<p>The <code>rokade</code> compiler implements a modular, multi-backend pipeline designed to produce either portable C source code or native machine code via LLVM.</p>

<div class="arch-diagram">
  <div class="arch-box">Source (<code>.rook</code>)</div>
  <div class="arch-arrow">➔</div>
  <div class="arch-box">Lexer &amp; Parser (AST)</div>
  <div class="arch-arrow">➔</div>
  <div class="arch-box">Sema &amp; Libclang Import</div>
  <div class="arch-arrow">➔</div>
  <div class="arch-split">
    <div class="arch-box branch"><strong>C Backend</strong><br>C11/C23 Transpiler<br>GCC / Clang Toolchain</div>
    <div class="arch-box branch"><strong>LLVM Backend</strong><br>LLVM C-API<br>Object (.o) / JIT</div>
    <div class="arch-box branch"><strong>LLVM2 Backend</strong><br>Direct IR Codegen<br>Strict C ABI Parity</div>
  </div>
</div>

<h3>2.1 Compiler Backends</h3>
<div class="table-container">
<table>
  <thead>
    <tr><th>Backend</th><th>Implementation</th><th>Target Output</th><th>Characteristics</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>C Backend (<code>--backend=c</code>)</strong></td>
      <td><code>src/c_backend.c</code></td>
      <td>C11 / C23 Source (<code>.c</code>)</td>
      <td>Default portable backend. Transpiles AST directly to standard C source and drives the host compiler (GCC or Clang). Compatible with all C profiling, debugging (GDB/LLDB), and external build systems.</td>
    </tr>
    <tr>
      <td><strong>LLVM Backend (<code>--backend=llvm</code>)</strong></td>
      <td><code>src/llvm_backend.c</code></td>
      <td>Native Object (<code>.o</code>) / Direct LLVM IR (<code>.ll</code>) / JIT</td>
      <td>Production native LLVM backend. Implements strict short-circuit control flow via basic blocks and PHI nodes, automated C typedef resolution, recursive struct sizing, union backing buffers, in-memory JIT execution (<code>rokade run --jit</code>), and zero-wrapper foreign function calls. (<code>--backend=llvm2</code> is supported as an alias).</td>
    </tr>
  </tbody>
</table>
</div>

<h3>2.2 Pipeline Stages</h3>
<ol>
  <li><strong>Lexing (<code>src/lexer.c</code>):</strong> Tokenizes source text into keywords, identifiers, literals, and operators. Comments and whitespace are discarded.</li>
  <li><strong>Parsing (<code>src/parse.c</code>):</strong> Builds an Abstract Syntax Tree (AST) using recursive descent parsing. Validates structural syntax (functions, structs, impls, sum types, defer statements).</li>
  <li><strong>Semantic Analysis (<code>src/sema.c</code>):</strong> Resolves symbol tables across lexical scopes, checks type compatibility, validates struct member access, and ensures pattern matching exhaustiveness.</li>
  <li><strong>Dynamic C Interop (<code>src/c_import.c</code>):</strong> When an <code>#include &lt;header.h&gt;</code> is encountered, libclang parses the header AST directly from system include paths and registers foreign functions, structs, typedefs, and enums into the semantic symbol table.</li>
  <li><strong>Code Generation:</strong> The selected backend walks the verified AST and generates target C code or LLVM IR.</li>
</ol>
"""
    add_ch("architecture", "2. Architecture & Compiler Backends", ch2)

    # ==========================================
    # Chapter 3: Technical Comparison: C, Rust, Zig, and Rook
    # ==========================================
    ch3 = f"""
<p>To understand Rook's technical position, we evaluate concrete design parameters against established systems languages: Standard C (C11/C23), Rust, and Zig.</p>

<div class="table-container">
<table>
  <thead>
    <tr>
      <th>Feature / Dimension</th>
      <th>Standard C (C11/C23)</th>
      <th>Rust (2024 Edition)</th>
      <th>Zig (0.13+)</th>
      <th>Rook (v0.6.1)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>Memory Model</strong></td>
      <td>Manual, uninitialized stack defaults, raw pointers.</td>
      <td>Affine type system, compile-time borrow checker, lifetimes.</td>
      <td>Manual with explicit allocators, slices, no hidden control flow.</td>
      <td>Manual, deterministic zero initialization, raw pointers, bounds checks.</td>
    </tr>
    <tr>
      <td><strong>C ABI Compatibility</strong></td>
      <td>Native (is C).</td>
      <td>Requires <code>extern "C"</code> blocks and bindings (<code>bindgen</code>).</td>
      <td>Requires <code>@cImport</code> and translated C type headers.</td>
      <td>Native 1:1 ABI mapping, dynamic libclang header parsing without wrappers.</td>
    </tr>
    <tr>
      <td><strong>Polymorphism</strong></td>
      <td>Manual void* casting or manual vtables.</td>
      <td>Traits, dynamic trait objects (vtables), generics.</td>
      <td>Compile-time duck typing (<code>comptime</code>).</td>
      <td>Single inheritance with prefix subtyping (zero-offset casting), static <code>impl</code> methods.</td>
    </tr>
    <tr>
      <td><strong>Algebraic Data Types</strong></td>
      <td>Manual tagged union (struct + enum + union).</td>
      <td>First-class <code>enum</code> with pattern matching.</td>
      <td>Tagged <code>union(enum)</code> with <code>switch</code>.</td>
      <td>First-class <code>sum</code> and <code>enum</code> with exhaustive <code>match</code>.</td>
    </tr>
    <tr>
      <td><strong>Resource Cleanup</strong></td>
      <td>Manual <code>goto cleanup</code> or compiler extensions.</td>
      <td>RAII (<code>Drop</code> trait implementation).</td>
      <td><code>defer</code> and <code>errdefer</code> statements.</td>
      <td><code>defer</code> statement with deterministic LIFO block unwinding.</td>
    </tr>
    <tr>
      <td><strong>Compiler &amp; Dependencies</strong></td>
      <td>Compiler toolchains (GCC, Clang, MSVC).</td>
      <td><code>rustc</code> + LLVM (~1.5 GB installation).</td>
      <td>Self-contained binary compiler with embedded Clang.</td>
      <td>Single lightweight C binary (~500 KB) with optional LLVM/libclang linking.</td>
    </tr>
  </tbody>
</table>
</div>

<h3>3.1 Memory Model &amp; Safety Trade-Offs</h3>
<p>Unlike Rust, Rook does not attempt to solve use-after-free or data races through static lifetime annotations. Lifetimes in systems code (such as game engine render graphs or operating system page tables) often resist compile-time tracking without substantial syntactic overhead. Rook instead mitigates common C failure modes through targeted mechanical rules:</p>
<ul>
  <li>Zero-initialization prevents uninitialized variable use.</li>
  <li>Condition expressions forbid assignment, preventing logic inversion.</li>
  <li>Runtime bounds checking (<code>-b</code> flag) validates array indexing.</li>
  <li><code>defer</code> statements ensure cleanup handlers are called across all exit paths.</li>
</ul>

<h3>3.2 C Interoperability Trade-Offs</h3>
<p>In Rust and Zig, interacting with complex C libraries (such as SDL3, Raylib, or GTK) requires generating bindings, translating macro definitions, and managing ABI boundary types. In Rook, the compiler directly invokes libclang on host system headers. Declarations like <code>GtkWidget*</code> or <code>SDL_Event</code> are ingested natively into the type checker without manual binding files.</p>
"""
    add_ch("c-comparison", "3. Technical Comparison: C, Rust, Zig, and Rook", ch3)

    # ==========================================
    # Chapter 4: Memory Layout & Storage Semantics
    # ==========================================
    ch4 = f"""
<p>Rook enforces predictable, hardware-aligned memory layouts matching the host architecture's standard C ABI.</p>

<h3>4.1 Primitive Scalar Storage</h3>
<p>Primitive types have fixed bit widths and natural byte alignments:</p>
<div class="table-container">
<table>
  <thead>
    <tr><th>Rook Type</th><th>C Equivalent</th><th>LLVM IR Type</th><th>Size (Bytes)</th><th>Alignment (Bytes)</th></tr>
  </thead>
  <tbody>
    <tr><td><code>bool</code></td><td><code>bool</code> / <code>_Bool</code></td><td><code>i8</code> / <code>i1</code></td><td>1</td><td>1</td></tr>
    <tr><td><code>char</code></td><td><code>char</code></td><td><code>i8</code></td><td>1</td><td>1</td></tr>
    <tr><td><code>int8_t</code> / <code>uint8_t</code></td><td><code>int8_t</code> / <code>uint8_t</code></td><td><code>i8</code></td><td>1</td><td>1</td></tr>
    <tr><td><code>int16_t</code> / <code>uint16_t</code></td><td><code>int16_t</code> / <code>uint16_t</code></td><td><code>i16</code></td><td>2</td><td>2</td></tr>
    <tr><td><code>int</code> / <code>int32_t</code></td><td><code>int</code> / <code>int32_t</code></td><td><code>i32</code></td><td>4</td><td>4</td></tr>
    <tr><td><code>int64_t</code> / <code>uint64_t</code></td><td><code>int64_t</code> / <code>uint64_t</code></td><td><code>i64</code></td><td>8</td><td>8</td></tr>
    <tr><td><code>float</code></td><td><code>float</code></td><td><code>float</code></td><td>4</td><td>4</td></tr>
    <tr><td><code>double</code></td><td><code>double</code></td><td><code>double</code></td><td>8</td><td>8</td></tr>
    <tr><td><code>T*</code> (Pointer)</td><td><code>T*</code></td><td><code>ptr</code></td><td>8 (on 64-bit)</td><td>8 (on 64-bit)</td></tr>
  </tbody>
</table>
</div>

<h3>4.2 Struct Memory Layout and Padding</h3>
<p>Struct fields are laid out in source declaration order. The compiler inserts padding bytes between fields to ensure each field starts at an address aligned to its natural alignment:</p>

{make_code_box("rook", """
struct Node {
    flag: bool     // 1 byte offset 0, followed by 3 bytes padding
    id: int        // 4 bytes offset 4
    ptr: void*     // 8 bytes offset 8
}
// Total size: 16 bytes, alignment: 8 bytes
""", "Struct Memory Layout Example")}

<h3>4.3 Single Inheritance Layout (Prefix Subtyping)</h3>
<p>When a <code>struct</code> inherits from another <code>struct</code>, the parent's fields are embedded at the very beginning of the child struct (offset 0):</p>

{make_code_box("rook", """
struct Entity {
    id: int;
    active: bool;
};

struct Player : Entity {
    health: int;
    score: int;
};
""", "Prefix Subtyping")}

<div class="table-container">
<table>
  <thead>
    <tr><th>Offset (Bytes)</th><th>Field</th><th>Type</th><th>Size</th><th>Belongs To</th></tr>
  </thead>
  <tbody>
    <tr><td>0</td><td><code>id</code></td><td><code>int</code></td><td>4</td><td><code>Entity</code> (Parent)</td></tr>
    <tr><td>4</td><td><code>active</code></td><td><code>bool</code></td><td>1</td><td><code>Entity</code> (Parent)</td></tr>
    <tr><td>5..7</td><td><em>Padding</em></td><td>—</td><td>3</td><td>Alignment padding</td></tr>
    <tr><td>8</td><td><code>health</code></td><td><code>int</code></td><td>4</td><td><code>Player</code> (Child)</td></tr>
    <tr><td>12</td><td><code>score</code></td><td><code>int</code></td><td>4</td><td><code>Player</code> (Child)</td></tr>
  </tbody>
</table>
</div>

<p>Because the parent struct is at byte offset 0, casting a child pointer to a parent pointer requires no pointer arithmetic:</p>
{make_code_box("rook", """
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
""", "Zero-Offset Upcasting")}
"""
    add_ch("mental-model", "4. Memory Layout & Storage Semantics", ch4)

    # ==========================================
    # Chapter 5: Type System & Semantic Analysis
    # ==========================================
    ch5 = f"""
<p>Rook utilizes a static, nominal type system with strict type checking and automated C typedef resolution.</p>

<h3>5.1 Variable Declarations and Type Inference</h3>
<p>Variables can be declared with an explicit type or inferred using the <code>let</code> keyword:</p>
{make_code_box("rook", """
int count = 10;          // Explicit scalar
let score = 250;         // Inferred as int
let ptr = &count;        // Inferred as int*
int uninitialized_val;   // Automatically initialized to 0
""", "Variable Declarations")}

<h3>5.2 Pointer Semantics and Void Pointers</h3>
<p>Pointers in Rook use standard C syntax (<code>T*</code>, <code>T**</code>). The type system enforces:</p>
<ul>
  <li><code>void*</code> is compatible with any pointer type (allowing standard memory allocators like <code>malloc</code> without explicit casts).</li>
  <li>Arithmetic on <code>void*</code> is prohibited at compile time; pointers must be cast to <code>char*</code> or <code>uint8_t*</code> before applying byte offsets.</li>
  <li>Pointer indirection is performed using either unary <code>*ptr</code> or member arrow <code>ptr->field</code> / dot <code>ptr.field</code>.</li>
</ul>

<h3>5.3 Dynamic Typedef Unwrapping (<code>ck_unwrap_typedef</code>)</h3>
<p>When external C headers are included, the semantic analyzer indexes all typedef aliases (such as <code>uint32_t</code>, <code>size_t</code>, <code>gchar</code>, <code>gboolean</code>, or <code>HWND</code>). During type checking, <code>ck_unwrap_typedef</code> recursively resolves typedef chains to verify structural equivalence:</p>
{make_code_box("rook", """
#include <stdio.h>
#include <stdint.h>

// uint32_t is a typedef alias to 'unsigned int' in C standard headers.
// The compiler unwraps typedef aliases dynamically to verify exact compatibility:
int main() {
    uint32_t count = 42;
    unsigned int raw = count; // Exact compatibility verified by ck_unwrap_typedef
    return 0;
}
""", "C Typedef Resolution")}
"""
    add_ch("variables", "5. Type System & Semantic Analysis", ch5)

    # ==========================================
    # Chapter 6: Control Flow & Evaluation Rules
    # ==========================================
    ch6 = f"""
<p>Control flow structures in Rook provide strict compile-time invariants to eliminate common C logic bugs.</p>

<h3>6.1 Conditionals &amp; Assignment Rejection</h3>
<p>Condition expressions in <code>if</code> and <code>while</code> statements must evaluate to boolean or numeric truth values. Assignment operators (<code>=</code>, <code>+=</code>, etc.) are syntactically rejected inside conditions:</p>
{make_code_box("rook", """
int main() {
    int x = 0;
    if (x == 5) {
        // Valid equality check
    }

    // if (x = 5) { } // Hard compile-time error: assignment not allowed in condition
    return 0;
}
""", "Condition Invariant")}

<h3>6.2 Guaranteed Short-Circuit Evaluation</h3>
<p>In both the C and LLVM2 backends, logical AND (<code>&&</code>) and logical OR (<code>||</code>) operators guarantee short-circuit evaluation. The right-hand operand is never evaluated if the left-hand operand determines the result:</p>
{make_code_box("rook", """
int* ptr = NULL;
if (ptr != NULL && *ptr == 10) {
    // Safe: '*ptr' is never evaluated when ptr is NULL
}
""", "Short-Circuit Guard")}

<p>In the LLVM2 backend, short-circuiting is lowered using explicit basic blocks (<code>land.rhs</code>, <code>land.merge</code>) and PHI nodes:</p>
{make_code_box("llvm", """
  %lhs_val = icmp ne ptr %ptr, null
  br i1 %lhs_val, label %land.rhs, label %land.merge

land.rhs:
  %deref = load i32, ptr %ptr, align 4
  %rhs_val = icmp eq i32 %deref, 10
  br label %land.merge

land.merge:
  %result = phi i1 [ false, %entry ], [ %rhs_val, %land.rhs ]
""", "LLVM2 Lowering of &&")}

<h3>6.3 Loops</h3>
{make_code_box("rook", """
#include <stdio.h>

int main() {
    // Standard while loop
    int n = 0;
    while (n < 5) {
        n = n + 1;
    }

    // Three-clause for loop
    for (int i = 0; i < 10; i++) {
        printf("%d\\n", i);
    }

    // Array literal iteration (for-in)
    for item in [10, 20, 30, 40] {
        printf("%d\\n", item);
    }
    return 0;
}
""", "Loops in Rook")}

<h3>6.4 Deterministic Cleanup with <code>defer</code></h3>
<p>The <code>defer</code> statement registers a statement or block to be executed upon leaving the current enclosing scope. Multiple <code>defer</code> statements execute in reverse declaration order (Last-In, First-Out / LIFO):</p>
{make_code_box("rook", """
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
""", "Deterministic Resource Cleanup")}
"""
    add_ch("control-flow", "6. Control Flow & Evaluation Rules", ch6)

    # ==========================================
    # Chapter 7: Algebraic Data Types: sum, enum & match
    # ==========================================
    ch7 = f"""
<p>Rook provides first-class Algebraic Data Types (ADTs) through <code>sum</code> and <code>enum</code> declarations, accompanied by compile-time exhaustive <code>match</code> expressions.</p>

<h3>7.1 Simple Enums</h3>
<p>A simple <code>enum</code> defines named numeric constants matching C enum semantics:</p>
{make_code_box("rook", """
enum Direction {
    North,
    East,
    South,
    West
}
""", "Simple Enum")}

<h3>7.2 Sum Types (Tagged Unions)</h3>
<p>A <code>sum</code> type represents a value that can take one of several variant shapes, each optionally carrying payload data:</p>
{make_code_box("rook", """
sum Shape {
    Circle { radius: float; };
    Rectangle { width: float; height: float; };
    Point;
}
""", "Sum Type Declaration")}

<p>In memory, a <code>sum</code> is represented as a tagged struct containing an integer discriminator tag followed by a union buffer sized to the largest variant:</p>
<div class="table-container">
<table>
  <thead>
    <tr><th>Offset</th><th>Member</th><th>Description</th></tr>
  </thead>
  <tbody>
    <tr><td>0..3</td><td><code>tag: int32_t</code></td><td>Numeric variant index (0 = Circle, 1 = Rectangle, 2 = Point)</td></tr>
    <tr><td>4..7</td><td><em>Padding</em></td><td>Natural alignment padding</td></tr>
    <tr><td>8..15</td><td><code>payload: union</code></td><td>Sized to <code>max(sizeof(Circle), sizeof(Rectangle))</code> = 8 bytes</td></tr>
  </tbody>
</table>
</div>

<h3>7.3 Pattern Matching and Exhaustiveness</h3>
<p>The <code>match</code> construct evaluates a sum type against its possible variants. Struct patterns unpack field members directly into scope:</p>
{make_code_box("rook", """
sum Shape {
    Circle { radius: float; };
    Rectangle { width: float; height: float; };
    Point;
}

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
""", "Pattern Matching")}

<h3>7.4 Canonical Error &amp; Option Handling: <code>std/result</code></h3>
<p>The standard library provides zero-cost algebraic sum types for error handling and optional values without runtime exceptions:</p>
{make_code_box("rook", """
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
        printf("Error: %s\\n", res.unwrap_err());
    }
    return 0;
}
""", "Error Handling with std/result")}
"""
    add_ch("sum-match", "7. Algebraic Data Types: sum, enum & match", ch7)

    # ==========================================
    # Chapter 8: Zero-Overhead C Interoperability & Libclang Integration
    # ==========================================
    ch8 = f"""
<p>Rook interfaces directly with standard C libraries without manual foreign function interface (FFI) bindings or glue code.</p>

<h3>8.1 Dynamic Header Compilation with Libclang</h3>
<p>When Rook encounters <code>#include &lt;header.h&gt;</code> in a source file, <code>c_import.c</code> invokes the libclang parser. It traverses the Clang Translation Unit AST in memory, extracting:</p>
<ul>
  <li>Function declarations, return types, and parameter signatures.</li>
  <li>Struct and union definitions with exact byte offsets and padding.</li>
  <li>Typedef aliases and underlying canonical types.</li>
  <li>Preprocessor enum constants and global variables.</li>
</ul>

<h3>8.2 Function Pointer Callbacks</h3>
<p>Rook functions matching C callback prototypes can be passed directly as C function pointers:</p>
{make_code_box("rook", """
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
    printf("\\n");
    return 0;
}
""", "Passing Function Pointers to C Callbacks")}

<h3>8.3 Managing C Dependencies via <code>rokade.toml</code></h3>
<p>Project dependencies on system libraries are declared in <code>rokade.toml</code> under <code>pkg-config</code> in the <code>[build]</code> table:</p>
{make_code_box("toml", """
[package]
name = "gui_app"
version = "0.1.0"

[build]
kind = "exe"
pkg-config = ["raylib"]
""", "rokade.toml")}
<p>The compiler automatically queries <code>pkg-config --cflags</code> and <code>pkg-config --libs</code> during build execution.</p>
"""
    add_ch("c-interop", "8. Zero-Overhead C Interoperability & Libclang Integration", ch8)

    # ==========================================
    # Chapter 9: Project Composition: Inclusions, Modules & Cross-Path Ingestion
    # ==========================================
    ch9 = f"""
<p>Rook separates the ingestion of foreign C interfaces from native Rook module composition via two distinct directives: <code>#include</code> and <code>#comprise</code>.</p>

<h3>9.1 The Two Ingestion Models: <code>#include</code> vs <code>#comprise</code></h3>
<div class="table-container">
<table>
  <thead>
    <tr><th>Directive</th><th>Target File Type</th><th>Parsing Engine</th><th>Behavior &amp; Semantics</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><code>#include &lt;header.h&gt;</code><br><code>#include "header.h"</code></td>
      <td>C Header (<code>.h</code>)</td>
      <td>libclang (Clang C Frontend)</td>
      <td>Parses host C headers dynamically at compile time. Ingests C functions, structs, unions, typedefs, enums, and macros directly into Rook's symbol table without hand-written binding layers.</td>
    </tr>
    <tr>
      <td><code>#comprise &lt;std/mod&gt;</code><br><code>#comprise "path.rook"</code></td>
      <td>Rook Source (<code>.rook</code>)</td>
      <td>Rokade Preprocessor</td>
      <td>Inlines and resolves Rook source modules. Expands declarations into the compilation unit with canonical path deduplication.</td>
    </tr>
  </tbody>
</table>
</div>

<h3>9.2 Cross-Directory Module Ingestion &amp; Relative Path Resolution</h3>
<p>Rook source files can be composed across nested directories using standard POSIX relative paths:</p>
<ul>
  <li><strong>Standard Library Modules:</strong> Use angle brackets with the <code>std/</code> prefix, e.g., <code>#comprise &lt;std/io&gt;</code> or <code>#comprise &lt;std/str&gt;</code>. The compiler resolves these from the toolchain installation directory or <code>ROKADE_PATH</code>.</li>
  <li><strong>Sibling Modules:</strong> Use quoted relative paths, e.g., <code>#comprise "utils.rook"</code> or <code>#comprise "./types.rook"</code>.</li>
  <li><strong>Nested Subdirectories:</strong> Specify the relative path to sub-modules, e.g., <code>#comprise "engine/renderer.rook"</code> or <code>#comprise "net/socket.rook"</code>.</li>
  <li><strong>Parent and Sibling Trees:</strong> Walk up directory hierarchies using <code>../</code>, e.g., <code>#comprise "../shared/config.rook"</code>.</li>
</ul>

{make_callout("spec", "Canonical Path Deduplication", "Rook resolves every included file to its canonical filesystem path (via <code>rk_realpath</code>). If multiple modules comprise the same source file—either directly or transitively—the compiler expands and parses that file exactly once. Circular comprises are automatically prevented, eliminating the need for C-style <code>#ifndef</code> include guards.")}

<h3>9.3 Multi-File Project Architecture (Comprehensive Example)</h3>
<p>Consider a modular systems project structured across directories with external C dependencies:</p>

{make_code_box("text", """
my_game/
├── rokade.toml
└── src/
    ├── main.rook
    ├── config.rook
    └── engine/
        ├── math.rook
        └── renderer.rook
""", "Project Directory Structure")}

<p>In <code>src/engine/math.rook</code>, we define local geometry types:</p>
{make_code_box("rook", """
// src/engine/math.rook
struct Vec2 {
    x: float;
    y: float;
};

Vec2 vec2_new(float x, float y) {
    return Vec2 { x: x, y: y };
}
""", "src/engine/math.rook")}

<p>In <code>src/engine/renderer.rook</code>, we combine local math, standard strings, and native Raylib C headers:</p>
{make_code_box("rook", """
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
""", "src/engine/renderer.rook")}

<p>In <code>src/main.rook</code>, we link the entire project together:</p>
{make_code_box("rook", """
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
""", "src/main.rook")}
"""
    add_ch("composition", "9. Project Composition: Inclusions, Modules & Cross-Path Ingestion", ch9)

    # ==========================================
    # Chapter 10: The Rook Standard Library (std/) Deep Dive
    # ==========================================
    ch10 = f"""
<p>The Rook Standard Library resides in <code>std/</code>. It is written purely in Rook, carries zero runtime overhead, and relies exclusively on standard C ABI primitives.</p>

<h3>10.1 Safe String Slices: <code>std/str</code></h3>
<p>C strings (<code>char*</code>) are null-terminated, requiring <code>O(n)</code> scans for length calculations and risking buffer overflows. Rook's <code>Str</code> provides a safe, non-owning slice holding a pointer and an explicit length:</p>

{make_code_box("rook", """
struct Str {
    data: const char*;
    len: size_t;
};
""", "Str Internal Representation")}

<h4>Core Capabilities of <code>Str</code>:</h4>
<ul>
  <li><strong>Zero-Allocation Slicing:</strong> <code>s.slice(start, end)</code> returns a new <code>Str</code> view over existing memory in <code>O(1)</code> time without allocating.</li>
  <li><strong>Safe Inspection:</strong> <code>s.is_empty()</code>, <code>s.starts_with(prefix)</code>, <code>s.ends_with(suffix)</code>, <code>s.equals(other)</code>, <code>s.char_at(index)</code>.</li>
  <li><strong>Searching &amp; Splitting:</strong> <code>s.find_char(c)</code>, <code>s.find(needle)</code>, <code>s.contains(needle)</code>, <code>s.split_once(delim, &left, &right)</code>.</li>
  <li><strong>Whitespace Trimming:</strong> <code>s.trim_start()</code>, <code>s.trim_end()</code>, <code>s.trim()</code>.</li>
  <li><strong>Parsing &amp; Conversions:</strong> <code>s.to_int()</code>, <code>s.to_float()</code>, <code>s.to_bool()</code>, <code>s.to_cstr()</code> (heap-allocated copy).</li>
</ul>

{make_code_box("rook", """
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
""", "Working with std/str")}

<h3>10.2 Ergonomic Input/Output: <code>std/io</code></h3>
<p><code>std/io</code> replaces raw <code>printf</code>/<code>scanf</code> with type-safe, bounds-checked I/O routines:</p>
<ul>
  <li><strong>Printing:</strong> <code>print(s)</code>, <code>println(s)</code>, <code>println_int(n)</code>, <code>println_float(f)</code>, <code>println_bool(b)</code>, <code>println_str(s)</code>.</li>
  <li><strong>Standard Error:</strong> <code>eprint(s)</code>, <code>eprintln(s)</code>, <code>eprintln_int(n)</code>, <code>eprintln_str(s)</code>, <code>io_eflush()</code>.</li>
  <li><strong>Safe Input Scanning:</strong> <code>scanln(buf, cap)</code>, <code>scanln_alloc()</code>, <code>scan_word(buf, cap)</code>, <code>scan_int(&out)</code>, <code>scan_float(&out)</code>.</li>
</ul>

<h3>10.3 High-Performance Memory Management: <code>std/mem</code></h3>
<p>In high-throughput systems, invoking <code>malloc</code>/<code>free</code> repeatedly fragments the heap and incurs allocator lock contention. <code>std/mem</code> provides specialized allocators:</p>

<div class="table-container">
<table>
  <thead>
    <tr><th>Allocator</th><th>Allocation Pattern</th><th>Deallocation Mechanism</th><th>Use Case</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><strong><code>Arena</code></strong></td>
      <td>Linear monotonic bump pointer. Aligns all allocations to 8-byte boundaries.</td>
      <td>Bulk reset via <code>arena.reset()</code> or free via <code>arena.destroy()</code>.</td>
      <td>Per-frame allocations in games, request-scoped lifecycles in network servers, AST compilation phases.</td>
    </tr>
    <tr>
      <td><strong><code>ArenaTemp</code></strong></td>
      <td>Saves current arena offset. Sub-allocations increment offset.</td>
      <td>Restores offset via <code>arena_temp_end()</code>.</td>
      <td>Temporary scratchpad buffers inside inner loops or subroutines.</td>
    </tr>
    <tr>
      <td><strong><code>ElementPool</code></strong></td>
      <td>Fixed-size chunk allocator with embedded free-list recycling.</td>
      <td>Instantaneous <code>O(1)</code> return via <code>pool.free(ptr)</code>.</td>
      <td>Game entities, network connection slots, graph nodes, AST nodes.</td>
    </tr>
  </tbody>
</table>
</div>

{make_code_box("rook", """
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
""", "Using Arena and ElementPool")}

<h3>10.4 Hardware Lock-Free Atomics: <code>std/atomic</code></h3>
<p>Rook exposes direct CPU memory bus atomic operations without library indirection:</p>
<ul>
  <li><code>AtomicInt</code> &amp; <code>AtomicBool</code>: wrappers over machine-word atomic storage.</li>
  <li><strong>Operations:</strong> <code>load()</code>, <code>store(val)</code>, <code>fetch_add(delta)</code>, <code>fetch_sub(delta)</code>, <code>exchange(val)</code>, and <code>compare_exchange(&expected, desired)</code>.</li>
</ul>

<h3>10.5 Multi-Threading &amp; Synchronization: <code>std/sync</code></h3>
<p>Provides zero-overhead abstractions over host OS threads (POSIX pthreads / Windows Win32 threads):</p>
<ul>
  <li><strong><code>Thread</code>:</strong> Created with <code>thread_spawn(worker_fn, arg)</code>. Joined via <code>t.join(&ret_val)</code>.</li>
  <li><strong><code>Mutex</code>:</strong> Native OS mutual exclusion lock (<code>m.init()</code>, <code>m.lock()</code>, <code>m.unlock()</code>, <code>m.try_lock()</code>, <code>m.destroy()</code>).</li>
  <li><strong><code>CondVar</code>:</strong> Condition variable for thread coordination (<code>cv.init()</code>, <code>cv.wait(&mutex)</code>, <code>cv.signal()</code>, <code>cv.broadcast()</code>).</li>
</ul>

<h3>10.6 Concurrency Safety Model &amp; Current Limitations</h3>
{make_callout("warn", "Explicit Concurrency Model", "Rook does not incorporate a compile-time borrow checker or automatic race detector. Thread safety is explicit and the responsibility of the systems programmer.")}

<p>When developing concurrent software in Rook, developers must enforce the following architectural rules:</p>
<ol>
  <li><strong>Shared Mutable State Must Be Synchronized:</strong> Any data structure accessible by multiple threads must be protected by a <code>Mutex</code> or implemented with lock-free atomic primitives (<code>AtomicInt</code>). Unsynchronized concurrent writes cause undefined behavior.</li>
  <li><strong>Thread Worker Argument Lifetimes:</strong> The <code>void* arg</code> passed to <code>thread_spawn</code> must remain valid until the spawned thread finishes executing. Passing a pointer to a local stack variable of a function that returns before <code>t.join()</code> results in a dangling pointer read. Shared state should be allocated on the heap or in an Arena that outlives all worker threads.</li>
  <li><strong>Deadlock Prevention with <code>defer</code>:</strong> Always unlock mutexes using <code>defer m.unlock()</code> immediately after acquiring them. This guarantees the lock is released across all return paths and branches.</li>
</ol>

{make_code_box("rook", """
#include <stdio.h>
#comprise <std/sync>
#comprise <std/atomic>

struct CounterTask {
    counter: AtomicInt;
    lock: Mutex;
};

void* worker(void* arg) {
    CounterTask* task = (CounterTask*)arg;
    for (int i = 0; i < 1000; i = i + 1) {
        task->lock.lock();
        task->counter.fetch_add(1);
        task->lock.unlock();
    }
    return NULL;
}

int main() {
    CounterTask task;
    task.counter = atomic_int_new(0);
    task.lock.init();
    defer task.lock.destroy();

    Thread t1 = thread_spawn(worker, &task);
    Thread t2 = thread_spawn(worker, &task);

    void* r1 = NULL;
    void* r2 = NULL;
    t1.join(&r1);
    t2.join(&r2);

    printf("Final counter: %d\\n", task.counter.load());
    return 0;
}
""", "Safe Concurrency with Thread, Mutex, and AtomicInt")}

<h3>10.7 Null Safety &amp; Explicit Errors: <code>std/option</code> &amp; <code>std/result</code></h3>
<ul>
  <li><strong><code>std/option</code>:</strong> Replaces unchecked nullable pointers with <code>Option</code> (<code>option_some(val)</code>, <code>option_none()</code>, <code>Option_unwrap(&opt)</code>, <code>Option_unwrap_or(&opt, default)</code>).</li>
  <li><strong><code>std/result</code>:</strong> Explicit error propagation (<code>result_ok(val)</code>, <code>result_err(code)</code>, <code>Result_is_ok(&res)</code>, <code>Result_unwrap(&res)</code>) eliminating silent error code ignoring.</li>
</ul>
"""
    add_ch("stdlib", "10. The Rook Standard Library (std/) Deep Dive", ch10)

    # ==========================================
    # Chapter 11: Idiomatic Data Structure Implementation in Rook
    # ==========================================
    ch11 = f"""
<p>Building high-performance data structures in Rook leverages explicit memory allocation, pointers, struct methods via <code>impl</code>, and deterministic cleanup via <code>defer</code>.</p>

<h3>11.1 Dynamic Array (Vector)</h3>
<p>Here is an idiomatic resizable integer vector demonstrating explicit allocation, growth doubling, and bounds checking:</p>

{make_code_box("rook", """
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

    printf("Element at 5: %d\\n", v.get(5)); // Prints: 50
    return 0;
}
""", "Idiomatic Dynamic Array in Rook")}

<h3>11.2 Singly Linked List with Recycled Allocations</h3>
<p>By pairing custom node structures with <code>ElementPool</code> from <code>std/mem</code>, linked lists achieve cache locality and zero heap fragmentation:</p>

{make_code_box("rook", """
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
        printf("NULL\\n");
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
""", "Linked List Powered by ElementPool")}
"""
    add_ch("data-structures", "11. Idiomatic Data Structure Implementation in Rook", ch11)

    # ==========================================
    # Chapter 12: Configuration, Multi-Target Builds & Toolchains
    # ==========================================
    ch12 = f"""
<p>The Rokade build system manages compilation, cross-compilation, and language server diagnostics.</p>

<h3>12.1 CLI Commands</h3>
{make_code_box("bash", """
# Compile and run project
rokade run [path] [--backend=c|llvm|llvm2]

# Build release binary
rokade build [path] [--backend=c|llvm|llvm2]

# Run one-shot health and toolchain verification
rokade doctor

# Query or modify toolchain overrides
rokade toolchain
rokade toolchain set cc /usr/bin/clang
""", "Common CLI Commands")}

<h3>12.2 Cross-Compilation</h3>
<p>Rokade supports cross-compilation targets out of the box:</p>
<ul>
  <li><strong>Windows (x86_64-w64-mingw32):</strong> Compiles via MinGW-w64 GCC toolchain.</li>
  <li><strong>Android (aarch64-linux-android):</strong> Cross-compiles using the Android NDK Clang toolchain and target sysroot.</li>
</ul>

<h3>12.3 Language Server Protocol (<code>rook-lsp</code>)</h3>
<p>The official language server (written in Rust) provides editor integration for editors including Zed, VSCode, and Neovim. It provides syntax validation, semantic diagnostics via <code>rokade --diagnostics</code>, jump-to-definition (<code>--def-at</code>), and document outlines (<code>--symbols</code>).</p>
"""
    add_ch("config", "12. Configuration, Multi-Target Builds & Toolchains", ch12)

    return chapters

