#!/usr/bin/env python3
"""
docs_beginner.py
Systems programming foundations course for learners studying computer systems fundamentals using Rook.
Focuses on hardware mechanics, memory layouts, pointers, and program execution.
"""

def get_beginner_modules(make_code_box, make_callout):
    modules = []

    def add_mod(mid, title, content):
        modules.append((mid, title, content))

    # ============================================================
    # Module B1: Hardware Architecture & The Memory Hierarchy
    # ============================================================
    m1 = f"""
<p>To write reliable systems code, you must understand how software interacts directly with physical hardware: the processor, memory, and permanent storage.</p>

<h3>1.1 Hardware Components: Storage, RAM, and the CPU</h3>
<div class="arch-diagram">
  <div class="arch-box"><strong>Permanent Storage (SSD / NVMe)</strong><br>Terabytes capacity<br>~10–50 microseconds latency</div>
  <div class="arch-arrow">➔</div>
  <div class="arch-box"><strong>Main Memory (RAM)</strong><br>Gigabytes capacity<br>~50–100 nanoseconds latency</div>
  <div class="arch-arrow">➔</div>
  <div class="arch-box"><strong>CPU Registers &amp; Caches</strong><br>Kilobytes / Megabytes<br>~0.5–5 nanoseconds latency</div>
</div>

<ul>
  <li><strong>Permanent Storage:</strong> Retains data when powered down. Executable binary files are stored on disk. The CPU cannot execute instructions directly from an SSD.</li>
  <li><strong>RAM (Random Access Memory):</strong> A vast array of byte storage cells indexed by numerical addresses. When an application runs, its machine code and active data are mapped into RAM.</li>
  <li><strong>CPU Registers:</strong> Small, high-speed storage slots inside the processor core (such as <code>RAX</code>, <code>RSP</code>, <code>RIP</code> on x86-64). Operations like addition and comparisons execute directly inside registers.</li>
</ul>

<h3>1.2 The Stack vs. The Heap</h3>
<p>When an operating system executes a Rook binary, the process memory space is partitioned into distinct functional segments:</p>
<div class="table-container">
<table>
  <thead>
    <tr><th>Memory Segment</th><th>Allocation Mechanism</th><th>Lifetime</th><th>Speed</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>Call Stack</strong></td>
      <td>Automatic: Adjusting the Stack Pointer register (<code>RSP</code>).</td>
      <td>Scoped: Deallocated automatically when the function returns.</td>
      <td>Instantaneous (a single CPU subtraction instruction).</td>
    </tr>
    <tr>
      <td><strong>The Heap</strong></td>
      <td>Manual: Managed by an allocator (<code>malloc</code> / <code>free</code>).</td>
      <td>Dynamic: Persists until explicitly released by the programmer.</td>
      <td>Requires allocator search algorithms and operating system page mapping.</td>
    </tr>
  </tbody>
</table>
</div>
"""
    add_mod("b1-hardware-memory", "Module B1: Hardware Architecture & Memory Hierarchy", m1)

    # ============================================================
    # Module B2: Your First Program & Execution Lifecycle
    # ============================================================
    m2 = f"""
<p>In Rook, every executable program begins execution at the <code>main</code> function.</p>

<h3>2.1 Anatomy of a Rook Program</h3>
{make_code_box("rook", """
#include <stdio.h>

