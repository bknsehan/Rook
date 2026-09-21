#!/usr/bin/env bash
set -e

# Guard against running with sudo
if [ "${EUID:-$(id -u)}" -eq 0 ] || [ -n "${SUDO_USER}" ]; then
    echo "================================================================="
    echo " [NOTICE] Please DO NOT run install.sh with sudo!"
    echo ""
    echo " Rook installs directly into your user environment:"
    echo "   Prefix:   \${HOME}/bin/Rook"
    echo "   Symlinks: \${HOME}/bin/rokade"
    echo "   Editor:   VS Code, VSCodium, Code - OSS, Zed"
    echo ""
    echo " Simple user install works without any root permissions."
    echo " Please re-run simply as:"
    echo "   ./install.sh"
    echo "================================================================="
    exit 1
fi

# Default installation paths
DEFAULT_PREFIX="${HOME}/bin/Rook"
PREFIX="${DEFAULT_PREFIX}"
INSTALL_EXT="auto"
CHOSEN_EDITOR=""
CHOSEN_FLAVOR=""
CREATE_SYMLINKS=1

print_help() {
    echo "Rook Toolchain Installer"
    echo "Usage: ./install.sh [options]"
    echo ""
    echo "Options:"
    echo "  --prefix=<dir>            Installation directory (default: ${DEFAULT_PREFIX})"
    echo "  --with-extension          Install editor extension(s)"
    echo "  --no-extension            Do not install any editor extension"
    echo "  --editor=<name>           Editor to target: zed, vscode, or all"
    echo "  --vscode-flavor=<flavor>  VS Code variant: vscode, vscodium, code-oss, or all"
    echo "  --with-zed                Install Rook Zed editor extension (legacy alias)"
    echo "  --no-zed                  Do not install Zed editor extension (legacy alias)"
    echo "  --no-symlinks             Do not create symlinks in ~/bin or ~/.local/bin"
    echo "  -h, --help                Show this help message"
    exit 0
}

# Parse options
for arg in "$@"; do
    case "$arg" in
        --prefix=*)
            PREFIX="${arg#*=}"
            ;;
        --with-extension)
            INSTALL_EXT="yes"
            ;;
        --no-extension)
            INSTALL_EXT="no"
            ;;
        --editor=*)
            CHOSEN_EDITOR="${arg#*=}"
            INSTALL_EXT="yes"
            ;;
        --vscode-flavor=*)
            CHOSEN_FLAVOR="${arg#*=}"
            INSTALL_EXT="yes"
            ;;
        --with-zed)
            INSTALL_EXT="yes"
            CHOSEN_EDITOR="zed"
            ;;
        --no-zed)
            INSTALL_EXT="no"
            ;;
        --no-symlinks)
            CREATE_SYMLINKS=0
            ;;
        -h|--help)
            print_help
            ;;
        *)
            echo "Unknown option: $arg"
            print_help
            ;;
    esac
done

echo "=========================================="
echo "       Rook Toolchain Installer           "
echo "=========================================="
echo "Target Prefix: ${PREFIX}"

# 1. Check prerequisites
echo -n "Checking tools... "
if ! command -v cmake >/dev/null 2>&1; then
    echo "error: cmake is required but not installed." >&2
    exit 1
fi
if ! command -v gcc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
    echo "error: a C compiler (gcc or clang) is required." >&2
    exit 1
fi
echo "OK"

# 2. Build binaries
echo "Building rokade and rook-lsp..."
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc 2>/dev/null || echo 4)"

# 3. Create destination directory structure
echo "Installing to ${PREFIX}..."
mkdir -p "${PREFIX}/bin"
mkdir -p "${PREFIX}/std"
mkdir -p "${PREFIX}/share/rokade"
mkdir -p "${PREFIX}/editors/zed"
mkdir -p "${PREFIX}/editors/vscode"

# 4. Copy executables & assets
cp -f build/rokade "${PREFIX}/bin/rokade"
chmod +x "${PREFIX}/bin/rokade"

