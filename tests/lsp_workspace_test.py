import subprocess
import json
import sys
import os
import shutil
import tempfile

def run_lsp_session(messages, binary_path="./build/rook-lsp"):
    proc = subprocess.Popen(
        [binary_path],
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

    stdout_data, stderr_data = proc.communicate(input=full_payload, timeout=10)

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
    print("=== LSP Workspace Indexing & C Engine Comprehensive Test ===")
    
    # 1. Create temporary mock multi-directory project
    project_dir = tempfile.mkdtemp(prefix="rook_test_proj_")
    try:
        src_dir = os.path.join(project_dir, "src")
        net_dir = os.path.join(src_dir, "net")
        sub_dir = os.path.join(net_dir, "sub")
        vendor_dir = os.path.join(project_dir, "vendor")
        os.makedirs(sub_dir, exist_ok=True)
        os.makedirs(vendor_dir, exist_ok=True)

        # rokade.toml
        with open(os.path.join(project_dir, "rokade.toml"), "w") as f:
            f.write('[package]\nname = "test_proj"\nversion = "0.1.0"\n\n[build]\ninclude-dirs = ["vendor"]\n')

        # src/config.rook
        config_path = os.path.join(src_dir, "config.rook")
        with open(config_path, "w") as f:
            f.write("""#comprise <std/str>

struct AppConfig {
    port: int;
    host: Str;
};

int get_default_port() {
    return 8080;
}
""")

        # vendor/stb.rook
        stb_path = os.path.join(vendor_dir, "stb.rook")
        with open(stb_path, "w") as f:
            f.write("""int stb_image_load(const char* path) {
    return 1;
}
""")

        # src/net/helper.rook
        helper_path = os.path.join(net_dir, "helper.rook")
        with open(helper_path, "w") as f:
            f.write("""int net_helper_ping() {
    return 1;
}
""")

        # src/net/sub/worker.rook (deeply nested file testing multi-path resolution)
        worker_path = os.path.join(sub_dir, "worker.rook")
        worker_content = """#comprise "src/config.rook"
#comprise "config.rook"
#comprise "helper.rook"
#comprise "vendor/stb.rook"
#comprise <std/io>
#include <stdio.h>

void run_worker() {
    printf("Worker starting...\\n");
    println("Worker running!");
    int p = get_default_port();
    int h = net_helper_ping();
    int s = stb_image_load("test.png");
}
"""
        with open(worker_path, "w") as f:
            f.write(worker_content)

        root_uri = f"file://{os.path.abspath(project_dir)}"
        worker_uri = f"file://{os.path.abspath(worker_path)}"

        # Prepare LSP requests
        msgs = [
            # 1. Initialize with rootUri
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {
                    "rootUri": root_uri,
                    "rootPath": os.path.abspath(project_dir),
                    "capabilities": {}
                }
            },
            {"jsonrpc": "2.0", "method": "initialized", "params": {}},
            # 2. Open worker.rook
            {
                "jsonrpc": "2.0",
                "method": "textDocument/didOpen",
                "params": {
                    "textDocument": {
                        "uri": worker_uri,
                        "languageId": "rook",
                        "version": 1,
                        "text": worker_content
                    }
                }
            },
            # 3. Workspace symbol search: "AppConfig"
            {
                "jsonrpc": "2.0",
                "id": 2,
                "method": "workspace/symbol",
                "params": {"query": "AppConfig"}
            },
            # 4. Workspace symbol search: "net_helper"
            {
                "jsonrpc": "2.0",
                "id": 3,
                "method": "workspace/symbol",
                "params": {"query": "net_helper"}
            },
            # 5. Completion inside worker.rook
            {
                "jsonrpc": "2.0",
                "id": 4,
                "method": "textDocument/completion",
                "params": {
                    "textDocument": {"uri": worker_uri},
                    "position": {"line": 10, "character": 12}
                }
            },
            # 6. Definition on get_default_port (line 10, char 15)
            {
                "jsonrpc": "2.0",
                "id": 5,
                "method": "textDocument/definition",
                "params": {
                    "textDocument": {"uri": worker_uri},
                    "position": {"line": 10, "character": 15}
                }
            },
            # 7. Definition on net_helper_ping (line 11, char 15)
            {
                "jsonrpc": "2.0",
                "id": 6,
                "method": "textDocument/definition",
                "params": {
                    "textDocument": {"uri": worker_uri},
                    "position": {"line": 11, "character": 15}
                }
            },
            # 8. Definition on printf (line 8, char 6)
            {
                "jsonrpc": "2.0",
                "id": 7,
                "method": "textDocument/definition",
                "params": {
                    "textDocument": {"uri": worker_uri},
                    "position": {"line": 8, "character": 6}
                }
            },
            # 9. Hover on printf (line 8, char 6)
            {
                "jsonrpc": "2.0",
                "id": 8,
                "method": "textDocument/hover",
                "params": {
                    "textDocument": {"uri": worker_uri},
                    "position": {"line": 8, "character": 6}
                }
            },
            # 10. Directive completion for #comprise <
            {
                "jsonrpc": "2.0",
                "id": 9,
                "method": "textDocument/completion",
                "params": {
                    "textDocument": {"uri": worker_uri},
                    "position": {"line": 4, "character": 11}
                }
            },
            {"jsonrpc": "2.0", "id": 10, "method": "shutdown", "params": {}},
            {"jsonrpc": "2.0", "method": "exit", "params": {}}
        ]

        responses = run_lsp_session(msgs)

        # Verification 1: Initialize
        init_res = next((r for r in responses if r.get("id") == 1), None)
        assert init_res, "Missing response for initialize"
        caps = init_res["result"]["capabilities"]
        assert caps.get("workspaceSymbolProvider") is True, "workspaceSymbolProvider must be advertised"
        print("  ✓ Initialize successful: workspaceSymbolProvider advertised")

        # Verification 2: Diagnostics on worker.rook (should have 0 errors, proving all comprises and includes resolved!)
        diag_msg = next((r for r in responses if r.get("method") == "textDocument/publishDiagnostics"), None)
        assert diag_msg, "Missing publishDiagnostics notification"
        diags = diag_msg["params"]["diagnostics"]
        errs = [d for d in diags if d["severity"] == 1]
        if errs:
            print("  ✗ Unexpected errors in worker.rook:", [e["message"] for e in errs])
            sys.exit(1)
        print("  ✓ Multi-directory path resolution: 0 errors (all comprises and includes resolved cleanly)")

        # Verification 3: Workspace symbol search: "AppConfig"
        ws_res1 = next((r for r in responses if r.get("id") == 2), None)
        assert ws_res1, "Missing workspace/symbol response for AppConfig"
        sym_names1 = [s["name"] for s in ws_res1["result"]]
        assert "AppConfig" in sym_names1, f"AppConfig not found in workspace symbols: {sym_names1}"
        print(f"  ✓ Workspace symbol search found AppConfig: {sym_names1}")

        # Verification 4: Workspace symbol search: "net_helper"
        ws_res2 = next((r for r in responses if r.get("id") == 3), None)
        assert ws_res2, "Missing workspace/symbol response for net_helper"
        sym_names2 = [s["name"] for s in ws_res2["result"]]
        assert "net_helper_ping" in sym_names2, f"net_helper_ping not found in workspace symbols: {sym_names2}"
        print(f"  ✓ Workspace symbol search found net_helper_ping: {sym_names2}")

        # Verification 5: Autocompletion
        comp_res = next((r for r in responses if r.get("id") == 4), None)
        assert comp_res, "Missing completion response"
        items = comp_res["result"]["items"]
        labels = [item["label"] for item in items]

        assert "AppConfig" in labels, "AppConfig missing from completion"
        assert "get_default_port" in labels, "get_default_port missing from completion"
        assert "net_helper_ping" in labels, "net_helper_ping missing from completion"
        assert "stb_image_load" in labels, "stb_image_load missing from completion"
        assert "println" in labels, "println missing from completion"
        assert "printf" in labels, "printf missing from completion"

        # Check detail of printf
        printf_item = next(it for it in items if it["label"] == "printf")
        assert "printf" in printf_item["detail"], "printf detail missing signature"
        print(f"  ✓ C function completion present: printf -> {printf_item['detail']}")

        # Strict keyword check: NO 'fn'
        assert "fn" not in labels, "CRITICAL ERROR: 'fn' found in completion labels!"
        print("  ✓ Strict language check: 'fn' is strictly absent from completions")

        # Verification 6: Definition on get_default_port -> points to config.rook
        def_res1 = next((r for r in responses if r.get("id") == 5), None)
        assert def_res1 and def_res1.get("result"), "Failed definition on get_default_port"
        def_uri1 = def_res1["result"]["uri"]
        assert "config.rook" in def_uri1, f"Expected config.rook, got {def_uri1}"
        print(f"  ✓ Go-to-Definition on get_default_port resolved to: {def_uri1}")

        # Verification 7: Definition on net_helper_ping -> points to helper.rook
        def_res2 = next((r for r in responses if r.get("id") == 6), None)
        assert def_res2 and def_res2.get("result"), "Failed definition on net_helper_ping"
        def_uri2 = def_res2["result"]["uri"]
        assert "helper.rook" in def_uri2, f"Expected helper.rook, got {def_uri2}"
        print(f"  ✓ Go-to-Definition on net_helper_ping resolved to: {def_uri2}")

        # Verification 8: Definition on printf -> points to stdio.h
        def_res3 = next((r for r in responses if r.get("id") == 7), None)
        assert def_res3 and def_res3.get("result"), "Failed definition on printf"
        def_uri3 = def_res3["result"]["uri"]
        assert "stdio.h" in def_uri3, f"Expected stdio.h in URI, got {def_uri3}"
        print(f"  ✓ Go-to-Definition on printf resolved to C header: {def_uri3}")

        # Verification 9: Hover on printf
        hov_res = next((r for r in responses if r.get("id") == 8), None)
        assert hov_res and hov_res.get("result"), "Failed hover on printf"
        hov_val = hov_res["result"]["contents"]["value"]
        assert "stdio.h" in hov_val, f"Hover did not mention stdio.h: {hov_val}"
        print(f"  ✓ Hover on printf shows C header: {hov_val.splitlines()[-1]}")

        # Verification 10: Comprise module completion
        mod_comp_res = next((r for r in responses if r.get("id") == 9), None)
        assert mod_comp_res, "Missing comprise module completion response"
        mod_items = mod_comp_res["result"]["items"]
        mod_labels = [it["label"] for it in mod_items]
        assert "std/io" in mod_labels, "std/io missing from comprise completion"
        assert "std/vec" in mod_labels, "std/vec missing from comprise completion"
        print(f"  ✓ Comprise module autocompletion: {mod_labels[:5]}... ({len(mod_labels)} modules found)")

    finally:
        shutil.rmtree(project_dir, ignore_errors=True)

    print("\nALL WORKSPACE AND C ENGINE TESTS PASSED!")

if __name__ == "__main__":
    main()