int main() {
    printf("Hello from Rook systems code!\\n");
    return 0; // Return code 0 indicates success to the operating system
}
""", "src/main.rook")}

<h3>2.2 The Compilation and Execution Flow</h3>
<p>When you run <code>rokade run</code>, three concrete actions occur:</p>
<ol>
  <li><strong>Translation &amp; Assembly:</strong> The compiler translates <code>main.rook</code> into machine instructions.</li>
  <li><strong>Linking:</strong> The object file is combined with system libraries (like the C standard runtime <code>libc</code>) to produce a standalone executable binary.</li>
  <li><strong>Process Spawning:</strong> The operating system kernel creates a new process, allocates virtual memory, loads the binary instructions, and points the Instruction Pointer register to <code>main</code>.</li>
</ol>
"""
    add_mod("b2-first-program", "Module B2: Your First Program & Execution Lifecycle", m2)

    # ============================================================
    # Module B3: Bits, Bytes, and Integer Widths
    # ============================================================
    m3 = f"""
<p>In systems programming, data types represent concrete physical bit patterns in hardware.</p>

<h3>3.1 Scalar Types and Hardware Bit Widths</h3>
<p>Rook provides fixed-width integers so memory footprints remain exact across different processor targets:</p>
<div class="table-container">
<table>
  <thead>
    <tr><th>Type</th><th>Bit Width</th><th>Byte Size</th><th>Signed Range</th><th>Unsigned Equivalent</th></tr>
  </thead>
  <tbody>
    <tr><td><code>int8_t</code></td><td>8 bits</td><td>1 byte</td><td>-128 to 127</td><td><code>uint8_t</code> (0 to 255)</td></tr>
    <tr><td><code>int16_t</code></td><td>16 bits</td><td>2 bytes</td><td>-32,768 to 32,767</td><td><code>uint16_t</code> (0 to 65,535)</td></tr>
    <tr><td><code>int32_t</code> (<code>int</code>)</td><td>32 bits</td><td>4 bytes</td><td>~-2.14 billion to ~2.14 billion</td><td><code>uint32_t</code> (0 to ~4.29 billion)</td></tr>
    <tr><td><code>int64_t</code></td><td>64 bits</td><td>8 bytes</td><td>-9.22 &times; 10<sup>18</sup> to 9.22 &times; 10<sup>18</sup></td><td><code>uint64_t</code> (0 to 1.84 &times; 10<sup>19</sup>)</td></tr>
  </tbody>
</table>
</div>

<h3>3.2 Two's Complement and Integer Overflow</h3>
<p>Signed integers are stored in <strong>Two's Complement</strong> representation. The highest bit represents the sign bit. If an 8-bit signed integer holding <code>127</code> (binary <code>01111111</code>) is incremented by 1, it overflows to <code>-128</code> (binary <code>10000000</code>). Understanding bit representations prevents integer wraparound vulnerabilities.</p>
"""
    add_mod("b3-bits-bytes", "Module B3: Bits, Bytes, and Integer Widths", m3)

    # ============================================================
    # Module B4: Memory Addresses & The Pointer Mental Model
    # ============================================================
    m4 = f"""
<p>A pointer is not an abstract reference; a pointer is simply an integer whose value is a physical address in memory.</p>

<h3>4.1 Address-Of (<code>&amp;</code>) and Dereferencing (<code>*</code>)</h3>
{make_code_box("rook", """
int target = 42;
int* ptr = &target; // ptr stores the numerical memory address of 'target'

printf("Address of target: %p\\n", (void*)ptr);
printf("Value via pointer: %d\\n", *ptr); // Reads the 4 bytes stored at that address

*ptr = 99; // Writes 99 to the memory address stored in 'ptr'
printf("New target value:  %d\\n", target); // Prints 99
""", "Pointer Operations")}

<h3>4.2 Pointer Arithmetic</h3>
<p>When you add an integer <code>n</code> to a typed pointer <code>T*</code>, the hardware memory address increases by <code>n * sizeof(T)</code> bytes:</p>
{make_code_box("rook", """
int numbers[3] = { 100, 200, 300 };
int* p = &numbers[0]; // Address: 0x1000

p = p + 1; // Increases by 1 * sizeof(int) (4 bytes) -> Address: 0x1004
printf("%d\\n", *p); // Prints 200
""", "Pointer Arithmetic Mechanics")}

{make_callout("warn", "Null Pointers", """
A pointer containing address <code>0</code> (<code>NULL</code>) points to an unmapped virtual memory address. Attempting to dereference a null pointer (<code>*ptr</code> when <code>ptr == NULL</code>) causes an immediate Segmentation Fault (SIGSEGV) triggered by the CPU Memory Management Unit (MMU).
""")}
"""
    add_mod("b4-pointers", "Module B4: Memory Addresses & The Pointer Mental Model", m4)

    # ============================================================
    # Module B5: Structs & Memory Layout in Practice
    # ============================================================
    m5 = f"""
<p>Structures allow grouping heterogeneous data fields into contiguous memory blocks.</p>

<h3>5.1 Defining and Initializing Structs</h3>
{make_code_box("rook", """
struct Vector3 {
    x: float
    y: float
    z: float
}

