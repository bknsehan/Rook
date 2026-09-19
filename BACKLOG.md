# Rook Compiler & Standard Library Backlog

This document tracks known observations, planned compiler enhancements, and non-blocking refinements identified during stabilization and pre-freeze verification.

---

## Compiler Pipeline & Code Generation

### 1. Method Invocation on Rvalue / Temporary Struct Expressions
- **Status:** Backlog / Known limitation (pre-existing)
- **Component:** `src/codegen.c` (`case E_CALL`), C backend emission
- **Description:** 
  Calling an `impl` method directly on a temporary/rvalue struct expression (e.g. `str_from_cstr("42").try_to_int(&v)`) fails during host C compiler emission with `error: lvalue required as unary '&' operand`.
  In `codegen.c`, method invocation lowers `obj.method(args)` to `Owner_method(&obj, args)`. When `obj` is an rvalue function call or expression, standard C forbids taking the address directly (`&func()`).
- **Workaround:**
  Assign the result to a named stack variable prior to invoking methods:
  ```rook
  Str s = str_from_cstr("42");
  s.try_to_int(&v);
  ```
- **Planned Solution:**
  In `cg_expr` for `case E_CALL`, detect when the method receiver `m->a` is not an lvalue identifier or dereference, and wrap the call in a statement expression with a scoped temporary:
  `({ Type __tmp = <expr>; Owner_method(&__tmp, args...); })`.

---

## Standard Library & Tooling

### 2. Precise Diagnostics Offset in JSON and TOML Parsers
- **Status:** Backlog / Usability enhancement
- **Component:** `std/json.rook`, `std/toml.rook`
- **Description:**
  `json_valid` and `toml_find` currently report boolean/null failures on syntax or key lookup errors. Adding line/column or byte offset tracking in diagnostic structures will improve configuration debugging.

### 3. Strict RFC 3339 Validation for TOML Datetime Values
- **Status:** Backlog / Usability enhancement
- **Component:** `std/toml.rook`
- **Description:**
  `toml_is_date_time` performs fast shape-based verification of ISO-8601/RFC 3339 timestamps. Adding calendar range checks (leap years, month ranges 1-12, days per month) will provide strict RFC compliance.
