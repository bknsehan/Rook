#!/usr/bin/env python3
"""Generate src/libc/commandlist.json (the C API signatures rokade loads)
by parsing real libc headers with libclang.

Maintainer-only: end users ship the committed JSON and do NOT need Python or
libclang. This script regenerates that file from the host C toolchain so the
signatures (return types, parameter types, arity, variadic markers) stay
accurate. sema.c::cl_load reads the JSON; it enforces argument count and
records the return type (non-void) for C calls.

Output shape:
  [{"name":"strlen","ret":"size_t","params":[{"name":"arg0","type":"const char*"},...]}]
Variadic functions carry a trailing {"name":"...","type":"..."} marker (not
counted toward the parameter count).

Usage:
  python3 scripts/gen_commandlist.py [--headers a.h,b.h,...] \
      [--libclang /path/to/libclang.so] [--out src/libc/commandlist.json]
"""
from __future__ import annotations
import argparse
import json
import os
import subprocess
import sys


DEFAULT_WHITELIST = [
    "printf", "fprintf", "sprintf", "snprintf", "scanf", "sscanf",
    "puts", "putchar", "getchar", "perror", "fflush",
    "fopen", "fclose", "fread", "fwrite", "fgets", "fgetc", "fputc",
    "fputs", "feof", "ferror", "ftell", "fseek", "rewind",
    "getline", "getdelim",
    "strlen", "strcmp", "strcpy", "strcat", "strdup", "strchr",
    "strrchr", "strstr", "strcspn", "strtok", "strncmp", "strncpy",
    "strncat", "memcpy", "memset", "memcmp", "memmove", "memchr",
    "malloc", "calloc", "realloc", "free",
    "abs", "labs", "llabs", "rand", "srand", "exit",
    "atoi", "atol", "atof", "strtol", "strtoul", "strtod", "strtoll",
    "pow", "sqrt", "cbrt", "sin", "cos", "tan", "asin", "acos", "atan",
    "exp", "log", "log2", "log10", "ceil", "floor", "fabs",
    "getenv", "system", "qsort", "bsearch",
]

# sys/types.h BEFORE stdio.h so ssize_t/size_t resolve cleanly under _GNU_SOURCE.
DEFAULT_HEADERS = ["sys/types.h", "stdio.h", "string.h", "stdlib.h", "math.h",
                   "ctype.h", "stdint.h", "stddef.h", "stdarg.h"]


def find_libclang():
    try:
        import clang.cindex as ci
    except ImportError:
        sys.stderr.write("error: python `clang` bindings not installed (pip install clang).\n")
        sys.exit(1)
    candidates = [os.environ.get("LIBCLANG_PATH")]
    candidates += [
        "/usr/lib/llvm-22/lib/libclang.so",
        "/usr/lib/llvm-21/lib/libclang.so",
        "/usr/lib/llvm-20/lib/libclang.so",
        "/usr/lib/llvm-19/lib/libclang.so",
        "/usr/lib/llvm-18/lib/libclang.so",
        "/usr/lib/x86_64-linux-gnu/libclang-16.so.1",
        "/usr/lib/x86_64-linux-gnu/libclang-15.so.1",
        "/usr/lib/x86_64-linux-gnu/libclang.so.1",
        "/usr/lib/x86_64-linux-gnu/libclang.so",
        "/usr/lib/libclang.so.1",
        "/usr/lib/libclang.so",
        "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib/libclang.dylib",
    ]
    for c in candidates:
        if c and os.path.exists(c):
            return ci, c
    return ci, None


def get_cc_include_paths():
    """System C compiler include dirs (where stddef.h / size_t live)."""
    paths = []
    for cc in ("gcc", "clang"):
        try:
            out = subprocess.run([cc, "-E", "-Wp,-v", "-x", "c", "-"],
                                 capture_output=True, text=True, input="", timeout=15)
            err = out.stderr
        except Exception:
            continue
        if "search starts here" not in err:
            continue
        inpaths = False
        for line in err.splitlines():
            s = line.strip()
            if "search starts here" in s:
                inpaths = True; continue
            if "End of search list" in s:
                inpaths = False; continue
            if inpaths and s:
                paths.append(s)
        if paths:
            break
    # de-dup, keep order
    seen = set(); uniq = []
    for p in paths:
        if p not in seen:
            seen.add(p); uniq.append(p)
    return uniq