int main() {
    Vector3 v = { 1.0, 2.0, 3.0 };
    printf("Vector: (%.1f, %.1f, %.1f)\\n", v.x, v.y, v.z);
    return 0;
}
""", "Struct Declaration")}

<h3>5.2 Passing by Value vs. Passing by Pointer</h3>
<div class="table-container">
<table>
  <thead>
    <tr><th>Strategy</th><th>Syntax</th><th>Hardware Cost</th><th>Mutation Allowed?</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>Pass by Value</strong></td>
      <td><code>void process(Vector3 v)</code></td>
      <td>Copies all 12 bytes of the struct onto the new stack frame.</td>
      <td>No: modifications affect only the local copy.</td>
    </tr>
    <tr>
      <td><strong>Pass by Pointer</strong></td>
      <td><code>void process(Vector3* v)</code></td>
      <td>Passes a single 8-byte memory address in a CPU register.</td>
      <td>Yes: modifications directly mutate the original caller memory.</td>
    </tr>
  </tbody>
</table>
</div>
"""
    add_mod("b5-structs", "Module B5: Structs & Memory Layout in Practice", m5)

    # ============================================================
    # Module B6: Arrays, Buffers, and Bounds Safety
    # ============================================================
    m6 = f"""
<p>An array is a fixed-size sequence of elements stored contiguously in memory with zero padding between items.</p>

<h3>6.1 Stack-Allocated Arrays</h3>
{make_code_box("rook", """
int buffer[5]; // 5 integers * 4 bytes = 20 contiguous bytes on the stack
buffer[0] = 10;
buffer[1] = 20;

// Iterating over items
for item in buffer {
    printf("%d\\n", item);
}
""", "Array Declaration")}

<h3>6.2 Runtime Bounds Checking (<code>-b</code> flag)</h3>
<p>In standard C, indexing out of bounds (such as <code>buffer[10]</code> on a 5-element array) silently reads or corrupts adjacent stack memory. In Rook, compiling with the bounds-checking flag (<code>rokade build -b</code>) emits runtime checks that safely abort execution if an out-of-bounds index is accessed.</p>
"""
    add_mod("b6-arrays", "Module B6: Arrays, Buffers, and Bounds Safety", m6)

    # ============================================================
    # Module B7: Resource Management & Avoiding Leaks
    # ============================================================
    m7 = f"""
<p>Systems software must manage system resources (file handles, network sockets, heap memory) with deterministic lifetimes.</p>

<h3>7.1 The Resource Leak Problem</h3>
<p>When resources are acquired, every possible exit path (early returns, error branches) must release that resource. In traditional C, missing a <code>free</code> or <code>fclose</code> creates a resource leak.</p>

<h3>7.2 Scope-Bound Cleanup with <code>defer</code></h3>
<p>Rook's <code>defer</code> statement registers cleanup actions that execute automatically whenever the current block exits:</p>
{make_code_box("rook", """
#include <stdio.h>
#include <stdlib.h>

int process_data(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) return -1;
    defer fclose(f); // Guaranteed to execute when function returns

    char* buf = (char*)malloc(1024);
    if (!buf) return -2;
    defer free(buf); // Guaranteed to execute before fclose(f)

    if (fread(buf, 1, 1024, f) <= 0) {
        return -3; // Both 'buf' is freed and 'f' is closed
    }

    return 0;     // Both 'buf' is freed and 'f' is closed
}
""", "Guaranteed Resource Cleanup")}
"""
    add_mod("b7-defer", "Module B7: Resource Management & Avoiding Leaks", m7)

    # ============================================================
    # Module B8: Building Real Projects & C Library Integration
    # ============================================================
    m8 = f"""
<p>Rook integrates into multi-file projects and uses external C libraries directly through <code>rokade.toml</code>.</p>

<h3>8.1 Project Configuration (<code>rokade.toml</code>)</h3>
{make_code_box("toml", """
[project]
name = "graphics_demo"
version = "0.1.0"
pkg-config = ["raylib"]
""", "rokade.toml")}

<h3>8.2 Integrating Foreign C Libraries Directly</h3>
<p>Because Rook compiles with full C ABI compatibility, C functions can be called directly without bindings or glue code:</p>
{make_code_box("rook", """
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
""", "Raylib Integration Example")}
"""
    add_mod("b8-real-projects", "Module B8: Building Real Projects & C Library Integration", m8)

    return modules
