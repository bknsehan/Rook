import subprocess
import json
import sys

def run_lsp_session(messages):
    proc = subprocess.Popen(
        ["./build/rook-lsp"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=False
    )

    full_payload = b""
    for msg in messages:
        body = json.dumps(msg).encode("utf-8")
        header = f"Content-Length: {len(body)}\r\n\r\n".encode("utf-8")
        full_payload += header + body

    stdout_data, stderr_data = proc.communicate(input=full_payload, timeout=5)

    # Parse JSON-RPC responses from stdout
    responses = []
    idx = 0
    while idx < len(stdout_data):
        header_end = stdout_data.find(b"\r\n\r\n", idx)
        if header_end == -1:
            break
        header = stdout_data[idx:header_end].decode("utf-8")
        content_len = 0
        for line in header.split("\r\n"):
            if line.startswith("Content-Length:"):
                content_len = int(line.split(":")[1].strip())
        body_start = header_end + 4
        body = stdout_data[body_start:body_start + content_len].decode("utf-8")
        try:
            responses.append(json.loads(body))
        except Exception as e:
            print(f"Error parsing body: {body}: {e}")
        idx = body_start + content_len

    return responses

def main():
    print("=== Test 1: Initialize & Diagnostics (Error -> Fix) ===")
    test_file_uri = "file:///test_sample.rook"
    broken_code = "int main() {\n    return 10\n"
    fixed_code = "int main() {\n    return 10;\n}\n"

    msgs = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": test_file_uri,
                    "languageId": "rook",
                    "version": 1,
                    "text": broken_code
                }
            }
        },
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didChange",
            "params": {
                "textDocument": {"uri": test_file_uri, "version": 2},
                "contentChanges": [{"text": fixed_code}]
            }
        }
    ]
    resps = run_lsp_session(msgs)
    init_resp = next((r for r in resps if r.get("id") == 1), None)
    assert init_resp is not None, "Missing init response"
    assert "capabilities" in init_resp["result"]
    print("  ✓ Initialize successful")

    diags = [r for r in resps if r.get("method") == "textDocument/publishDiagnostics"]
    assert len(diags) >= 2, f"Expected at least 2 diagnostic notifications, got {len(diags)}"
    assert len(diags[0]["params"]["diagnostics"]) > 0, "Expected error in broken code"
    print(f"  ✓ Broken code correctly flagged diagnostic: {diags[0]['params']['diagnostics'][0]['message']}")
    assert len(diags[1]["params"]["diagnostics"]) == 0, "Expected 0 diagnostics after fix"
    print("  ✓ Fixed code cleared diagnostics")

    print("\n=== Test 2: Type-Aware & Module Completion ===")
    source_with_types = """#comprise "tests/corpus/modules/mod1.rook" as m1;

sum Shape {
    Circle { r: float; };
    Point;
}

struct Rect {
    w: int;
    h: int;
}

impl Rect {
    int area(self) {
        return self.w * self.h;
    }
}

int main() {
    Rect r;
    r.
    m1.
    Shape.
    return 0;
}
"""
    msgs = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": test_file_uri,
                    "languageId": "rook",
                    "version": 1,
                    "text": source_with_types
                }
            }
        },
        # Line 20: "    r." -> cursor at character 6
        {
            "jsonrpc": "2.0",
            "id": 10,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {"uri": test_file_uri},
                "position": {"line": 20, "character": 6}
            }
        },
        # Line 21: "    m1." -> cursor at character 7
        {
            "jsonrpc": "2.0",
            "id": 11,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {"uri": test_file_uri},
                "position": {"line": 21, "character": 7}
            }
        },
        # Line 22: "    Shape." -> cursor at character 10
        {
            "jsonrpc": "2.0",
            "id": 12,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {"uri": test_file_uri},
                "position": {"line": 22, "character": 10}
            }
        }
    ]
    resps = run_lsp_session(msgs)

    # Check r. completions (fields w, h, and method area)
    comp_r = next(r for r in resps if r.get("id") == 10)
    labels_r = [item["label"] for item in comp_r["result"]["items"]]
    assert "w" in labels_r and "h" in labels_r, f"Missing struct fields in r.: {labels_r}"
    assert "area" in labels_r, f"Missing method area in r.: {labels_r}"
    print(f"  ✓ Struct dot-completion: {labels_r}")

    # Check m1. completions (calc from mod1.rook)
    comp_m1 = next(r for r in resps if r.get("id") == 11)
    labels_m1 = [item["label"] for item in comp_m1["result"]["items"]]
    assert "calc" in labels_m1, f"Missing module function calc in m1.: {labels_m1}"
    print(f"  ✓ Module dot-completion: {labels_m1}")

    # Check Shape. completions (Circle, Point)
    comp_shape = next(r for r in resps if r.get("id") == 12)
    labels_shape = [item["label"] for item in comp_shape["result"]["items"]]
    assert "Circle" in labels_shape and "Point" in labels_shape, f"Missing sum variants in Shape.: {labels_shape}"
    print(f"  ✓ Enum/Sum dot-completion: {labels_shape}")

    print("\n=== Test 3: Hover & Definition ===")
    hover_source = """int add(int a, int b) {
    return a + b;
}

int main() {
    int total = add(10, 20);
    return 0;
}
"""
    msgs = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": test_file_uri,
                    "languageId": "rook",
                    "version": 1,
                    "text": hover_source
                }
            }
        },
        # Hover over "add" call at line 5, col 17
        {
            "jsonrpc": "2.0",
            "id": 20,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": test_file_uri},
                "position": {"line": 5, "character": 17}
            }
        },
        # Definition of "add" at line 5, col 17
        {
            "jsonrpc": "2.0",
            "id": 21,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": test_file_uri},
                "position": {"line": 5, "character": 17}
            }
        },
        # Document symbols
        {
            "jsonrpc": "2.0",
            "id": 22,
            "method": "textDocument/documentSymbol",
            "params": {
                "textDocument": {"uri": test_file_uri}
            }
        },
        # Document formatting
        {
            "jsonrpc": "2.0",
            "id": 23,
            "method": "textDocument/formatting",
            "params": {
                "textDocument": {"uri": test_file_uri}
            }
        }
    ]
    resps = run_lsp_session(msgs)

    hover_resp = next(r for r in resps if r.get("id") == 20)
    assert hover_resp["result"] is not None and "contents" in hover_resp["result"]
    print(f"  ✓ Hover markdown content:\n    {hover_resp['result']['contents']['value'].replace(chr(10), ' ')}")

    def_resp = next(r for r in resps if r.get("id") == 21)
    assert def_resp["result"] is not None and "range" in def_resp["result"]
    assert def_resp["result"]["range"]["start"]["line"] == 0, f"Expected definition on line 0, got {def_resp['result']}"
    print(f"  ✓ Goto Definition located function on line {def_resp['result']['range']['start']['line']}")

    syms_resp = next(r for r in resps if r.get("id") == 22)
    sym_names = [s["name"] for s in syms_resp["result"]]
    assert "add" in sym_names and "main" in sym_names
    print(f"  ✓ Document symbols: {sym_names}")

    fmt_resp = next(r for r in resps if r.get("id") == 23)
    assert len(fmt_resp["result"]) > 0 and "newText" in fmt_resp["result"][0]
    print("  ✓ Document formatting produced valid text edits")

    print("\n=== Test 4: Semantic Tokens & Import Isolation ===")
    mod_source = """#comprise "tests/corpus/modules/mod1.rook" as m1;

int main() {
    int v = m1.calc(10);
    return 0;
}
"""
    msgs = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": test_file_uri,
                    "languageId": "rook",
                    "version": 1,
                    "text": mod_source
                }
            }
        },
        # Semantic tokens request
        {
            "jsonrpc": "2.0",
            "id": 30,
            "method": "textDocument/semanticTokens/full",
            "params": {
                "textDocument": {"uri": test_file_uri}
            }
        },
        # Formatting request on file with #comprise
        {
            "jsonrpc": "2.0",
            "id": 31,
            "method": "textDocument/formatting",
            "params": {
                "textDocument": {"uri": test_file_uri}
            }
        },
        # Document symbols on file with #comprise
        {
            "jsonrpc": "2.0",
            "id": 32,
            "method": "textDocument/documentSymbol",
            "params": {
                "textDocument": {"uri": test_file_uri}
            }
        },
        # Cross-file definition: m1.calc at line 3, col 16
        {
            "jsonrpc": "2.0",
            "id": 33,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": test_file_uri},
                "position": {"line": 3, "character": 16}
            }
        }
    ]
    resps = run_lsp_session(msgs)

    # 1. Verify semantic tokens
    sem_resp = next(r for r in resps if r.get("id") == 30)
    assert sem_resp["result"] is not None and "data" in sem_resp["result"]
    assert len(sem_resp["result"]["data"]) > 0
    print(f"  ✓ Semantic tokens returned {len(sem_resp['result']['data']) // 5} tokens")

    # 2. Verify formatting does NOT leak imported module code
    fmt_resp2 = next(r for r in resps if r.get("id") == 31)
    new_text = fmt_resp2["result"][0]["newText"]
    # mod1.rook contains "calc" definition. The active document should NOT have "int calc(" in it!
    assert "calc" not in new_text or "int calc(" not in new_text, "Formatting leaked imported items!"
    print("  ✓ Document formatting cleanly isolated to active document")

    # 3. Verify document symbols only show local items ('main'), not imported ('calc')
    syms_resp2 = next(r for r in resps if r.get("id") == 32)
    local_syms = [s["name"] for s in syms_resp2["result"]]
    assert "main" in local_syms, f"Expected 'main' in local symbols: {local_syms}"
    assert "calc" not in local_syms, f"Imported 'calc' leaked into document symbols: {local_syms}"
    print(f"  ✓ Document symbols cleanly isolated to active document: {local_syms}")

    # 4. Verify cross-file definition navigates to mod1.rook URI
    def_resp2 = next(r for r in resps if r.get("id") == 33)
    assert def_resp2["result"] is not None, "Missing cross-file definition"
    assert "mod1.rook" in def_resp2["result"]["uri"], f"Expected URI to point to mod1.rook, got: {def_resp2['result']['uri']}"
    print(f"  ✓ Cross-file definition points to external module: {def_resp2['result']['uri']}")

    print("\n=== Test 5: Directive Go-to-Definition (#comprise & #include) ===")
    directive_source = """#comprise <std/io>
#include <stdio.h>

int main() {
    return 0;
}
"""
    msgs = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": test_file_uri,
                    "languageId": "rook",
                    "version": 1,
                    "text": directive_source
                }
            }
        },
        # Definition on #comprise <std/io> (line 0, col 12)
        {
            "jsonrpc": "2.0",
            "id": 40,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": test_file_uri},
                "position": {"line": 0, "character": 12}
            }
        },
        # Definition on #include <stdio.h> (line 1, col 12)
        {
            "jsonrpc": "2.0",
            "id": 41,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": test_file_uri},
                "position": {"line": 1, "character": 12}
            }
        }
    ]
    resps = run_lsp_session(msgs)

    # Check #comprise <std/io> definition
    def_comprise = next(r for r in resps if r.get("id") == 40)
    assert def_comprise.get("result") is not None, f"Expected definition for #comprise <std/io>, got {def_comprise}"
    assert "std/io.rook" in def_comprise["result"]["uri"], f"Expected uri to contain std/io.rook, got: {def_comprise['result']['uri']}"
    print(f"  ✓ Directive #comprise <std/io> resolved to: {def_comprise['result']['uri']}")

    # Check #include <stdio.h> definition
    def_include = next(r for r in resps if r.get("id") == 41)
    assert def_include.get("result") is not None, f"Expected definition for #include <stdio.h>, got {def_include}"
    assert "stdio.h" in def_include["result"]["uri"], f"Expected uri to contain stdio.h, got: {def_include['result']['uri']}"
    print(f"  ✓ Directive #include <stdio.h> resolved to: {def_include['result']['uri']}")

    print("\n=== Test 6: Cross-File Std & C Symbol Definition ===")
    symbol_source = """#comprise <std/io>
#include <stdio.h>

int main() {
    println("Hello Rook!");
    printf("Hello C!\\n");
    return 0;
}
"""
    msgs = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": test_file_uri,
                    "languageId": "rook",
                    "version": 1,
                    "text": symbol_source
                }
            }
        },
        # Definition on println (line 4, col 6)
        {
            "jsonrpc": "2.0",
            "id": 50,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": test_file_uri},
                "position": {"line": 4, "character": 6}
            }
        },
        # Definition on printf (line 5, col 6)
        {
            "jsonrpc": "2.0",
            "id": 51,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": test_file_uri},
                "position": {"line": 5, "character": 6}
            }
        }
    ]
    resps = run_lsp_session(msgs)

    # Check println definition -> std/io.rook
    def_println = next(r for r in resps if r.get("id") == 50)
    assert def_println.get("result") is not None, f"Expected definition for println, got {def_println}"
    assert "std/io.rook" in def_println["result"]["uri"], f"Expected println in std/io.rook, got {def_println['result']['uri']}"
    print(f"  ✓ Std symbol 'println' resolved to: {def_println['result']['uri']} (line {def_println['result']['range']['start']['line']})")

    # Check printf definition -> stdio.h
    def_printf = next(r for r in resps if r.get("id") == 51)
    assert def_printf.get("result") is not None, f"Expected definition for printf, got {def_printf}"
    assert "stdio.h" in def_printf["result"]["uri"], f"Expected printf in stdio.h, got {def_printf['result']['uri']}"
    print(f"  ✓ C libc symbol 'printf' resolved to: {def_printf['result']['uri']}")

    print("\n=== Test 7: Supercharged Autocompletion & Keyword/Snippet Helper ===")
    helper_source = """int main() {
    int my_counter = 42;
    my_
    return 0;
}
"""
    msgs = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": test_file_uri,
                    "languageId": "rook",
                    "version": 1,
                    "text": helper_source
                }
            }
        },
        # Completion at my_ (line 2, col 7)
        {
            "jsonrpc": "2.0",
            "id": 60,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {"uri": test_file_uri},
                "position": {"line": 2, "character": 7}
            }
        },
        # Completion on empty line with '#' prefix
        {
            "jsonrpc": "2.0",
            "id": 61,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {"uri": test_file_uri},
                "position": {"line": 0, "character": 0}
            }
        }
    ]
    resps = run_lsp_session(msgs)

    # 1. Local variable completion
    comp_local = next(r for r in resps if r.get("id") == 60)
    items_local = comp_local["result"]["items"]
    labels_local = [it["label"] for it in items_local]
    assert "my_counter" in labels_local, f"Expected my_counter in local completions, got: {labels_local[:15]}"
    print(f"  ✓ Local variable completion found: 'my_counter'")

    # 2. Check keywords and snippets in completions
    assert "func" in labels_local, f"Expected 'func' snippet in completions: {labels_local[:15]}"
    assert "struct" in labels_local, f"Expected 'struct' keyword/snippet in completions"
    assert "sum" in labels_local, f"Expected 'sum' keyword/snippet in completions"
    assert "impl" in labels_local, f"Expected 'impl' keyword/snippet in completions"
    assert "while" in labels_local, f"Expected 'while' keyword/snippet in completions"
    assert "for" in labels_local, f"Expected 'for' keyword/snippet in completions"
    assert "match" in labels_local, f"Expected 'match' keyword/snippet in completions"
    print(f"  ✓ Keywords & snippets present (func, struct, sum, impl, while, for, match)")

    # 3. CRITICAL: Ensure 'fn' is NOT present in any form
    for it in items_local:
        lbl = it.get("label", "")
        ins = it.get("insertText", "")
        filt = it.get("filterText", "")
        assert lbl != "fn", "CRITICAL ERROR: 'fn' found as completion label!"
        assert ins != "fn" and not ins.startswith("fn "), "CRITICAL ERROR: 'fn' found in insertText!"
        assert filt != "fn", "CRITICAL ERROR: 'fn' found as filterText!"
    print("  ✓ CRITICAL VERIFICATION: 'fn' is strictly absent from all completion items")

    # 4. Directive completions
    comp_top = next(r for r in resps if r.get("id") == 61)
    labels_top = [it["label"] for it in comp_top["result"]["items"]]
    assert "#comprise" in labels_top, f"Expected #comprise directive completion: {labels_top[:15]}"
    assert "#include" in labels_top, f"Expected #include directive completion: {labels_top[:15]}"
    print("  ✓ Preprocessor directive completions present (#comprise, #include, etc.)")

    print("\nALL LSP TESTS PASSED SUCCESSFULLY!")

if __name__ == "__main__":
    main()