def is_variadic(ci, cursor):
    try:
        return bool(ci.conf.lib.clang_Cursor_isVariadic(cursor))
    except Exception:
        return False


# Internal compiler typedefs -> public names (for clean display / C emission).
TYPE_RENAMES = {
    "__ssize_t": "ssize_t",
    "__size_t": "size_t",
    "__compar_fn_t": "int (*)(const void*, const void*)",
    "__int128": "__int128",
    "__uint128_t": "__uint128_t",
}


def normalize_type(s):
    if not s:
        return "void"
    s = s.strip()
    s = " ".join(s.split())  # collapse whitespace
    # pull '*' together: "const char *" -> "const char*", "char * *" -> "char**"
    while " *" in s or "* " in s:
        s = s.replace(" *", "*").replace("* ", "*")
    # strip restrict noise from parameters.
    for tok in ("restrict", "__restrict", "__restrict__"):
        s = s.replace(" " + tok, "").replace(tok, "")
    s = " ".join(s.split())
    s = TYPE_RENAMES.get(s, s)
    return s


def build_tu(ci, headers, libclang_path):
    args = ["-xc", "-std=c11", "-D_GNU_SOURCE", "-D_POSIX_C_SOURCE=200809L"]
    for p in get_cc_include_paths():
        if os.path.exists(p):
            args.append("-I" + p)
    lines = ["#include <%s>" % h for h in headers]
    src = "\n".join(lines) + "\n"
    tmp = "/tmp/_rook_commandlist_probe.c"
    with open(tmp, "w") as f:
        f.write(src)
    if libclang_path:
        try:
            ci.Config.set_library_file(libclang_path)
        except Exception:
            pass
    idx = ci.Index.create()
    return idx.parse(tmp, args=args,
                     options=ci.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)


def collect(ci, tu, whitelist):
    from clang.cindex import CursorKind
    out = {}
    decl_files = {}
    for c in tu.cursor.walk_preorder():
        if c.kind != CursorKind.FUNCTION_DECL:
            continue
        name = c.spelling
        if name not in whitelist:
            continue
        loc = c.location.file.name if (c.location and c.location.file) else ""
        if name in decl_files and not (loc and (loc.endswith("stdio.h") or loc.endswith("string.h")
                                                or loc.endswith("stdlib.h") or loc.endswith("math.h"))):
            continue
        decl_files[name] = loc
        rt = c.result_type
        ret = normalize_type(rt.spelling) if rt else "void"
        params = []
        try:
            arg_types = list(c.type.argument_types())
        except Exception:
            arg_types = []
        for at in arg_types:
            params.append(normalize_type(at.spelling))
        if is_variadic(ci, c):
            params.append("...")
        out[name] = {"ret": ret, "params": params}
    return out


def emit_json(funcs):
    arr = []
    for name in sorted(funcs):
        f = funcs[name]
        params = []
        for i, t in enumerate(f["params"]):
            label = "..." if t == "..." else ("arg%d" % i)
            params.append({"name": label, "type": t})
        arr.append({"name": name, "ret": f["ret"], "params": params})
    return json.dumps(arr, indent=2) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--headers", default=",".join(DEFAULT_HEADERS))
    ap.add_argument("--libclang", default=None)
    ap.add_argument("--out", default=None, help="write JSON here (default: stdout)")
    ap.add_argument("--whitelist", default=None,
                    help="comma-separated extra function names to include")
    args = ap.parse_args()

    ci, libclang_path = find_libclang()
    if args.libclang:
        libclang_path = args.libclang
    headers = [h.strip() for h in args.headers.split(",") if h.strip()]
    whitelist = list(DEFAULT_WHITELIST)
    if args.whitelist:
        whitelist += [w.strip() for w in args.whitelist.split(",") if w.strip()]
    whitelist = set(whitelist)

    tu = build_tu(ci, headers, libclang_path)
    for d in tu.diagnostics:
        sys.stderr.write("warning: %s\n" % d.spelling)

    funcs = collect(ci, tu, whitelist)
    missing = sorted(whitelist - set(funcs))
    if missing:
        sys.stderr.write("warning: not found in libc headers: %s\n" % ", ".join(missing))

    text = emit_json(funcs)
    if args.out:
        with open(args.out, "w") as f:
            f.write(text)
        sys.stderr.write("wrote %d functions to %s\n" % (len(funcs), args.out))
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
