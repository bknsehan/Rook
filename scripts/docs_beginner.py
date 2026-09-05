#!/usr/bin/env python3
"""
docs_beginner.py
Programming Foundations course for learners studying computer programming fundamentals.
"""

def get_beginner_modules(make_code_box, make_callout):
    modules = []

    def add_mod(mid, title, content):
        modules.append((mid, title, content))

    # ============================================================
    # Module B1: How Computers Work
    # ============================================================
    m1 = """
<p>Before writing code, it helps to understand how computers run programs and manage data. A computer relies on three main physical components: storage, memory, and the processor.</p>

<h3>1.1 Hardware Basics: Storage, RAM, and CPU</h3>
<p>Every computer relies on three main components to run software:</p>

<div class="arch-diagram">
  <div class="arch-box"><strong>Storage (SSD / Hard Drive)</strong><br>Permanent storage<br>Holds files when powered off</div>
  <div class="arch-arrow">➔</div>
  <div class="arch-box"><strong>RAM (Working Memory)</strong><br>Temporary workspace<br>Fast access, cleared on shutdown</div>
  <div class="arch-arrow">➔</div>
  <div class="arch-box"><strong>CPU (Processor)</strong><br>Calculation engine<br>Executes instructions sequentially</div>
</div>

<ul>
  <li><strong>Storage (SSD or Hard Drive):</strong> Holds data permanently. When you shut down your computer, your files, operating system, and programs remain intact on disk. Storage has high capacity, but the CPU cannot access it directly at memory speeds.</li>
  <li><strong>RAM (Random Access Memory):</strong> The working memory where active programs run. When you launch a program, the operating system copies it from storage into RAM. The CPU reads and writes RAM in nanoseconds. RAM is volatile: when the computer turns off, all data in RAM is cleared.</li>
  <li><strong>CPU (Central Processing Unit):</strong> The processor that executes program instructions. The CPU fetches an instruction from RAM, executes it (such as adding two numbers or checking a condition), and moves to the next instruction.</li>
</ul>

<h3>1.2 Bits and Bytes: How Hardware Represents Data</h3>
<p>At the hardware level, digital circuits store and transmit information using voltages that represent two states: 0 and 1.</p>
<ul>
  <li>A single 0 or 1 is called a <strong>bit</strong> (binary digit).</li>
  <li>Computers group bits into sets of eight. A group of 8 bits is called a <strong>byte</strong>.</li>
</ul>

""" + make_callout("note", "Why 8 Bits Make a Byte", """
An 8-bit byte can represent 256 distinct patterns (from <code>00000000</code> to <code>11111111</code>, or 0 through 255 in decimal). Historically, 256 values were sufficient to map every English letter, digit, and punctuation mark in the standard ASCII character set.
""") + """

<h3>1.3 Source Code, Machine Code, and Compilers</h3>
<p>Processors only execute machine code: numeric binary instructions tailored to the processor architecture. Because writing machine code directly is impractical, programmers write source code in human-readable text files using languages like Rook or C.</p>

<p>A <strong>compiler</strong> translates source code into machine code:</p>
<ol>
  <li><strong>Write source code:</strong> Instructions saved in a text file (such as <code>main.rook</code>).</li>
  <li><strong>Compile the code:</strong> The compiler checks syntax, validates types, enforces safety rules, and generates native machine code.</li>
  <li><strong>Run the executable:</strong> The resulting binary runs directly on the CPU.</li>
</ol>

<h3>1.4 Low-Level and High-Level Languages</h3>
<p>Programming languages differ in how closely they expose hardware details:</p>
<ul>
  <li><strong>Low-level languages</strong> (such as Assembly and C) provide direct control over memory addresses, data layout, and hardware registers. This allows fine-tuned performance, but requires developers to handle memory allocation and types manually.</li>
  <li><strong>High-level languages</strong> (such as Python, JavaScript, and Ruby) abstract away memory management and hardware specifics. They emphasize rapid development, but introduce runtime interpreters or garbage collectors.</li>
</ul>
<p>Rook bridges these two styles: it retains C-level control and memory layout predictability, while adding compile-time checks that prevent common memory and syntax mistakes.</p>

<h3>1.5 Compiled, Interpreted, and Transpiled Languages</h3>
<p>Languages take different paths to turn source text into execution:</p>

<div class="table-container">
<table>
  <thead>
    <tr><th>Approach</th><th>How it works</th><th>When translation happens</th><th>Examples</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>Compiled</strong></td>
      <td>A compiler translates the entire source program into machine code ahead of time, creating a native binary.</td>
      <td>Once, before execution</td>
      <td>C, C++, Rook, Rust, Go</td>
    </tr>
    <tr>
      <td><strong>Interpreted</strong></td>
      <td>An interpreter program reads and executes source instructions line by line at runtime.</td>
      <td>During program execution</td>
      <td>Python, Ruby, Bash</td>
    </tr>
    <tr>
      <td><strong>Transpiled</strong></td>
      <td>A source-to-source translator converts code written in one language into another high-level language, which is then compiled or interpreted.</td>
      <td>Before compilation or execution</td>
      <td>TypeScript (to JS), Rook C-backend (to C)</td>
    </tr>
  </tbody>
</table>
</div>
""" + make_callout("note", "Where Rook Fits", """
Rook is a compiled systems language. The <code>rokade</code> compiler translates Rook source code into clean C code (default backend) or LLVM IR (LLVM backend), which is then compiled into a standalone native executable. It requires no virtual machine or runtime interpreter.
""") + make_callout("tip", "Summary of Module 1", """
<ul>
  <li>Storage holds files permanently, RAM provides fast working memory, and the CPU executes instructions.</li>
  <li>Digital data is stored as bits (0 and 1) grouped into 8-bit bytes.</li>
  <li>Source code is human-readable text; compilers convert it into machine code for the CPU.</li>
  <li>Compiled programs run as native binaries without requiring a runtime interpreter.</li>
</ul>
""")
    add_mod("b1-how-computers-work", "Module 1: How Computers Work", m1)


    # ============================================================
    # Module B2: Your First Program
    # ============================================================
    m2 = """
<p>A standard introduction to any language is printing text to the terminal. Here is a minimal program in Rook:</p>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    printf("Hello, World!\n");
    return 0;
}
""", "Your First Program (src/main.rook)") + """
<h3>2.1 Code Structure</h3>
<ol>
  <li><strong><code>#include &lt;stdio.h&gt;</code></strong><br>
  Tells the compiler to include declarations from the C standard input/output library, which defines the <code>printf</code> function.</li>
  
  <li><strong><code>int main()</code></strong><br>
  The program entry point. When the operating system runs an application, execution begins at <code>main</code>. The <code>int</code> return type means the function returns an integer exit code to the operating system.</li>

  <li><strong>Braces <code>{ ... }</code></strong><br>
  Curly braces delimit a block of code. Statements inside these braces belong to the <code>main</code> function.</li>

  <li><strong><code>printf("Hello, World!\n");</code></strong><br>
  Calls <code>printf</code> to print text to standard output. The escape sequence <code>\n</code> outputs a newline character.</li>

  <li><strong>Semicolon <code>;</code></strong><br>
  Each statement in Rook ends with a semicolon.</li>

  <li><strong><code>return 0;</code></strong><br>
  Exits the program and returns status code <code>0</code> to the operating system, indicating that the program ran without errors.</li>
</ol>

<h3>2.2 Compiling and Running</h3>
<div class="arch-diagram">
  <div class="arch-box">1. Source File<br><code>src/main.rook</code></div>
  <div class="arch-arrow">➔</div>
  <div class="arch-box">2. Compiler<br><code>rokade build</code></div>
  <div class="arch-arrow">➔</div>
  <div class="arch-box">3. Executable<br><code>./build/linux/my_project</code></div>
</div>

<p>Running <code>rokade run</code> compiles the project and executes the resulting binary in one step.</p>
""" + make_callout("tip", "Summary of Module 2", """
<ul>
  <li>Execution starts in the <code>main()</code> function.</li>
  <li>Statements end with semicolons (<code>;</code>), and blocks use curly braces (<code>{ ... }</code>).</li>
  <li><code>printf()</code> writes text to the terminal, and <code>\n</code> starts a new line.</li>
  <li>Returning <code>0</code> indicates successful execution.</li>
</ul>
""")
    add_mod("b2-first-program", "Module 2: Your First Program", m2)


    # ============================================================
    # Module B3: Storing Information: Variables and Types
    # ============================================================
    m3 = """
<p>Programs store data in variables. A variable is a named location in memory that holds a value of a specific data type.</p>

<h3>3.1 Understanding Variables</h3>
<p>A variable has three key properties:</p>
<ul>
  <li><strong>Name (Identifier):</strong> How you reference the variable in code (such as <code>health</code> or <code>count</code>).</li>
  <li><strong>Type:</strong> The kind of data it stores, determining how much memory it uses and which operations are valid.</li>
  <li><strong>Value:</strong> The current data stored in that memory location.</li>
</ul>

<h3>3.2 Common Primitive Types</h3>
<div class="table-container">
<table>
  <thead>
    <tr><th>Type</th><th>Purpose</th><th>Size (x86_64)</th><th>Example</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><code>int</code></td>
      <td>Signed whole numbers</td>
      <td>4 bytes</td>
      <td><code>0</code>, <code>42</code>, <code>-15</code></td>
    </tr>
    <tr>
      <td><code>float</code></td>
      <td>Single-precision decimal numbers</td>
      <td>4 bytes</td>
      <td><code>3.14f</code>, <code>-0.5f</code></td>
    </tr>
    <tr>
      <td><code>double</code></td>
      <td>Double-precision decimal numbers</td>
      <td>8 bytes</td>
      <td><code>2.718281828459</code></td>
    </tr>
    <tr>
      <td><code>char</code></td>
      <td>Single character or byte</td>
      <td>1 byte</td>
      <td><code>'A'</code>, <code>'z'</code>, <code>'!'</code></td>
    </tr>
    <tr>
      <td><code>bool</code></td>
      <td>Boolean truth value</td>
      <td>1 byte</td>
      <td><code>true</code>, <code>false</code></td>
    </tr>
    <tr>
      <td><code>const char*</code></td>
      <td>Pointer to a null-terminated string</td>
      <td>8 bytes</td>
      <td><code>"Hello, World!"</code></td>
    </tr>
  </tbody>
</table>
</div>

<h3>3.3 Declaring and Modifying Variables</h3>
<p>Rook supports both C-style declarations and <code>let</code> declarations:</p>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    // C-style declarations
    int player_health = 100;
    float walking_speed = 4.5f;
    char rank_letter = 'A';
    bool is_alive = true;

    // let-style declarations
    let score = 50;              // Type inferred as int
    let player_name: const char* = "Arthur";

    printf("Player: %s\n", player_name);
    printf("Health: %d, Score: %d\n", player_health, score);

    // Reassignment
    player_health = 85;
    printf("Updated Health: %d\n", player_health);

    return 0;
}
""", "Declaring and Using Variables") + """
""" + make_callout("note", "Automatic Zero-Initialization", """
In standard C, declaring an uninitialized local variable (such as <code>int x;</code>) leaves it containing whatever random data was in that stack location. Rook automatically initializes uninitialized variables to zero (<code>= {0}</code>), preventing undefined reads.
""") + make_callout("tip", "Summary of Module 3", """
<ul>
  <li>Variables are named memory locations holding typed values.</li>
  <li>Primitive types include <code>int</code> for integers, <code>float</code> for decimals, <code>char</code> for characters, and <code>bool</code> for booleans.</li>
  <li>Variables can be declared using C style (<code>int x = 1;</code>) or let style (<code>let x = 1;</code>).</li>
  <li>Uninitialized local variables in Rook default safely to zero.</li>
</ul>
""")
    add_mod("b3-variables-types", "Module 3: Storing Information: Variables and Types", m3)


    # ============================================================
    # Module B4: Arithmetic and Comparisons
    # ============================================================
    m4 = """
<p>Programs compute results and evaluate conditions using operators.</p>

<h3>4.1 Arithmetic Operators</h3>
<ul>
  <li><code>+</code> (Addition): <code>10 + 5</code> evaluates to <code>15</code></li>
  <li><code>-</code> (Subtraction): <code>10 - 5</code> evaluates to <code>5</code></li>
  <li><code>*</code> (Multiplication): <code>10 * 5</code> evaluates to <code>50</code></li>
  <li><code>/</code> (Division): <code>10 / 5</code> evaluates to <code>2</code></li>
  <li><code>%</code> (Modulo / Remainder): <code>10 % 3</code> evaluates to <code>1</code></li>
</ul>

""" + make_callout("warn", "Integer Division", """
Dividing two integers performs integer division, which truncates the fractional portion. For example, <code>7 / 2</code> evaluates to <code>3</code>. To obtain a decimal result, use floating-point numbers: <code>7.0f / 2.0f</code> evaluates to <code>3.5f</code>.
""") + """

<h3>4.2 Comparison Operators</h3>
<p>Comparison operators compare two values and produce a boolean result (<code>true</code> or <code>false</code>):</p>
<ul>
  <li><code>==</code> (Equal to): checks if two values are equal. Note that <code>==</code> compares values, while <code>=</code> assigns them.</li>
  <li><code>!=</code> (Not equal to): checks if two values differ.</li>
  <li><code>&gt;</code> and <code>&lt;</code>: greater than, less than.</li>
  <li><code>&gt;=</code> and <code>&lt;=</code>: greater than or equal, less than or equal.</li>
</ul>

<h3>4.3 Logical Operators</h3>
<ul>
  <li><code>&amp;&amp;</code> (Logical AND): true only if both operands are true.</li>
  <li><code>||</code> (Logical OR): true if at least one operand is true.</li>
  <li><code>!</code> (Logical NOT): inverts a boolean value (e.g. <code>!true</code> is <code>false</code>).</li>
</ul>

""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    int age = 20;
    bool has_id = true;

    if (age >= 18 && has_id) {
        printf("Admission granted.\n");
    }

    int items = 14;
    int pack_size = 4;
    int leftover = items % pack_size;
    printf("Remaining items: %d\n", leftover); // Prints 2

    return 0;
}
""", "Operators in Action") + make_callout("tip", "Summary of Module 4", """
<ul>
  <li>Use <code>==</code> to compare values, and <code>=</code> to assign values.</li>
  <li>Integer division discards remainders; use floating-point numbers when decimal precision is needed.</li>
  <li>Combine conditions using <code>&&</code> (AND), <code>||</code> (OR), and <code>!</code> (NOT).</li>
</ul>
""")
    add_mod("b4-math-operators", "Module 4: Doing Math & Making Comparisons", m4)


    # ============================================================
    # Module B5: Making Choices: Control Flow
    # ============================================================
    m5 = """
<p>Control flow structures allow programs to execute different code blocks based on conditions.</p>

<h3>5.1 The if, else if, and else Construct</h3>
<p>An <code>if</code> statement evaluates a condition. If the condition is <code>true</code>, the corresponding block executes; otherwise, execution continues to the next branch.</p>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    int score = 85;

    if (score >= 90) {
        printf("Grade: A\n");
    } else if (score >= 80) {
        printf("Grade: B\n");
    } else if (score >= 70) {
        printf("Grade: C\n");
    } else {
        printf("Grade: F\n");
    }

    return 0;
}
""", "Branching Evaluation") + """
<p>Evaluation steps:</p>
<ol>
  <li>The program checks <code>score &gt;= 90</code>. Because 85 is less than 90, it proceeds to the next check.</li>
  <li>It checks <code>score &gt;= 80</code>. This is <code>true</code>.</li>
  <li>The program prints <code>Grade: B</code> and skips the remaining branches.</li>
</ol>

<h3>5.2 Compile-Time Enforcement: Assignments in Conditions</h3>
<p>In standard C, accidentally writing <code>if (score = 100)</code> instead of <code>if (score == 100)</code> assigns 100 to <code>score</code> and treats the result as true. Rook checks condition expressions at compile time and rejects assignments inside conditions:</p>
""" + make_code_box("rook", """
// Compile error in Rook:
// error: assignment used as condition; did you mean '=='?
if (score = 100) {
    printf("Perfect\n");
}
""", "Compile Error on Assignment in Condition") + """
""" + make_callout("tip", "Summary of Module 5", """
<ul>
  <li>Use <code>if</code> and <code>else if</code> to branch based on conditions.</li>
  <li>Only the first matching branch in an <code>if/else if</code> chain is executed.</li>
  <li>Assignments inside conditional expressions are compile-time errors in Rook.</li>
</ul>
""")
    add_mod("b5-control-flow", "Module 5: Making Choices: Control Flow", m5)


    # ============================================================
    # Module B6: Repetition: Automating with Loops
    # ============================================================
    m6 = """
<p>Loops repeat a block of code as long as a condition is met, or across a specific count.</p>

<h3>6.1 The while Loop</h3>
<p>A <code>while</code> loop continues executing its body as long as the condition remains true:</p>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    int countdown = 5;

    while (countdown > 0) {
        printf("%d...\n", countdown);
        countdown = countdown - 1;
    }

    printf("Done.\n");
    return 0;
}
""", "Countdown with while") + """
""" + make_callout("warn", "Infinite Loops", """
If the loop condition never becomes false (for example, if <code>countdown</code> is not decremented), the loop will run indefinitely. Ensure that the loop body makes progress toward terminating the condition.
""") + """

<h3>6.2 The for Loop</h3>
<p>When iterating a specific number of times, a <code>for</code> loop groups the initialization, condition, and step in one place:</p>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    for (int i = 1; i <= 5; i++) {
        printf("Iteration: %d\n", i);
    }
    return 0;
}
""", "Counting with for") + """
<ul>
  <li><code>int i = 1;</code>: initializes the loop counter once at the start.</li>
  <li><code>i &lt;= 5;</code>: checked before each iteration. When false, the loop ends.</li>
  <li><code>i++</code>: increments the counter after each iteration.</li>
</ul>

<h3>6.3 Loop Control: break and continue</h3>
<ul>
  <li><code>break</code> exits the enclosing loop immediately.</li>
  <li><code>continue</code> skips the rest of the current iteration and begins the next cycle.</li>
</ul>
""" + make_callout("tip", "Summary of Module 6", """
<ul>
  <li>Use <code>while</code> when the number of iterations depends on runtime state.</li>
  <li>Use <code>for</code> when counting or iterating a known number of steps.</li>
  <li>Use <code>break</code> to exit early and <code>continue</code> to skip to the next iteration.</li>
</ul>
""")
    add_mod("b6-loops-repetition", "Module 6: Repetition: Automating with Loops", m6)


    # ============================================================
    # Module B7: Functions: Reusable Building Blocks
    # ============================================================
    m7 = """
<p>Functions organize code into named, modular units that take inputs, perform computations, and return results.</p>

<h3>7.1 Function Structure</h3>
<p>A function definition specifies its return type, name, parameters, and body:</p>
""" + make_code_box("rook", """
#include <stdio.h>

// Takes two integers, returns their sum
int add(int a, int b) {
    return a + b;
}

// Takes a string, returns nothing (void)
void greet(const char* name) {
    printf("Hello, %s!\n", name);
}

int main() {
    greet("Alice");
    greet("Bob");

    int sum = add(15, 25);
    printf("15 + 25 = %d\n", sum);

    return 0;
}
""", "Defining and Calling Functions") + """
<h3>7.2 Variable Scope</h3>
<p>Variables declared inside a function are local to that function. They are created when the function executes and discarded when it returns. Functions cannot directly read or modify local variables defined in other functions.</p>
""" + make_callout("tip", "Summary of Module 7", """
<ul>
  <li>Functions divide logic into named, reusable components.</li>
  <li>Parameters receive input arguments; the <code>return</code> statement sends back a result.</li>
  <li>Functions that return no value use the <code>void</code> return type.</li>
  <li>Local variables are scoped to the function in which they are declared.</li>
</ul>
""")
    add_mod("b7-functions", "Module 7: Functions: Reusable Building Blocks", m7)


    # ============================================================
    # Module B8: Grouping Data: Structures
    # ============================================================
    m8 = """
<p>A <code>struct</code> (structure) groups related variables under a single custom data type.</p>

<h3>8.1 Defining and Instantiating a Struct</h3>
<p>In Rook, fields can be declared using either C style (<code>Type name;</code>) or let style (<code>let name: Type;</code>):</p>
""" + make_code_box("rook", """
#include <stdio.h>

// C-style field definitions
struct Player {
    const char* name;
    int health;
    int score;
};

// Equivalent let-style field definitions
struct PlayerLet {
    let name: const char*;
    let health: int;
    let score: int;
};

int main() {
    // Initializing with named fields
    Player hero = Player{
        name: "Arthur",
        health: 100,
        score: 0
    };

    // Accessing fields with dot notation
    printf("Player: %s\n", hero.name);
    printf("Health: %d HP\n", hero.health);

    // Updating a field
    hero.score += 500;
    printf("Updated Score: %d\n", hero.score);

    return 0;
}
""", "Struct Definition and Field Access") + """
""" + make_callout("tip", "Summary of Module 8", """
<ul>
  <li>Structs bundle multiple related variables into a single record.</li>
  <li>Field declarations support both C-style (<code>Type name;</code>) and let-style (<code>let name: Type;</code>).</li>
  <li>Fields are accessed and modified using the dot operator (<code>hero.health</code>).</li>
</ul>
""")
    add_mod("b8-structs", "Module 8: Grouping Data: Structures", m8)


    # ============================================================
    # Module B9: Demystifying Memory: Pointers
    # ============================================================
    m9 = """
<p>In systems programming, programs interact directly with computer memory. A <strong>pointer</strong> is a variable that stores the memory address of another value.</p>

<h3>9.1 Addresses and Pointers</h3>
<p>Every variable resides at a distinct location in RAM, identified by its memory address:</p>
<ul>
  <li>The <strong>variable</strong> holds the actual data value.</li>
  <li>The <strong>pointer</strong> holds the numerical memory address where that variable is located.</li>
</ul>

<h3>9.2 The Address-Of (&amp;) and Dereference (*) Operators</h3>
<ul>
  <li><code>&amp;</code> (Address-of): returns the memory address of a variable (e.g. <code>&amp;x</code>).</li>
  <li><code>*</code> (Dereference): accesses the value stored at the address a pointer holds (e.g. <code>*ptr</code>).</li>
</ul>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    int value = 42;
    int* ptr = &value; // ptr stores the address of value

    printf("Value: %d\n", value);
    printf("Address: %p\n", (void*)ptr);

    // Modify value through the pointer
    *ptr = 100;
    printf("New Value: %d\n", value); // Prints 100

    return 0;
}
""", "Pointers and Dereferencing") + """
<h3>9.3 Common Uses for Pointers</h3>
<ul>
  <li><strong>Modifying values across function calls:</strong> When arguments are passed by value, the function receives a copy. Passing a pointer allows a function to update the caller's original data.</li>
  <li><strong>Passing large structures efficiently:</strong> Passing a pointer copies an 8-byte memory address rather than duplicating entire structs in memory.</li>
</ul>

<h3>9.4 Null Pointers</h3>
<p>A pointer with an address of <code>0</code> is called a null pointer. It indicates that the pointer does not reference valid memory. Attempting to dereference a null pointer causes an immediate operating system crash (segmentation fault). Check pointers before dereferencing when an address may be absent.</p>
""" + make_callout("tip", "Summary of Module 9", """
<ul>
  <li>A pointer stores the memory address of another variable.</li>
  <li>Use <code>&</code> to get a variable's address, and <code>*</code> to access the value at that address.</li>
  <li>Pointers allow functions to mutate caller values and avoid copying large data structures.</li>
  <li>Dereferencing a null pointer causes a segmentation fault.</li>
</ul>
""")
    add_mod("b9-memory-pointers", "Module 9: Demystifying Memory: Pointers", m9)


    # ============================================================
    # Module B10: Working with Lists: Arrays
    # ============================================================
    m10 = """
<p>An array stores a fixed-size sequence of elements of the same data type in contiguous memory.</p>

<h3>10.1 Declaring and Indexing Arrays</h3>
<p>Array indices in Rook and C are zero-based, representing an offset from the start of the array:</p>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    int scores[5] = {95, 88, 72, 99, 84};

    // Reading elements by index
    printf("First: %d\n", scores[0]); // Index 0
    printf("Third: %d\n", scores[2]); // Index 2

    // Modifying an element
    scores[2] = 78;

    // Summing elements with a loop
    int total = 0;
    for (int i = 0; i < 5; i++) {
        total += scores[i];
    }

    printf("Total: %d\n", total);
    printf("Average: %.1f\n", (float)total / 5.0f);

    return 0;
}
""", "Array Declaration and Indexing") + """
""" + make_callout("warn", "Array Bounds", """
An array with 5 elements has valid indices from 0 to 4. Accessing <code>scores[5]</code> or higher is out of bounds. In standard C, out-of-bounds access reads or overwrites adjacent memory. In Rokade, compiling with the <code>-b</code> flag enables runtime bounds checking that stops execution if an index is out of range.
""") + make_callout("tip", "Summary of Module 10", """
<ul>
  <li>Arrays hold a fixed number of elements of the same type stored contiguously.</li>
  <li>Indices start at <code>0</code> and end at <code>length - 1</code>.</li>
  <li>Use the <code>-b</code> compiler flag to detect out-of-bounds errors at runtime.</li>
</ul>
""")
    add_mod("b10-arrays", "Module 10: Working with Lists: Arrays", m10)


    # ============================================================
    # Module B11: Building Real Projects
    # ============================================================
    m11 = """
<p>As applications grow, organizing code across multiple files becomes essential for maintainability.</p>

<h3>11.1 Project Structure</h3>
<p>Running <code>rokade new my_project</code> creates a standardized project directory:</p>
""" + make_code_box("text", """
my_project/
├── rokade.toml          # Project configuration, name, version, and dependencies
├── src/
│   ├── main.rook        # Application entry point containing main()
│   └── math_utils.rook  # Utility functions module
└── build/               # Generated binaries and intermediate files
""", "Project Directory Layout") + """
<h3>11.2 Importing Modules with #comprise</h3>
<p>In standard C, splitting code across files requires creating <code>.h</code> header files with include guards. Rook replaces this with the <code>#comprise</code> directive:</p>
""" + make_code_box("rook", """
// src/math_utils.rook
int square(int x) {
    return x * x;
}

// src/main.rook
#comprise math_utils
#include <stdio.h>

int main() {
    int val = 8;
    printf("%d squared is %d\n", val, square(val));
    return 0;
}
""", "Modular Multi-File Code") + """
<p>When Rokade builds the project, it parses <code>math_utils.rook</code>, verifies types, and compiles both modules into a single binary without duplicate symbol errors.</p>
""" + make_callout("tip", "Summary of Module 11", """
<ul>
  <li>Organize code into separate files by function or module.</li>
  <li><code>rokade.toml</code> configures project settings and dependencies.</li>
  <li>Use <code>#comprise filename</code> to import local Rook source files directly without header files.</li>
</ul>
""")
    add_mod("b11-multi-file", "Module 11: Building Real Projects", m11)


    # ============================================================
    # Module B12: How to Think Like a Programmer
    # ============================================================
    m12 = """
<p>Programming centers on problem solving: decomposing tasks into small, verifiable steps and locating errors methodically.</p>

<h3>12.1 Problem Decomposition</h3>
<p>When solving an unfamiliar problem, avoid writing everything at once:</p>
<ol>
  <li>Break the task into smaller subtasks until each subtask has a straightforward solution.</li>
  <li>Write and verify each component independently.</li>
  <li>Compose the tested components into the complete solution.</li>
</ol>

<h3>12.2 Reading Compiler Errors</h3>
<p>Compiler errors provide actionable diagnostics when code violates syntax or type rules:</p>
<ol>
  <li><strong>Check the file and line number:</strong> An error like <code>src/main.rook:14:5: error</code> points to line 14, column 5.</li>
  <li><strong>Read the error description:</strong> Common errors include missing semicolons, unrecognized identifiers, or type mismatches.</li>
  <li><strong>Fix the top error first:</strong> A single error near the top of a file can trigger several downstream warnings. Address the first error and recompile.</li>
</ol>

<h3>12.3 Print Debugging</h3>
<p>When a program compiles but produces unexpected output, adding <code>printf</code> statements lets you inspect values as execution proceeds:</p>
""" + make_code_box("rook", """
int calculate_discount(int price, int customer_years) {
    printf("[DEBUG] price=%d, years=%d\n", price, customer_years);
    
    int discount = 0;
    if (customer_years > 5) {
        discount = 20;
    }
    printf("[DEBUG] calculated discount=%d\n", discount);
    
    return price - discount;
}
""", "Tracing Values with printf") + """
<p>Examining intermediate outputs clarifies where execution diverges from expectations.</p>
""" + make_callout("tip", "Summary of Module 12", """
<ul>
  <li>Decompose complex requirements into small, testable steps.</li>
  <li>Read compiler messages carefully, starting with the first reported error.</li>
  <li>Trace program state with print statements to diagnose logic errors.</li>
</ul>
""")
    add_mod("b12-debugging-thinking", "Module 12: How to Think Like a Programmer", m12)


    # ============================================================
    # Module B13: Objects and Methods
    # ============================================================
    m13 = """
<p>In addition to plain structs, Rook provides <code>object</code> and <code>impl</code> for organizing state and associated methods together. This model supports single inheritance with static dispatch.</p>

<h3>13.1 Defining an Object</h3>
<p>An <code>object</code> defines data fields. Fields support both C-style (<code>Type name;</code>) and let-style (<code>let name: Type;</code>):</p>
""" + make_code_box("rook", """
#include <stdio.h>

object Animal {
    const char* name;
    int legs;
}
""", "Defining an Object") + """
<h3>13.2 Attaching Methods with impl</h3>
<p>Methods are defined in an <code>impl</code> block. Each method takes a pointer to the instance as its first argument, conventionally named <code>self</code>:</p>
""" + make_code_box("rook", """
#include <stdio.h>

object Animal {
    const char* name;
    int legs;
}

impl Animal {
    void speak(Animal* self) {
        printf("%s\n", self.name);
    }
    int legs_count(Animal* self) {
        return self.legs;
    }
}

int main() {
    Animal dog;
    dog.name = "Rex";
    dog.legs = 4;

    dog.speak();
    printf("Legs: %d\n", dog.legs_count());
    return 0;
}
""", "Attaching Methods to Objects") + """
<h3>13.3 Single Inheritance</h3>
<p>An object can extend a base object using a colon (<code>:</code>). The child object inherits the parent object's fields and methods:</p>
""" + make_code_box("rook", """
object Cat : Animal {
    int lives;
}

impl Cat {
    void meow(Cat* self) {
        self.speak(); // Calls inherited Animal.speak
    }
}

int main() {
    Cat c;
    c.name = "Tom";
    c.legs = 4;
    c.lives = 9;

    c.speak();
    c.meow();
    printf("Lives: %d\n", c.lives);
    return 0;
}
""", "Single Inheritance") + make_callout("note", "Language Comparison", """
Object-oriented concepts appear across many languages. C++, Java, and Python use classes with methods. Rust uses structs with <code>impl</code> blocks. Other languages, like C and Go, avoid class inheritance in favor of composition or interface tables. Rook provides single inheritance lowered statically to nested C structs with zero vtable overhead.
""") + make_callout("tip", "Summary of Module 13", """
<ul>
  <li><code>object</code> defines records with named fields in C-style or let-style.</li>
  <li><code>impl</code> binds methods to an object, taking an explicit <code>self</code> pointer.</li>
  <li><code>object Child : Parent</code> establishes single inheritance.</li>
  <li>Method dispatch is static and maps directly to C function calls without virtual tables.</li>
</ul>
""")
    add_mod("b13-objects-impl", "Module 13: Objects and Methods", m13)


    # ============================================================
    # Module B14: Pattern Matching
    # ============================================================
    m14 = """
<p>Rook provides a <code>match</code> construct that compares values against patterns and executes the matching arm. It supports scalar values, enums, and sum types (tagged unions).</p>

<h3>14.1 Value Matching</h3>
<p>The simplest form matches integers or enums against constant patterns:</p>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    int code = 2;

    match (code) {
        1 => printf("One\n"),
        2 => printf("Two\n"),
        3 => printf("Three\n"),
        _ => printf("Other\n"),
    }
    return 0;
}
""", "Basic Pattern Match") + """
<p>The underscore <code>_</code> acts as a wildcard pattern that matches any value not matched by prior arms.</p>

<h3>14.2 Pattern Matching on Sum Types</h3>
<p>A <code>sum</code> type represents a value that can hold one of several variant shapes. A <code>match</code> expression inspects the variant and destructures its payload fields:</p>
""" + make_code_box("rook", """
#include <stdio.h>

sum Shape {
    Circle { float r; };
    Rect   { float w; float h; };
    Point;
}

impl Shape {
    float area(Shape* self) {
        return match (*self) {
            Circle { r }    => 3.14159f * r * r,
            Rect   { w, h } => w * h,
            Point           => 0.0f,
            _               => 0.0f,
        };
    }
}

int main() {
    Shape a = Circle { r: 2.0f };
    Shape b = Rect { w: 3.0f, h: 4.0f };
    Shape c = Point;

    printf("Circle area: %f\n", a.area());
    printf("Rect area:   %f\n", b.area());
    printf("Point area:  %f\n", c.area());
    return 0;
}
""", "Matching on Sum Types") + make_callout("note", "Language Comparison", """
Pattern matching on tagged unions is common in languages like Rust, Swift, and Haskell. Traditional C offers <code>switch</code> on integers without payload destructuring, requiring manual union tagging. Rook provides tagged unions and pattern matching directly in the grammar.
""") + make_callout("tip", "Summary of Module 14", """
<ul>
  <li><code>match</code> compares values against patterns and executes the first matching arm.</li>
  <li>The <code>_</code> pattern serves as the fallback arm.</li>
  <li>Sum types represent values that can take one of multiple variant shapes.</li>
  <li>Pattern matching destructures sum type variant payloads in a single step.</li>
</ul>
""")
    add_mod("b14-match-patterns", "Module 14: Pattern Matching", m14)

    return modules


if __name__ == "__main__":
    def dummy_box(lang, code, title=""): return f"[BOX {lang} {title}]"
    def dummy_call(kind, title, body): return f"[CALL {kind} {title}]"
    mods = get_beginner_modules(dummy_box, dummy_call)
    print(f"Loaded {len(mods)} beginner modules successfully!")
