#!/usr/bin/env bash
set -e

# Default installation paths
DEFAULT_PREFIX="${HOME}/bin/Rook"
PREFIX="${DEFAULT_PREFIX}"
INSTALL_ZED="auto"
CREATE_SYMLINKS=1

print_help() {
    echo "Rook Toolchain Installer"
    echo "Usage: ./install.sh [options]"
    echo ""
    echo "Options:"
    echo "  --prefix=<dir>    Installation directory (default: ${DEFAULT_PREFIX})"
    echo "  --with-zed        Install Rook Zed editor extension"
    echo "  --no-zed          Do not install Zed editor extension"
    echo "  --no-symlinks     Do not create symlinks in ~/bin or ~/.local/bin"
    echo "  -h, --help        Show this help message"
    exit 0
}

# Parse options
for arg in "$@"; do
    case "$arg" in
        --prefix=*)
            PREFIX="${arg#*=}"
            ;;
        --with-zed)
            INSTALL_ZED="yes"
            ;;
        --no-zed)
            INSTALL_ZED="no"
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
if ! command -v cargo >/dev/null 2>&1; then
    echo "error: cargo is required for building rook-lsp." >&2
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
mkdir -p "${PREFIX}/share/rook"
mkdir -p "${PREFIX}/editors/zed"

# 4. Copy executables & assets
cp -f build/rokade "${PREFIX}/bin/rokade"
chmod +x "${PREFIX}/bin/rokade"

if [ -f "build/rook-lsp" ]; then
    cp -f build/rook-lsp "${PREFIX}/bin/rook-lsp"
    chmod +x "${PREFIX}/bin/rook-lsp"
fi

cp -rf std/* "${PREFIX}/std/"
cp -f src/libc/commandlist.json "${PREFIX}/share/rook/commandlist.json"
cp -rf editors/zed/* "${PREFIX}/editors/zed/"

echo "Installed:"
echo "  - Compiler:        ${PREFIX}/bin/rokade"
echo "  - Language Server: ${PREFIX}/bin/rook-lsp"
echo "  - Standard Lib:    ${PREFIX}/std"
echo "  - Data Files:      ${PREFIX}/share/rook/commandlist.json"
echo "  - Zed Extension:   ${PREFIX}/editors/zed"

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

# 6. Zed Editor Detection & Extension Installation
ZED_FOUND=0
ZED_PATH=""
if command -v zeditor >/dev/null 2>&1; then
    ZED_FOUND=1
    ZED_PATH="$(command -v zeditor)"
elif command -v zed >/dev/null 2>&1; then
    ZED_FOUND=1
    ZED_PATH="$(command -v zed)"
elif [ -d "${HOME}/.config/zed" ]; then
    ZED_FOUND=1
    ZED_PATH="${HOME}/.config/zed"
fi

if [ "${ZED_FOUND}" -eq 1 ]; then
    echo ""
    echo ">>> Zed Editor detected: ${ZED_PATH}"
    
    DO_INSTALL_ZED=0
    if [ "${INSTALL_ZED}" = "yes" ]; then
        DO_INSTALL_ZED=1
    elif [ "${INSTALL_ZED}" = "auto" ]; then
        if [ -t 0 ]; then
            read -r -p "Install Rook extension for Zed? [Y/n] " response
            case "$response" in
                [nN][oO]|[nN]) DO_INSTALL_ZED=0 ;;
                *) DO_INSTALL_ZED=1 ;;
            esac
        else
            DO_INSTALL_ZED=1
        fi
    fi

    if [ "${DO_INSTALL_ZED}" -eq 1 ]; then
        ZED_EXT_DIR="${HOME}/.local/share/zed/extensions/installed/rook"
        echo "Installing Rook Zed extension to ${ZED_EXT_DIR}..."
        mkdir -p "${HOME}/.local/share/zed/extensions/installed"
        rm -rf "${ZED_EXT_DIR}"
        ln -sf "${PREFIX}/editors/zed" "${ZED_EXT_DIR}"

        # Configure ~/.config/zed/settings.json
        ZED_SETTINGS="${HOME}/.config/zed/settings.json"
        if [ -f "${ZED_SETTINGS}" ]; then
            if ! grep -q "rook-lsp" "${ZED_SETTINGS}"; then
                echo "Note: To link rook-lsp, add to your ${ZED_SETTINGS}:"
                echo '  "lsp": { "rook-lsp": { "binary": { "path": "'"${PREFIX}"'/bin/rook-lsp" } } },'
                echo '  "languages": { "Rook": { "language_servers": ["rook-lsp"] } }'
            fi
        fi
        echo "Zed extension successfully installed!"
    else
        echo "Skipping Zed extension installation."
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
