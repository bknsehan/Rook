#!/usr/bin/env python3
"""
docs_guide.py
Technical reference manual for the Rook programming language and Rokade compiler (v0.5.2).
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
      <td>Default backend. Transpiles AST directly to clean C source and calls the system C compiler (GCC or Clang). Compatible with all C profiling, debugging (GDB/LLDB), and build systems.</td>
    </tr>
    <tr>
      <td><strong>LLVM Backend (<code>--backend=llvm</code>)</strong></td>
      <td><code>src/llvm_backend.c</code></td>
      <td>Native Object (<code>.o</code>) / JIT</td>
      <td>Emits LLVM IR via LLVM-C API. Supports in-memory execution via <code>rokade run --jit</code>. Useful for rapid scripting and automated compiler tests without intermediate disk artifacts.</td>
    </tr>
    <tr>
      <td><strong>LLVM2 Backend (<code>--backend=llvm2</code>)</strong></td>
      <td><code>src/llvm2_backend.c</code></td>
      <td>Direct LLVM IR (<code>.ll</code> / <code>.o</code>)</td>
      <td>Next-generation native backend. Implements strict short-circuit control flow via basic blocks and PHI nodes, automated C typedef resolution, recursive struct sizing, union backing buffers, and zero-wrapper foreign function calls.</td>
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
    add_ch("architecture", "2. Architecture & The Three Compiler Backends", ch2)

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
      <th>Rook (v0.5.2)</th>
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
<p>When an <code>object</code> inherits from another <code>object</code> or <code>struct</code>, the parent's fields are embedded at the very beginning of the child struct (offset 0):</p>

{make_code_box("rook", """
object Entity {
    id: int
    active: bool
}

object Player : Entity {
    health: int
    score: int
}
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
Player p;
Entity* e = (Entity*)&p; // Exact same memory address, zero adjustment
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
#include <gtk/gtk.h>

// gtk_window_get_title returns 'const gchar*'
// In GLib, 'typedef char gchar;'
// The compiler unwraps 'gchar' to 'char', verifying exact compatibility:
const char* title = gtk_window_get_title(win);
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
int x = 0;
if (x == 5) {
    // Valid equality check
}

// if (x = 5) { } // Hard compile-time error: assignment not allowed in condition
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
// Standard while loop
while (condition) {
    step();
}

// Three-clause for loop
for (int i = 0; i < 10; i++) {
    printf("%d\\n", i);
}

// Array iteration (for-in)
int values[4] = { 10, 20, 30, 40 };
for item in values {
    printf("%d\\n", item);
}
""", "Loops in Rook")}

<h3>6.4 Deterministic Cleanup with <code>defer</code></h3>
<p>The <code>defer</code> statement registers a statement or block to be executed upon leaving the current enclosing scope. Multiple <code>defer</code> statements execute in reverse declaration order (Last-In, First-Out / LIFO):</p>
{make_code_box("rook", """
FILE* f = fopen("data.bin", "rb");
if (!f) return -1;
defer fclose(f); // Guaranteed to execute on any exit path

void* buf = malloc(1024);
defer free(buf); // Executes before fclose(f)

if (error_condition()) {
    return -2; // buf is freed, then f is closed
}
return 0;      // buf is freed, then f is closed
""", "Deterministic Resource Cleanup")}
"""
    add_ch("control-flow", "6. Control Flow & Evaluation Rules", ch6)

    # ==========================================
    # Chapter 7: Algebraic Data Types: sum, enum & match
    # ==========================================
    ch7 = f"""
<p>Rook provides first-class Algebraic Data Types (ADTs) through <code>sum</code> and <code>enum</code> declarations, accompanied by compile-time exhaustive <code>match</code> statements.</p>

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
    Circle { radius: float },
    Rectangle { width: float, height: float },
    Point
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
<p>The <code>match</code> construct tests a sum type against its possible variants. The compiler enforces that all variants are handled:</p>
{make_code_box("rook", """
float get_area(Shape s) {
    match (s) {
        Circle(c) => return 3.14159 * c.radius * c.radius;
        Rectangle(r) => return r.width * r.height;
        Point => return 0.0;
    }
}
""", "Pattern Matching")}

<h3>7.4 Error Handling with <code>Result</code> and <code>?</code> Operator</h3>
<p>The standard library provides <code>Result</code> and <code>Option</code> sum types. The postfix <code>?</code> operator unwraps a successful value or early-returns the error variant from the enclosing function:</p>
{make_code_box("rook", """
#comprise std/io

