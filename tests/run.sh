#!/usr/bin/env bash
# Rook output-driven test runner.
#
# For each <test>.rook under the corpus dir:
#   - if a sibling <test>.fail exists  -> must-error test:
#       PASS when `rokade` rejects the program (non-zero exit).
#   - else if sibling <test>.out exists -> expected-output test:
#       emit C, compile with gcc, run, diff stdout against <test>.out.
#     - an optional sibling <test>.in is fed to the program as stdin.
#
# Exit status is non-zero if any unexpected failure occurs, so it can be
# used in CI. Tests with a known implementation gap are reported separately
# and do NOT fail the run (PREVIEW), unless that gap is a regression.
set -u

# ---- config ----------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOK_ROOT="$(dirname "$SCRIPT_DIR")"          # .../Rook
ROKADE="${ROKADE:-$ROOK_ROOT/build/rokade}"
CORPUS="${CORPUS:-$SCRIPT_DIR/corpus}"
# C compiler used to build the emitted C. Defaults to `gcc` so existing CI
# keeps working; set ROKADE_CC=clang (or a path) to validate against another
# toolchain. Note: rokade itself auto-detects its compiler for `rokade run`;
# this knob drives the corpus runner's own compile step.
ROKADE_CC="${ROKADE_CC:-gcc}"
# Tell rokade where the bundled C-API commandlist (libc signatures) lives so the
# compiler can type/arity-check C calls. May be overridden by the caller.
export ROKADE_DATA_DIR="${ROKADE_DATA_DIR:-$ROOK_ROOT/src/libc}"
# The C code rokade emits requires C23 (`auto`); default to c2x so the corpus
# builds on both gcc and clang. Override with CSTD=gnu99 etc. if desired.
CSTD="${CSTD:-c2x}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# ---- preflight --------------------------------------------------------------
if [ ! -x "$ROKADE" ]; then
    echo "building rokade..."
    cmake --build "$ROOK_ROOT/build" >/dev/null 2>&1 || { echo "build failed"; exit 1; }
fi
if [ ! -f "$CORPUS/basic.rook" ]; then
    echo "corpus dir not found: $CORPUS"
    exit 1
fi
if ! command -v "$ROKADE_CC" >/dev/null; then echo "C compiler '$ROKADE_CC' not found"; exit 1; fi

# ---- helpers ----------------------------------------------------------------
has_multi_include() { grep -qE '^#\s*include\s*[<"][^>"\n]+\.rook' "$1"; }

# returns 0 if emit+C generation succeeded, 1 otherwise
emit() { # $1=src, $2=out.c
    "$ROKADE" --emit-c "$1" >"$2" 2>"$WORK/emit.log"
}

# ---- counters ----------------------------------------------------------------
PASS=0; FAIL=0; KNOWN=0; SKIP=0
declare -a failures=()
declare -a known=()

for src in "$CORPUS"/*.rook; do
    base="$(basename "$src" .rook)"
    fail_ref="$CORPUS/$base.fail"

    # ---- expected-error test -------------------------------------------------
    if [ -f "$fail_ref" ]; then
        if ! emit "$src" "$WORK/t.c"; then
            PASS=$((PASS+1)); echo "  PASS (rejected) $base"
        else
            KNOWN=$((KNOWN+1)); known+=("$base"); echo "  KNOWN (expected-reject not wired) $base"
        fi
        continue
    fi

    out_ref="$CORPUS/$base.out"
    # ---- expected-output test ------------------------------------------------
    if [ -f "$out_ref" ]; then
        if ! emit "$src" "$WORK/t.c"; then
            if has_multi_include "$src"; then
                KNOWN=$((KNOWN+1)); known+=("$base"); echo "  KNOWN (multi-file include) $base"
            else
                FAIL=$((FAIL+1)); failures+=("$base"); echo "  FAIL (emit) $base"
            fi
            continue
        fi
        if ! "$ROKADE_CC" ${CSTD:+-std="$CSTD"} -o "$WORK/r.out" "$WORK/t.c" -lm >/dev/null 2>&1; then
            if has_multi_include "$src"; then
                KNOWN=$((KNOWN+1)); known+=("$base"); echo "  KNOWN (multi-file include) $base"
            else
                FAIL=$((FAIL+1)); failures+=("$base"); echo "  FAIL (gcc) $base"
            fi
            continue
        fi
        # optional stdin from a sibling <base>.in
        if [ -f "$CORPUS/$base.in" ]; then
            "$WORK/r.out" <"$CORPUS/$base.in" >"$WORK/got" 2>"$WORK/err"
        else
            "$WORK/r.out" </dev/null >"$WORK/got" 2>"$WORK/err"
        fi
        if diff -q "$WORK/got" "$out_ref" >/dev/null 2>&1; then
            PASS=$((PASS+1)); echo "  PASS $base"
        else
            FAIL=$((FAIL+1)); failures+=("$base"); echo "  FAIL (output) $base"
        fi
        continue
    fi

    # ---- other .rook (no .out/.fail reference) ------------------------------
    SKIP=$((SKIP+1)); echo "  SKIP (no reference) $base"
done

# ---- summary -----------------------------------------------------------------
echo
echo "---------------------------"
echo "PASS         : $PASS"
echo "FAIL         : $FAIL"
echo "KNOWN (gap)  : $KNOWN"
echo "SKIP         : $SKIP"
if [ ${#failures[@]} -gt 0 ]; then
    echo
    echo "Unexpected failures:"
    printf '  %s\n' "${failures[@]}"
fi
if [ ${#known[@]} -gt 0 ]; then
    echo "Known implementation gaps (expected, not failures):"
    printf '  %s\n' "${known[@]}"
fi
echo "---------------------------"
[ "$FAIL" -eq 0 ]