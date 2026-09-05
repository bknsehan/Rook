#!/usr/bin/env python3
"""
docs_beginner.py
Comprehensive "Programming Foundations" educational course for high-school graduates
learning computer programming from scratch.
"""

def get_beginner_modules(make_code_box, make_callout):
    modules = []

    def add_mod(mid, title, content):
        modules.append((mid, title, content))

    # ============================================================
    # Module B1: How Computers Actually Work (The Mental Model)
    # ============================================================
    m1 = """
<p>Welcome to programming! Before writing a single line of code, it helps immensely to have a clear picture of what a computer actually is and how it follows instructions. You don't need an engineering degree to understand this—just a few simple mental models.</p>

<h3>1.1 The Physical Triumvirate: CPU, RAM, and Storage</h3>
<p>Every computer—from the phone in your pocket to massive supercomputers in data centers—relies on three core physical components working in harmony:</p>

<div class="arch-diagram">
  <div class="arch-box"><strong>The Storage (SSD / Hard Drive)</strong><br>Permanent Library<br>Large, durable, but slow</div>
  <div class="arch-arrow">➔</div>
  <div class="arch-box"><strong>The RAM (Working Memory)</strong><br>Kitchen Countertop<br>Blazing fast, temporary scratchpad</div>
  <div class="arch-arrow">➔</div>
  <div class="arch-box"><strong>The CPU (The Processor)</strong><br>The Chef / Calculator<br>Executes billions of simple steps per second</div>
</div>

<ul>
  <li><strong>The Storage (SSD or Hard Drive):</strong> Think of this as a permanent bookshelf or filing cabinet. When you turn your computer off, everything stored here remains safe. Your operating system, games, and code files live here. However, storage is relatively slow for the CPU to interact with directly.</li>
  <li><strong>RAM (Random Access Memory):</strong> Think of this as your active kitchen countertop or desk scratchpad. When you launch an application or compile code, the program is copied from storage into RAM. The CPU can read and write data in RAM in nanoseconds. However, RAM is <em>volatile</em>: when you shut down the computer, everything on the countertop is wiped clean.</li>
  <li><strong>The CPU (Central Processing Unit):</strong> Think of this as a hyper-fast, literal-minded calculator or chef. The CPU does not "think" or understand human nuance. It simply fetches an instruction from RAM, executes it (like adding two numbers or checking if a number is zero), and moves to the next instruction—billions of times per second.</li>
</ul>

<h3>1.2 Bits and Bytes: The Alphabet of Hardware</h3>
<p>Computers do not speak English, Spanish, or Indonesian. At the microscopic hardware level, computer chips consist of billions of microscopic transistors that act like light switches:</p>
<ul>
  <li>A switch can only be in one of two states: <strong>OFF (0)</strong> or <strong>ON (1)</strong>.</li>
  <li>A single 0 or 1 is called a <strong>bit</strong> (short for <em>binary digit</em>).</li>
  <li>Because a single bit can only represent two states, computers group bits into sets of eight. A group of 8 bits is called a <strong>byte</strong>.</li>
</ul>

<div class="callout callout-note">
  <div class="callout-title">💡 Why 8 Bits = 1 Byte?</div>
  <div class="callout-body">
    With 8 bits, you can create 256 unique combinations of 0s and 1s (from <code>00000000</code> to <code>11111111</code>). In decimal numbers, that covers 0 through 255. This was historically enough to assign a unique number to every English letter (A-Z, a-z), digit (0-9), and punctuation symbol in the standard ASCII character table!
  </div>
</div>

<h3>1.3 Source Code vs. Machine Code: What is a Compiler?</h3>
<p>Humans cannot reasonably write billions of 1s and 0s by hand (called <em>machine code</em>). Instead, we write human-readable text called <strong>source code</strong> using a programming language like Rook or C.</p>

<p>Because the CPU cannot directly run human text, we use a special tool called a <strong>compiler</strong> (such as <code>rokade</code>):</p>
<ol>
  <li><strong>You write source code:</strong> Clear, human-readable instructions saved in a text file (e.g. <code>main.rook</code>).</li>
  <li><strong>The compiler inspects your code:</strong> It checks for grammar errors, verifies data types, and ensures safety rules are respected.</li>
  <li><strong>The compiler emits machine code:</strong> It translates your human instructions into raw binary instructions that your CPU can execute directly at maximum speed.</li>
</ol>
""" + make_callout("tip", "Key Takeaways of Module 1", """
<ul>
  <li>The CPU is the brain/calculator; RAM is the fast temporary workspace; Storage is the permanent library.</li>
  <li>All computer data ultimately consists of bits (0s and 1s) and bytes (groups of 8 bits).</li>
  <li>Source code is what humans write; machine code is what the CPU executes.</li>
  <li>A compiler is the bridge that translates your human logic into blazing-fast machine instructions.</li>
</ul>
""")
    add_mod("b1-how-computers-work", "Module 1: How Computers Actually Work", m1)

    # ============================================================
    # Module B2: Your Very First Program & The Journey of Code
    # ============================================================
    m2 = """
<p>Every programmer's journey begins with a sacred tradition: writing a program that prints the words <code>"Hello, World!"</code> to your screen. Let's look at the complete code, and then dissect every single character so you understand why it is there.</p>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    printf("Hello, World!\\n");
    return 0;
}
""", "Your First Program (src/main.rook)") + """
<h3>2.1 Line-by-Line Breakdown</h3>
<ol>
  <li><strong><code>#include &lt;stdio.h&gt;</code></strong><br>
  Programs rarely start from scratch. Standard libraries provide pre-built tools for common tasks. Here, <code>stdio.h</code> stands for <em>Standard Input/Output</em>. Including this tells the compiler: "Please let me borrow the standard tools for reading from the keyboard and writing text to the terminal."</li>
  
  <li><strong><code>int main()</code></strong><br>
  This is the official front door of your program. When your operating system (Linux, Windows, or macOS) starts your application, it specifically searches for a function named <code>main</code> to begin execution. The word <code>int</code> before <code>main</code> means that when the program finishes, it will hand back a whole integer number (an exit code) to the operating system.</li>

  <li><strong>The Curly Braces <code>{ ... }</code></strong><br>
  In Rook and C, curly braces define a <strong>block of code</strong>. Everything between the opening <code>{</code> and the closing <code>}</code> belongs to the <code>main</code> function. Think of it like the walls of a room where the work happens.</li>

  <li><strong><code>printf("Hello, World!\\n");</code></strong><br>
  <code>printf</code> stands for <em>Print Formatted</em>. It takes the text inside the quotation marks and displays it on your terminal screen. Notice the funny characters <code>\\n</code> at the end: this is an <em>escape sequence</em> that tells the computer to press the "Enter" key and move to a fresh new line.</li>

  <li><strong>The Semicolon <code>;</code></strong><br>
  In English, every complete sentence ends with a period. In programming, every complete instruction (statement) ends with a semicolon. Forgetting a semicolon is like writing a run-on sentence without punctuation; the compiler won't know where one thought ends and the next begins.</li>

  <li><strong><code>return 0;</code></strong><br>
  This is your program telling the operating system: "I have finished all my work, and everything went successfully without any errors!" In computing convention, a return code of <code>0</code> signifies success, while non-zero numbers (like 1, -1, or 404) signify various error codes.</li>
</ol>

<h3>2.2 The Lifecycle: From Text File to Running Application</h3>
<div class="arch-diagram">
  <div class="arch-box">1. Write Code<br><code>src/main.rook</code></div>
  <div class="arch-arrow">➔</div>
  <div class="arch-box">2. Compile &amp; Link<br><code>rokade build</code></div>
  <div class="arch-arrow">➔</div>
  <div class="arch-box">3. Run Executable<br><code>./build/my_app</code></div>
</div>

<p>When you run <code>rokade run</code> in your terminal, the compiler performs the compilation and linking automatically, producing a native binary file and immediately launching it so you see the result in milliseconds.</p>
""" + make_callout("tip", "Key Takeaways of Module 2", """
<ul>
  <li><code>main()</code> is the designated entry point where every program starts running.</li>
  <li>Statements must end with semicolons (<code>;</code>), and blocks of code are enclosed in braces (<code>{ ... }</code>).</li>
  <li><code>printf()</code> outputs text, and <code>\\n</code> creates a new line.</li>
  <li><code>return 0;</code> signals to the operating system that your program completed successfully.</li>
</ul>
""")
    add_mod("b2-first-program", "Module 2: Your Very First Program", m2)

    # ============================================================
    # Module B3: Storing Information: Variables and Data Types
    # ============================================================
    m3 = """
<p>Programs are not static text; they exist to process information. To do that, a program needs a way to remember numbers, words, and choices. We store information using <strong>variables</strong>.</p>

<h3>3.1 The Labeled Storage Box Analogy</h3>
<p>Imagine your computer's RAM as a vast warehouse. A <strong>variable</strong> is like a sturdy cardboard box with a name tag written on the outside:</p>
<ul>
  <li><strong>The Variable Name (Identifier):</strong> The name written on the box (e.g. <code>player_score</code> or <code>age</code>).</li>
  <li><strong>The Data Type:</strong> The specific shape of the box, which determines what kind of items can fit inside (e.g. integers only, or decimal numbers only).</li>
  <li><strong>The Value:</strong> The actual item currently sitting inside the box (e.g. <code>100</code>).</li>
</ul>

<h3>3.2 Fundamental Data Types</h3>
<p>Computers handle different kinds of information differently. Storing someone's age is very different from storing their bank account balance or their name. Here are the core data types you will use every day:</p>

<div class="table-container">
<table>
  <thead>
    <tr><th>Type Keyword</th><th>Name &amp; Purpose</th><th>Typical Size</th><th>Example Values</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><code>int</code></td>
      <td><strong>Integer:</strong> Whole numbers (positive, negative, and zero).</td>
      <td>4 bytes (32 bits)</td>
      <td><code>0</code>, <code>42</code>, <code>-15</code>, <code>1000000</code></td>
    </tr>
    <tr>
      <td><code>float</code></td>
      <td><strong>Floating-Point:</strong> Numbers with decimal points.</td>
      <td>4 bytes (32 bits)</td>
      <td><code>3.14f</code>, <code>-0.5f</code>, <code>99.99f</code></td>
    </tr>
    <tr>
      <td><code>double</code></td>
      <td><strong>Double Precision:</strong> Extra-precise decimal numbers.</td>
      <td>8 bytes (64 bits)</td>
      <td><code>2.718281828459</code></td>
    </tr>
    <tr>
      <td><code>char</code></td>
      <td><strong>Character:</strong> A single letter, digit, or symbol in single quotes.</td>
      <td>1 byte (8 bits)</td>
      <td><code>'A'</code>, <code>'z'</code>, <code>'!'</code>, <code>'9'</code></td>
    </tr>
    <tr>
      <td><code>bool</code></td>
      <td><strong>Boolean:</strong> Truth values for logic and decision making.</td>
      <td>1 byte</td>
      <td><code>true</code> or <code>false</code></td>
    </tr>
    <tr>
      <td><code>const char*</code></td>
      <td><strong>String:</strong> A sequence of text enclosed in double quotes.</td>
      <td>Pointer size</td>
      <td><code>"Hello, World!"</code></td>
    </tr>
  </tbody>
</table>
</div>

<h3>3.3 Declaring and Using Variables</h3>
<p>Here is how you create and use variables in Rook:</p>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    // 1. Explicit type declaration
    int player_health = 100;
    float walking_speed = 4.5f;
    char rank_letter = 'A';
    bool is_alive = true;

    // 2. Printing variable values
    // %d prints an integer, %.1f prints a float, %c prints a char
    printf("Health: %d\\n", player_health);
    printf("Speed: %.1f\\n", walking_speed);
    printf("Rank: %c\\n", rank_letter);

    // 3. Modifying a variable
    player_health = 85; // Took 15 damage!
    printf("Updated Health: %d\\n", player_health);

    return 0;
}
""", "Variables in Action") + """
<div class="callout callout-note">
  <div class="callout-title">💡 Rook's Automatic Zero-Initialization</div>
  <div class="callout-body">
    In older languages like C, if you write <code>int score;</code> without giving it a value, the box contains random garbage left over in RAM from whatever app ran earlier! In Rook, if you don't give a variable a value, it is <strong>guaranteed to start at 0</strong>.
  </div>
</div>
""" + make_callout("tip", "Key Takeaways of Module 3", """
<ul>
  <li>A variable is a named storage location in memory that holds a specific type of data.</li>
  <li><code>int</code> is for whole numbers, <code>float</code> for decimals, <code>char</code> for single characters, <code>bool</code> for true/false.</li>
  <li>Use <code>=</code> to assign or update a value inside a variable box.</li>
</ul>
""")
    add_mod("b3-variables-types", "Module 3: Storing Information: Variables and Types", m3)

    # ============================================================
    # Module B4: Doing Math & Making Comparisons (Operators & Expressions)
    # ============================================================
    m4 = """
<p>Once you have variables, you want to perform calculations and make comparisons. In programming, we do this using <strong>operators</strong>.</p>

<h3>4.1 Basic Arithmetic Operators</h3>
<p>You already know most of these from middle school math:</p>
<ul>
  <li><code>+</code> (Addition): <code>10 + 5</code> results in <code>15</code></li>
  <li><code>-</code> (Subtraction): <code>10 - 5</code> results in <code>5</code></li>
  <li><code>*</code> (Multiplication): <code>10 * 5</code> results in <code>50</code></li>
  <li><code>/</code> (Division): <code>10 / 5</code> results in <code>2</code></li>
  <li><code>%</code> (Modulo / Remainder): <code>10 % 3</code> results in <code>1</code> (because 3 goes into 10 three times with 1 left over).</li>
</ul>

<div class="callout callout-warn">
  <div class="callout-title">⚠️ The Integer Division Surprise!</div>
  <div class="callout-body">
    What happens if you divide <code>7 / 2</code> using integers? In math class, the answer is <code>3.5</code>. But because both 7 and 2 are <code>int</code>, the computer performs <em>integer division</em>: it throws away the decimal remainder and gives you <code>3</code>! If you want the decimal <code>3.5</code>, at least one of the numbers must be a decimal: <code>7.0f / 2.0f</code>.
  </div>
</div>

<h3>4.2 Comparison Operators (Asking Questions)</h3>
<p>Computers make decisions by testing whether something is true or false:</p>
<ul>
  <li><code>==</code> (Is Equal To?): <code>x == 5</code> (Notice the double equals! Single <code>=</code> assigns a value; double <code>==</code> compares values).</li>
  <li><code>!=</code> (Is NOT Equal To?): <code>x != 5</code> (True if x is anything other than 5).</li>
  <li><code>&gt;</code> and <code>&lt;</code> (Greater than / Less than): <code>score &gt; 100</code></li>
  <li><code>&gt;=</code> and <code>&lt;=</code> (Greater than or equal / Less than or equal): <code>age &gt;= 18</code></li>
</ul>

<h3>4.3 Boolean Logic: Combining Conditions</h3>
<p>Often you need to check multiple conditions at the same time:</p>
<ul>
  <li><strong>Logical AND (<code>&amp;&amp;</code>):</strong> Both conditions must be true.<br>
  <em>Analogy:</em> To ride the roller coaster, you must be tall enough <strong>AND</strong> have a ticket. If either is missing, you cannot ride.</li>
  <li><strong>Logical OR (<code>||</code>):</strong> At least one condition must be true.<br>
  <em>Analogy:</em> You get a discount if it is Sunday <strong>OR</strong> you are a student. If either is true, you get the discount.</li>
  <li><strong>Logical NOT (<code>!</code>):</strong> Flips true to false, and false to true.<br>
  <em>Analogy:</em> <code>!is_raining</code> means "it is NOT raining".</li>
</ul>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    int age = 20;
    bool has_id = true;

    // Both must be true
    if (age >= 18 && has_id) {
        printf("Access granted: adult with ID.\\n");
    }

    // Remainder calculation
    int items = 14;
    int pack_size = 4;
    int leftover = items % pack_size;
    printf("Leftover items: %d\\n", leftover); // Prints 2

    return 0;
}
""", "Operators in Action") + make_callout("tip", "Key Takeaways of Module 4", """
<ul>
  <li>Integer division discards decimals; use floats when you need fractions.</li>
  <li>Use <code>==</code> to compare values, and <code>=</code> to assign values.</li>
  <li><code>&&</code> means AND (both true); <code>||</code> means OR (either true); <code>!</code> means NOT (invert).</li>
</ul>
""")
    add_mod("b4-math-operators", "Module 4: Doing Math & Making Comparisons", m4)

    # ============================================================
    # Module B5: Making Choices: Control Flow (if Statements)
    # ============================================================
    m5 = """
<p>If code only executed straight down from top to bottom, programs would be completely inflexible. To build intelligent software, we need the computer to make decisions: "If the password is correct, log the user in; otherwise, show an error message." We call this <strong>control flow</strong>.</p>

<h3>5.1 The <code>if</code>, <code>else if</code>, and <code>else</code> Construct</h3>
<p>Think of an <code>if</code> statement as a fork in the road. The computer evaluates a condition inside parentheses: if the condition is <code>true</code>, it takes the first path; otherwise, it skips ahead.</p>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    int score = 85;

    if (score >= 90) {
        printf("Grade: A - Outstanding!\\n");
    } else if (score >= 80) {
        printf("Grade: B - Great job!\\n");
    } else if (score >= 70) {
        printf("Grade: C - Satisfactory.\\n");
    } else {
        printf("Grade: F - Needs improvement.\\n");
    }

    return 0;
}
""", "Grade Evaluation Branching") + """
<p>How the computer evaluates this:</p>
<ol>
  <li>It checks <code>score &gt;= 90</code>. Since 85 is not &gt;= 90, it moves to the next branch.</li>
  <li>It checks <code>score &gt;= 80</code>. Since 85 is &gt;= 80, this condition is <code>true</code>!</li>
  <li>It runs the block printing "Grade: B", and then <strong>skips all remaining branches</strong> completely!</li>
</ol>

<h3>5.2 Banned Assignment in Conditions</h3>
<p>One of the most catastrophic bugs in old C code was accidentally writing <code>if (score = 100)</code> instead of <code>if (score == 100)</code>. In C, that overwrote the score to 100 and ran the block! In Rook, the compiler checks your code and <strong>strictly refuses to compile</strong> if you put an assignment inside an <code>if</code> condition, protecting you from subtle bugs.</p>
""" + make_callout("tip", "Key Takeaways of Module 5", """
<ul>
  <li>Use <code>if</code> to execute code only when a condition is met.</li>
  <li>Use <code>else if</code> to test alternative conditions, and <code>else</code> as a final catch-all.</li>
  <li>Only the first matching branch in an <code>if/else if</code> chain is executed.</li>
</ul>
""")
    add_mod("b5-control-flow", "Module 5: Making Choices: Control Flow", m5)

    # ============================================================
    # Module B6: Repetition: Automating Work with Loops
    # ============================================================
    m6 = """
<p>Humans get bored and make mistakes when performing the same task over and over again. Computers, on the other hand, can repeat an operation a billion times with flawless precision. To repeat code, we use <strong>loops</strong>.</p>

<h3>6.1 The <code>while</code> Loop: Repeat While Condition is True</h3>
<p>A <code>while</code> loop checks a condition. As long as that condition remains true, it keeps executing its code block:</p>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    int countdown = 5;

    while (countdown > 0) {
        printf("%d...\\n", countdown);
        countdown = countdown - 1; // Decrement countdown
    }

    printf("Blastoff!\\n");
    return 0;
}
""", "Countdown with a while Loop") + """
<div class="callout callout-warn">
  <div class="callout-title">⚠️ Beware the Infinite Loop!</div>
  <div class="callout-body">
    If you forget to change <code>countdown</code> inside the loop (e.g. leaving out <code>countdown = countdown - 1;</code>), the condition <code>countdown &gt; 0</code> will stay true forever! Your program will freeze in an <em>infinite loop</em>. Always ensure your loop has progress toward termination.
  </div>
</div>

<h3>6.2 The <code>for</code> Loop: The Counting Specialist</h3>
<p>When you know exactly how many times you want to repeat something (like counting from 1 to 10), a <code>for</code> loop is cleaner because it bundles the starting count, the condition, and the step into one line:</p>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    // for (start; condition; step)
    for (int i = 1; i <= 5; i++) {
        printf("Iteration number: %d\\n", i);
    }
    return 0;
}
""", "Counting with a for Loop") + """
<ul>
  <li><code>int i = 1;</code>: Runs once at the very beginning to initialize the counter.</li>
  <li><code>i &lt;= 5;</code>: Checked before every iteration. If false, the loop ends.</li>
  <li><code>i++</code>: Shorthand for <code>i = i + 1</code>. Runs after every iteration to increment the counter.</li>
</ul>

<h3>6.3 Controlling Loops: <code>break</code> and <code>continue</code></h3>
<ul>
  <li><strong><code>break</code>:</strong> Immediately exits the loop, skipping any remaining cycles.</li>
  <li><strong><code>continue</code>:</strong> Immediately stops the current iteration and jumps directly to the next cycle.</li>
</ul>
""" + make_callout("tip", "Key Takeaways of Module 6", """
<ul>
  <li>Loops automate repetitive work without copy-pasting code.</li>
  <li>Use <code>while</code> when you don't know in advance how many repetitions you will need.</li>
  <li>Use <code>for</code> when counting or iterating a known number of times.</li>
  <li>Always ensure the loop condition eventually becomes false to avoid freezing in an infinite loop.</li>
</ul>
""")
    add_mod("b6-loops-repetition", "Module 6: Repetition: Automating with Loops", m6)

    # ============================================================
    # Module B7: Functions: Reusable Building Blocks
    # ============================================================
    m7 = """
<p>As programs grow, writing all your logic inside <code>main()</code> becomes messy and unmaintainable. To organize software, we divide code into small, named, reusable pieces called <strong>functions</strong>.</p>

<h3>7.1 The Recipe &amp; Kitchen Appliance Analogy</h3>
<p>Think of a function as an appliance, like a blender or toaster:</p>
<ul>
  <li><strong>Inputs (Parameters):</strong> What you feed into the machine (e.g. slices of bread).</li>
  <li><strong>The Work (Function Body):</strong> What happens inside (heating the coils for 2 minutes).</li>
  <li><strong>Output (Return Value):</strong> What comes out of the machine (warm toast).</li>
</ul>
""" + make_code_box("rook", """
#include <stdio.h>

// A function that takes two integers, adds them, and returns the result
fn add(a: int, b: int) -> int {
    return a + b;
}

// A function that doesn't return anything (void)
fn greet(name: const char*) {
    printf("Welcome to Rook, %s!\\n", name);
}

int main() {
    // Calling our functions
    greet("Alice");
    greet("Bob");

    int sum = add(15, 25);
    printf("15 + 25 = %d\\n", sum);

    return 0;
}
""", "Creating and Calling Functions") + """
<h3>7.2 Variable Scope: Where Variables Live and Die</h3>
<p>Variables created inside a function are <strong>local</strong> to that function. They are born when the function begins running, and they are completely destroyed when the function finishes. A function cannot see or accidentally change local variables inside another function.</p>
""" + make_callout("tip", "Key Takeaways of Module 7", """
<ul>
  <li>Functions break large programs into small, readable, testable, and reusable blocks.</li>
  <li>Parameters are inputs passed into the function; the <code>return</code> statement sends an output back.</li>
  <li>Functions with no return value use <code>void</code>.</li>
  <li>Local variables only exist inside their own function, preventing unintended side effects.</li>
</ul>
""")
    add_mod("b7-functions", "Module 7: Functions: Reusable Building Blocks", m7)

    # ============================================================
    # Module B8: Grouping Data: Structures (struct)
    # ============================================================
    m8 = """
<p>In the real world, things are rarely single numbers. A person has a name, age, and height. A car has a make, model, year, and fuel level. Storing these as loose, disconnected variables (like <code>car1_year</code>, <code>car2_year</code>, <code>car3_year</code>) quickly creates chaos. In programming, we bundle related variables together using a <strong>struct</strong> (short for structure).</p>

<h3>8.1 Defining and Creating a Struct</h3>
<p>A struct is like designing your own custom data blueprint:</p>
""" + make_code_box("rook", """
#include <stdio.h>

// Define our custom blueprint
struct Player {
    name: const char*;
    health: int;
    score: int;
};

int main() {
    // Create an instance of Player and initialize its fields
    Player hero = Player{
        name: "Knight Arthur",
        health: 100,
        score: 0
    };

    // Access fields using the dot (.) operator
    printf("Player: %s\\n", hero.name);
    printf("Health: %d HP\\n", hero.health);

    // Modify a field
    hero.score += 500;
    printf("New Score: %d points\\n", hero.score);

    return 0;
}
""", "Using Structs to Model Data") + """
<p>Notice how clean this is: <code>hero</code> is a single unified package containing all the state for Arthur. If you need a second player, you just declare <code>Player villain = Player{ ... };</code>.</p>
""" + make_callout("tip", "Key Takeaways of Module 8", """
<ul>
  <li>A <code>struct</code> lets you create custom data types that group related fields together.</li>
  <li>Use designated initializers (<code>Player{ name: "Arthur", ... }</code>) to set field values clearly.</li>
  <li>Use the dot operator (<code>player.health</code>) to read or update individual fields.</li>
</ul>
""")
    add_mod("b8-structs", "Module 8: Grouping Data: Structures", m8)

    # ============================================================
    # Module B9: Demystifying Memory: Addresses & Pointers
    # ============================================================
    m9 = """
<p>Pointers have a scary reputation among programming students. But once you learn the fundamental real-world analogy, pointers are surprisingly simple and intuitive.</p>

<h3>9.1 The House vs. The Street Address Analogy</h3>
<p>Imagine a long residential street where every house has a unique number painted on the curb (like 100 Main St, 102 Main St, 104 Main St):</p>
<ul>
  <li><strong>The Variable:</strong> The physical house itself, containing furniture, people, and rooms (the data).</li>
  <li><strong>The Pointer:</strong> A small slip of paper with the <em>address</em> of the house written on it.</li>
</ul>

<p>The slip of paper is not the house! It is tiny and lightweight, but if you look at the address written on it, you can walk down the street to the house and see what is inside.</p>

<h3>9.2 The Two Essential Operators: <code>&amp;</code> and <code>*</code></h3>
<ol>
  <li><strong>Address-of Operator (<code>&amp;</code>):</strong> "What is the street address of this variable?"<br>
  If <code>x</code> is a variable, <code>&amp;x</code> gives you the memory address where <code>x</code> lives in RAM.</li>
  <li><strong>Dereference Operator (<code>*</code>):</strong> "Go to the house at this address and look/change what is inside."<br>
  If <code>ptr</code> holds the address of <code>x</code>, then <code>*ptr</code> reaches into that house!</li>
</ol>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    int gold = 50;       // A house containing 50 gold pieces
    int* ptr = &gold;    // A paper holding the memory address of gold

    printf("Value of gold: %d\\n", gold);
    printf("Memory address of gold: %p\\n", (void*)ptr);

    // Reach into the house using the pointer and change the gold!
    *ptr = 100;

    printf("Updated gold: %d\\n", gold); // Now prints 100!
    return 0;
}
""", "Pointers in Action") + """
<h3>9.3 Why Do We Need Pointers?</h3>
<p>If you already have the variable, why bother writing down its address?</p>
<ul>
  <li><strong>Modifying variables inside functions:</strong> When you pass a variable to a function, the computer usually makes a copy (pass-by-value). If the function modifies its copy, your original variable remains unchanged. But if you pass a pointer (the address), the function can reach back into your original memory and update it directly!</li>
  <li><strong>Efficiency:</strong> If you have a huge struct holding 1,000 game objects, copying it takes time and RAM. Passing a pointer to that struct only copies an 8-byte address, which takes less than a nanosecond!</li>
</ul>

<h3>9.4 What is a NULL Pointer?</h3>
<p>A <strong>null pointer</strong> is a slip of paper with address <code>0</code> (nowhere). If you try to dereference a null pointer (knocking on a door at an address that does not exist), the operating system immediately protects itself by terminating your program with a <em>segmentation fault</em> (crash). Always make sure a pointer points to valid memory before dereferencing it.</p>
""" + make_callout("tip", "Key Takeaways of Module 9", """
<ul>
  <li>A variable holds data; a pointer holds the memory address where data lives.</li>
  <li><code>&x</code> gets the address of <code>x</code>.</li>
  <li><code>*ptr</code> accesses or modifies the value at the address stored in <code>ptr</code>.</li>
  <li>Pointers allow functions to modify caller data and pass large structures with zero copy overhead.</li>
</ul>
""")
    add_mod("b9-memory-pointers", "Module 9: Demystifying Memory: Pointers", m9)

    # ============================================================
    # Module B10: Working with Lists: Arrays
    # ============================================================
    m10 = """
<p>What if you need to store the high scores of 100 players, or the temperature readings for every day of the year? Creating 365 separate variables would be unbearable. To store an ordered collection of items of the same type, we use an <strong>array</strong>.</p>

<h3>10.1 The Row of Lockers Analogy</h3>
<p>Think of an array as a straight row of identical, numbered metal lockers in a school hallway:</p>
<ul>
  <li>All lockers in the row hold the same kind of thing (e.g. all hold integers).</li>
  <li>The lockers sit side-by-side in memory in a continuous sequence.</li>
</ul>

<h3>10.2 Why Do Programmers Start Counting at 0?</h3>
<p>In programming, array indices start at <code>0</code>, not <code>1</code>. Why? Because the index is actually an <strong>offset</strong> (distance from the start):</p>
<ul>
  <li>Locker <code>[0]</code> is 0 steps away from the beginning (it is the very first locker).</li>
  <li>Locker <code>[1]</code> is 1 step away from the beginning (the second locker).</li>
  <li>If an array has 5 items, their indices are <code>0, 1, 2, 3, 4</code>.</li>
</ul>
""" + make_code_box("rook", """
#include <stdio.h>

int main() {
    // An array of 5 integers
    int scores[5] = {95, 88, 72, 99, 84};

    // Reading specific elements
    printf("First score: %d\\n", scores[0]); // 95
    printf("Third score: %d\\n", scores[2]); // 72

    // Modifying an element
    scores[2] = 78;

    // Looping through all elements to calculate the sum
    int total = 0;
    for (int i = 0; i < 5; i++) {
        total += scores[i];
    }

    printf("Total sum: %d\\n", total);
    printf("Average: %.1f\\n", (float)total / 5.0f);

    return 0;
}
""", "Working with Arrays") + """
<div class="callout callout-warn">
  <div class="callout-title">⚠️ Array Bounds: Never Step Off the Edge!</div>
  <div class="callout-body">
    If an array has 5 items (indices 0 through 4), trying to read or write to <code>scores[10]</code> is called an <em>out-of-bounds access</em> or <em>buffer overflow</em>. In C, this can read arbitrary memory or corrupt other variables. In Rokade, compiling with the <code>-b</code> flag adds automatic runtime checks that halt the program safely if you go out of bounds.
  </div>
</div>
""" + make_callout("tip", "Key Takeaways of Module 10", """
<ul>
  <li>An array stores a fixed-size sequence of elements of the same type in continuous memory.</li>
  <li>Indices start at <code>0</code>: an array of size N has valid indices from 0 to N - 1.</li>
  <li>Combine loops and arrays to process large amounts of data with minimal code.</li>
</ul>
""")
    add_mod("b10-arrays", "Module 10: Working with Lists: Arrays", m10)

    # ============================================================
    # Module B11: Building Real Projects: Organizing Multiple Files
    # ============================================================
    m11 = """
<p>When you build real-world software—like a game engine, a web server, or a desktop app—putting thousands of lines of code into a single file becomes impossible to navigate. Professional software is organized across multiple files and modules.</p>

<h3>11.1 Project Structure</h3>
<p>When you scaffold a new project with <code>rokade new my_project</code>, Rokade generates a clean, standardized layout:</p>
""" + make_code_box("text", """
my_project/
├── rokade.toml          # Project configuration, name, version, and dependencies
├── src/
│   ├── main.rook        # Entry point containing main()
│   ├── math_utils.rook  # Helper functions
│   └── physics.rook     # Physics calculations
└── build/               # Generated binaries and compiled artifacts
""", "Standard Project Layout") + """
<h3>11.2 Connecting Files with <code>#comprise</code></h3>
<p>In standard C, connecting two files requires creating and synchronizing <code>.h</code> header files with header guards. In Rook, you simply use the <code>#comprise</code> directive:</p>
""" + make_code_box("rook", """
// src/math_utils.rook
fn square(x: int) -> int {
    return x * x;
}

// src/main.rook
#comprise math_utils
#include <stdio.h>

int main() {
    int val = 8;
    printf("%d squared is %d\\n", val, square(val));
    return 0;
}
""", "Modular Multi-File Code") + """
<p>When Rokade builds your program, it automatically reads <code>math_utils.rook</code>, verifies its types, and compiles both files into a single unified binary without duplicate symbols.</p>
""" + make_callout("tip", "Key Takeaways of Module 11", """
<ul>
  <li>Real software is split into separate files by topic (math, graphics, network).</li>
  <li><code>rokade.toml</code> configures your package name, version, and compiler settings.</li>
  <li>Use <code>#comprise filename</code> to import and share functions across files cleanly without header boilerplate.</li>
</ul>
""")
    add_mod("b11-multi-file", "Module 11: Building Real Projects", m11)

    # ============================================================
    # Module B12: How to Think Like a Programmer & Debugging
    # ============================================================
    m12 = """
<p>Programming is not about memorizing syntax rules; it is about <strong>problem solving and computational thinking</strong>. When beginners run into errors or bugs, they often feel frustrated. But in reality, debugging is 80% of what professional software engineers do every day!</p>

<h3>12.1 Problem Decomposition: Breaking Big Problems into Tiny Steps</h3>
<p>When faced with a complex task (like "build a chess game"), the task feels overwhelming. The secret of programming is <strong>decomposition</strong>:</p>
<ol>
  <li>Don't try to build the whole thing at once.</li>
  <li>Break the problem down into smaller and smaller pieces until each piece is trivial.</li>
  <li><em>Example:</em> "How do I make a chess board?" ➔ "How do I represent 64 squares?" ➔ "How do I represent 1 square?" ➔ "A struct with an X and Y coordinate!"</li>
  <li>Solve that one tiny piece, verify that it works, and build the next piece.</li>
</ol>

<h3>12.2 Reading Compiler Errors Without Fear</h3>
<p>When the compiler prints a scary red error message, do not panic! The compiler is not angry with you; it is a helpful assistant pointing out where something doesn't make sense:</p>
<ol>
  <li><strong>Look for the file and line number:</strong> It will look like <code>src/main.rook:14:5: error</code>. This tells you exactly where the problem occurred (line 14, column 5).</li>
  <li><strong>Read the error description:</strong> Usually it's something simple, like a missing semicolon <code>;</code>, an undefined variable name, or passing the wrong type of argument.</li>
  <li><strong>Fix only the first error:</strong> Often, one typo causes 10 secondary errors below it. Fix the very first error at the top, recompile, and see if the rest disappear!</li>
</ol>

<h3>12.3 Print Debugging: Your Most Reliable Superpower</h3>
<p>When a program compiles successfully but does not give the expected answer, the easiest way to find the bug is <strong>print debugging</strong>. Add <code>printf()</code> statements at critical steps to inspect variable values:</p>
""" + make_code_box("rook", """
int calculate_discount(int price, int customer_years) {
    printf("[DEBUG] Starting calculate_discount: price=%d, years=%d\\n", price, customer_years);
    
    int discount = 0;
    if (customer_years > 5) {
        discount = 20;
    }
    printf("[DEBUG] Calculated discount: %d\\n", discount);
    
    return price - discount;
}
""", "Print Debugging in Practice") + """
<p>By watching the printed output, you can see exactly where the computer's logic diverged from what you intended.</p>

<h3>12.4 Welcoming Mistakes on Your Journey</h3>
<p>Every senior engineer, language designer, and technology creator spent thousands of hours making typos, forgetting semicolons, causing segmentation faults, and troubleshooting compiler errors. Making mistakes is not a sign that you aren't suited for programming—it is the universal mechanism by which every programmer learns. Welcome the challenges, be curious, and have fun building!</p>
""" + make_callout("tip", "Key Takeaways of Module 12", """
<ul>
  <li>Break big problems down into small, easily solvable steps.</li>
  <li>Read compiler errors from top to bottom; fix the first error first.</li>
  <li>Use print debugging to inspect variables when programs don't behave as expected.</li>
  <li>Bugs are normal and natural—treat every error as a fun puzzle to solve.</li>
</ul>
""")
    add_mod("b12-debugging-thinking", "Module 12: How to Think Like a Programmer", m12)

    return modules

if __name__ == "__main__":
    def dummy_box(lang, code, title=""): return f"[BOX {lang} {title}]"
    def dummy_call(kind, title, body): return f"[CALL {kind} {title}]"
    mods = get_beginner_modules(dummy_box, dummy_call)
    print(f"Loaded {len(mods)} beginner modules successfully!")
