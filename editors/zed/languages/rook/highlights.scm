; Rook tree-sitter highlights query (based on C grammar)
(identifier) @variable

; Types: PascalCase names (structs, objects, enums)
((identifier) @type
 (#match? @type "^[A-Z][a-zA-Z0-9_]*$"))

; Built-in & primitive type keywords
((identifier) @type
 (#match? @type "^(int|float|double|char|void|bool|size_t|u8|u16|u32|u64|i8|i16|i32|i64|f32|f64|uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t)$"))

; Rook special variable 'self'
((identifier) @variable.special
 (#match? @variable.special "^self$"))

; Rook language keywords
((identifier) @keyword
 (#match? @keyword "^(object|impl|sum|defer|let|match|comprise)$"))

; Boolean & Null constants
((identifier) @constant
 (#match? @constant "^(true|false|null|NULL|nullptr)$"))

; ALL-CAPS constants
((identifier) @constant
 (#match? @constant "^[A-Z][A-Z0-9_]{2,}$"))

; Standard C Keywords
"break" @keyword
"case" @keyword
"const" @keyword
"continue" @keyword
"default" @keyword
"do" @keyword
"else" @keyword
"enum" @keyword
"extern" @keyword
"for" @keyword
"if" @keyword
"inline" @keyword
"return" @keyword
"sizeof" @keyword
"static" @keyword
"struct" @keyword
"switch" @keyword
"typedef" @keyword
"union" @keyword
"volatile" @keyword
"while" @keyword

; Preprocessor Directives
"#define" @keyword
"#elif" @keyword
"#else" @keyword
"#endif" @keyword
"#if" @keyword
"#ifdef" @keyword
"#ifndef" @keyword
"#include" @keyword
(preproc_directive) @keyword

; Operators
"--" @operator
"-" @operator
"-=" @operator
"->" @operator
"=" @operator
"!=" @operator
"*" @operator
"&" @operator
"&&" @operator
"+" @operator
"++" @operator
"+=" @operator
"<" @operator
"<=" @operator
"==" @operator
">" @operator
">=" @operator
"||" @operator
"!" @operator
"%" @operator
"/" @operator

; Delimiters
"." @delimiter
";" @delimiter
":" @delimiter
"," @delimiter

; Literals
(string_literal) @string
(system_lib_string) @string
(null) @constant
(number_literal) @number
(char_literal) @number

; Fields & Declarations
(field_identifier) @property
(statement_identifier) @label
(type_identifier) @type
(primitive_type) @type
(sized_type_specifier) @type

; Function & Method Calls
(call_expression
  function: (identifier) @function)
(call_expression
  function: (field_expression
    field: (field_identifier) @function))
(function_declarator
  declarator: (identifier) @function)
(preproc_function_def
  name: (identifier) @function.special)

; Comments
(comment) @comment