if [ -f "build/rook-lsp" ]; then
    cp -f build/rook-lsp "${PREFIX}/bin/rook-lsp"
    chmod +x "${PREFIX}/bin/rook-lsp"
fi

cp -rf std/* "${PREFIX}/std/"
cp -f src/libc/commandlist.json "${PREFIX}/share/rokade/commandlist.json"
cp -rf editors/zed/* "${PREFIX}/editors/zed/"
if [ -d "editors/vscode" ]; then
    cp -rf editors/vscode/* "${PREFIX}/editors/vscode/"
fi

echo "Installed:"
echo "  - Compiler:        ${PREFIX}/bin/rokade"
echo "  - Language Server: ${PREFIX}/bin/rook-lsp"
echo "  - Standard Lib:    ${PREFIX}/std"
echo "  - Data Files:      ${PREFIX}/share/rokade/commandlist.json"
echo "  - Zed Extension:   ${PREFIX}/editors/zed"
echo "  - VS Code Ext:     ${PREFIX}/editors/vscode"

# 5. Create symlinks in PATH
if [ "${CREATE_SYMLINKS}" -eq 1 ]; then
    for bindir in "${HOME}/bin" "${HOME}/.local/bin"; do
        if [ -d "${bindir}" ] && [[ ":$PATH:" == *":${bindir}:"* ]]; then
            echo "Creating symlinks in ${bindir}..."
            ln -sf "${PREFIX}/bin/rokade" "${bindir}/rokade"
            if [ -f "${PREFIX}/bin/rook-lsp" ]; then
                ln -sf "${PREFIX}/bin/rook-lsp" "${bindir}/rook-lsp"
            fi
            break
        fi
    done
fi

# 6. Editor Extensions Detection & Installation
DO_INSTALL_EXT=0

if [ "${INSTALL_EXT}" = "yes" ]; then
    DO_INSTALL_EXT=1
elif [ "${INSTALL_EXT}" = "auto" ]; then
    if [ -t 0 ]; then
        echo ""
        read -r -p "Do you want to install editor extensions (Zed / VS Code)? [Y/n] " response
        case "$response" in
            [nN][oO]|[nN]) DO_INSTALL_EXT=0 ;;
            *) DO_INSTALL_EXT=1 ;;
        esac
    else
        DO_INSTALL_EXT=1
    fi
fi

if [ "${DO_INSTALL_EXT}" -eq 1 ]; then
    TARGET_ZED=0
    TARGET_VSCODE=0

    if [ -n "${CHOSEN_EDITOR}" ]; then
        case "${CHOSEN_EDITOR}" in
            zed) TARGET_ZED=1 ;;
            vscode) TARGET_VSCODE=1 ;;
            all|both) TARGET_ZED=1; TARGET_VSCODE=1 ;;
        esac
    elif [ -t 0 ]; then
        echo ""
        echo "Which editor extension would you like to install?"
        echo "  1) Zed"
        echo "  2) VS Code (or derivatives: VSCodium, Code - OSS)"
        echo "  3) Both"
        read -r -p "Enter choice [1-3] (default: 3): " ed_choice
        case "$ed_choice" in
            1) TARGET_ZED=1 ;;
            2) TARGET_VSCODE=1 ;;
            *) TARGET_ZED=1; TARGET_VSCODE=1 ;;
        esac
    else
        TARGET_ZED=1
        TARGET_VSCODE=1
    fi

    # 6a. Install Zed Extension
    if [ "${TARGET_ZED}" -eq 1 ]; then
        ZED_EXT_DIR="${HOME}/.local/share/zed/extensions/installed/rook"
        echo ""
        echo ">>> Installing Rook Zed extension to ${ZED_EXT_DIR}..."
        mkdir -p "${HOME}/.local/share/zed/extensions/installed"
        rm -rf "${ZED_EXT_DIR}"
        cp -rf "${PREFIX}/editors/zed" "${ZED_EXT_DIR}"

        # Rebuild extension.wasm from the current Rust source when possible.
        # GUI Zed loads extension.wasm, so a stale prebuilt blob would ignore
        # lib.rs fixes (PATH lookup, rokade fallback).
        if command -v cargo >/dev/null 2>&1; then
            echo "Rebuilding Zed extension.wasm..."
            if rustup target list --installed 2>/dev/null | grep -q "wasm32-wasip1"; then
                (cd "${ZED_EXT_DIR}" && cargo build --release --target wasm32-wasip1 2>/dev/null && cp -f target/wasm32-wasip1/release/*.wasm extension.wasm 2>/dev/null) || echo "warning: wasm rebuild failed, keeping prebuilt extension.wasm"
            elif rustup target list --installed 2>/dev/null | grep -q "wasm32-wasip2"; then
                (cd "${ZED_EXT_DIR}" && cargo build --release --target wasm32-wasip2 2>/dev/null && cp -f target/wasm32-wasip2/release/*.wasm extension.wasm 2>/dev/null) || echo "warning: wasm rebuild failed, keeping prebuilt extension.wasm"
            else
                echo "warning: no wasm32-wasip target installed (run: rustup target add wasm32-wasip1); keeping prebuilt extension.wasm"
            fi
        fi

        # Self-heal a broken tree-sitter grammar checkout.
        # Zed clones tree-sitter-c into grammars/c/ on dev-install and runs
        # `git checkout <rev>` there. An interrupted earlier run leaves a
        # shallow repo with no commits/tags, and every later install fails
        # with "pathspec 'vX.Y.Z' did not match any file(s) known to git".
        # Drop such a broken checkout so Zed re-clones fresh (needs network
        # on first install only).
        GRAMMAR_REV="$(sed -n '/^\[grammars\.c\]/,/^\[/p' "${ZED_EXT_DIR}/extension.toml" | sed -n 's/^rev *= * "\(.*\)" */\1/p' | head -n 1)"
        if [ -d "${ZED_EXT_DIR}/grammars/c" ] && [ -n "${GRAMMAR_REV}" ]; then
            if ! git -C "${ZED_EXT_DIR}/grammars/c" rev-parse --verify -q "${GRAMMAR_REV}^{commit}" >/dev/null 2>&1; then
                echo "Removing broken grammars/c checkout (Zed will re-clone it)..."
                rm -rf "${ZED_EXT_DIR}/grammars/c"
            fi
        fi

        if command -v python3 >/dev/null 2>&1; then
            python3 -c '