Result<int, IOError> read_config() {
    File f = File::open("config.json", "r")?; // Early return if Err
    defer f.close();
    int val = f.read_int()?;
    return Result::Ok(val);
}
""", "Error Propagation with ?")}
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
<p>Rook functions matching C callback prototypes can be passed directly as function pointers:</p>
{make_code_box("rook", """
#include <stdio.h>
#include <Elementary.h>

void on_click(void* data, Evas_Object* obj, void* event_info) {
    printf("Button clicked!\\n");
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
""", "Passing Function Pointers to C Callbacks")}

<h3>8.3 Managing C Dependencies via <code>rokade.toml</code></h3>
<p>Project dependencies on system libraries are declared in <code>rokade.toml</code> under <code>pkg-config</code>:</p>
{make_code_box("toml", """
[project]
name = "gui_app"
version = "0.1.0"
pkg-config = ["raylib", "elementary", "sqlite3"]
""", "rokade.toml")}
<p>The compiler automatically queries <code>pkg-config --cflags</code> and <code>pkg-config --libs</code> during build execution.</p>
"""
    add_ch("c-interop", "8. Zero-Overhead C Interoperability & Libclang Integration", ch8)

    # ==========================================
    # Chapter 9: Standard Library (std/) Architecture
    # ==========================================
    ch9 = f"""
<p>Rook's standard library resides in <code>std/</code> and provides fundamental primitives implemented directly in Rook.</p>

<h3>9.1 Modules Overview</h3>
<div class="table-container">
<table>
  <thead>
    <tr><th>Module</th><th>Header</th><th>Primary Capabilities</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><strong><code>std/io</code></strong></td>
      <td><code>#comprise std/io</code></td>
      <td>Buffered stream I/O, file handle abstraction, line-by-line reading, binary read/write operations.</td>
    </tr>
    <tr>
      <td><strong><code>std/math</code></strong></td>
      <td><code>#comprise std/math</code></td>
      <td>Arithmetic utility routines: <code>min</code>, <code>max</code>, <code>clamp</code>, <code>abs</code>, power functions, floating-point comparisons.</td>
    </tr>
    <tr>
      <td><strong><code>std/json</code></strong></td>
      <td><code>#comprise std/json</code></td>
      <td>Lightweight recursive descent JSON parser and serializer with zero external dependencies.</td>
    </tr>
    <tr>
      <td><strong><code>std/log</code></strong></td>
      <td><code>#comprise std/log</code></td>
      <td>Structured leveled logging (<code>DEBUG</code>, <code>INFO</code>, <code>WARN</code>, <code>ERROR</code>) with ISO-8601 timestamps and terminal styling.</td>
    </tr>
    <tr>
      <td><strong><code>std/test</code></strong></td>
      <td><code>#comprise std/test</code></td>
      <td>Unit testing framework: assertion macros (<code>assert_eq</code>, <code>assert_true</code>), test harness, and failure reports.</td>
    </tr>
  </tbody>
</table>
</div>
"""
    add_ch("stdlib", "9. Standard Library (std/) Architecture", ch9)

    # ==========================================
    # Chapter 10: Configuration, Multi-Target Builds & Toolchains
    # ==========================================
    ch10 = f"""
<p>The Rokade build system manages compilation, cross-compilation, and language server diagnostics.</p>

<h3>10.1 CLI Commands</h3>
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

<h3>10.2 Cross-Compilation</h3>
<p>Rokade supports cross-compilation targets out of the box:</p>
<ul>
  <li><strong>Windows (x86_64-w64-mingw32):</strong> Compiles via MinGW-w64 GCC toolchain.</li>
  <li><strong>Android (aarch64-linux-android):</strong> Cross-compiles using the Android NDK Clang toolchain and target sysroot.</li>
</ul>

<h3>10.3 Language Server Protocol (<code>rook-lsp</code>)</h3>
<p>The official language server (written in Rust) provides editor integration for editors including Zed, VSCode, and Neovim. It provides syntax validation, semantic diagnostics via <code>rokade --diagnostics</code>, jump-to-definition (<code>--def-at</code>), and document outlines (<code>--symbols</code>).</p>
"""
    add_ch("config", "10. Configuration, Multi-Target Builds & Toolchains", ch10)

    return chapters