import json, os
index_path = os.path.expanduser("~/.local/share/zed/extensions/index.json")
if os.path.exists(index_path):
    try:
        with open(index_path, "r") as f:
            d = json.load(f)
        d.setdefault("extensions", {})["rook"] = {
            "manifest": {
                "id": "rook",
                "name": "Rook",
                "version": "0.1.0",
                "schema_version": 1,
                "description": "Rook programming language support for Zed",
                "repository": "https://github.com/bknsehan/Rook",
                "authors": ["bknsehan"],
                "lib": {"kind": "Rust", "version": "0.7.0"},
                "themes": [],
                "icon_themes": [],
                "languages": ["languages/rook"],
                "grammars": {
                    "c": {
                        "repository": "https://github.com/tree-sitter/tree-sitter-c",
                        "rev": "3efee11f784605d44623d7dadd6cd12a0f73ea92",
                        "path": None
                    }
                },
                "language_servers": {
                    "rook-lsp": {
                        "language": "Rook",
                        "languages": ["Rook"],
                        "language_ids": {},
                        "code_action_kinds": None
                    }
                },
                "context_servers": {},
                "slash_commands": {},
                "snippets": None,
                "capabilities": []
            },
            "dev": False
        }
        with open(index_path, "w") as f:
            json.dump(d, f, indent=2)
    except Exception:
        pass
'
        fi
        echo "✓ Zed extension installed successfully!"
    fi

    # 6b. Install VS Code / VSCodium / Code - OSS Extension
    if [ "${TARGET_VSCODE}" -eq 1 ]; then
        TARGET_CODE=0
        TARGET_CODIUM=0
        TARGET_CODE_OSS=0

        if [ -n "${CHOSEN_FLAVOR}" ]; then
            case "${CHOSEN_FLAVOR}" in
                vscode) TARGET_CODE=1 ;;
                vscodium|codium) TARGET_CODIUM=1 ;;
                code-oss|oss) TARGET_CODE_OSS=1 ;;
                all) TARGET_CODE=1; TARGET_CODIUM=1; TARGET_CODE_OSS=1 ;;
            esac
        elif [ -t 0 ]; then
            echo ""
            echo "Which VS Code variant do you want to target?"
            echo "  1) VS Code (Official)"
            echo "  2) VSCodium"
            echo "  3) Code - OSS"
            echo "  4) All detected variants"
            read -r -p "Enter choice [1-4] (default: 4): " flavor_choice
            case "$flavor_choice" in
                1) TARGET_CODE=1 ;;
                2) TARGET_CODIUM=1 ;;
                3) TARGET_CODE_OSS=1 ;;
                *) TARGET_CODE=1; TARGET_CODIUM=1; TARGET_CODE_OSS=1 ;;
            esac
        else
            TARGET_CODE=1
            TARGET_CODIUM=1
            TARGET_CODE_OSS=1
        fi

        VS_EXT_SRC="${PREFIX}/editors/vscode"

        # Install for Official VS Code
        if [ "${TARGET_CODE}" -eq 1 ]; then
            CODE_EXT_DIR="${HOME}/.vscode/extensions/rook-lang-0.7.1"
            echo ">>> Installing Rook extension for VS Code to ${CODE_EXT_DIR}..."
            mkdir -p "${HOME}/.vscode/extensions"
            rm -rf "${CODE_EXT_DIR}"
            cp -rf "${VS_EXT_SRC}" "${CODE_EXT_DIR}"
            if command -v code >/dev/null 2>&1; then
                code --install-extension "${VS_EXT_SRC}" >/dev/null 2>&1 || true
            fi
            echo "✓ VS Code extension installed!"
        fi

        # Install for VSCodium
        if [ "${TARGET_CODIUM}" -eq 1 ]; then
            echo ">>> Installing Rook extension for VSCodium..."
            CODIUM_DIR="${HOME}/.vscode-oss/extensions/rook-lang-0.7.1"
            mkdir -p "${HOME}/.vscode-oss/extensions"
            rm -rf "${CODIUM_DIR}"
            cp -rf "${VS_EXT_SRC}" "${CODIUM_DIR}"
            if [ -d "${HOME}/.vscodium" ] || [ -d "${HOME}/.config/VSCodium" ]; then
                mkdir -p "${HOME}/.vscodium/extensions"
                rm -rf "${HOME}/.vscodium/extensions/rook-lang-0.7.1"
                cp -rf "${VS_EXT_SRC}" "${HOME}/.vscodium/extensions/rook-lang-0.7.1"
            fi
            if command -v codium >/dev/null 2>&1; then
                codium --install-extension "${VS_EXT_SRC}" >/dev/null 2>&1 || true
            fi
            echo "✓ VSCodium extension installed!"
        fi

        # Install for Code - OSS
        if [ "${TARGET_CODE_OSS}" -eq 1 ]; then
            OSS_DIR="${HOME}/.vscode-oss/extensions/rook-lang-0.7.1"
            if [ "${TARGET_CODIUM}" -ne 1 ]; then
                echo ">>> Installing Rook extension for Code - OSS to ${OSS_DIR}..."
                mkdir -p "${HOME}/.vscode-oss/extensions"
                rm -rf "${OSS_DIR}"
                cp -rf "${VS_EXT_SRC}" "${OSS_DIR}"
            fi
            if command -v code-oss >/dev/null 2>&1; then
                code-oss --install-extension "${VS_EXT_SRC}" >/dev/null 2>&1 || true
            fi
            echo "✓ Code - OSS extension installed!"
        fi
    fi
fi

echo ""
echo "Running environment check with installed rokade..."
"${PREFIX}/bin/rokade" doctor

echo ""
echo "=========================================="
echo "    Rook installation completed!          "
echo "=========================================="
echo "You can now run 'rokade' and 'rook-lsp'."
