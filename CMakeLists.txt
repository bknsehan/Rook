cmake_minimum_required(VERSION 3.16)
project(rokade C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(rokade
  src/main.c
  src/diag.c
  src/util.c
  src/ast.c
  src/lexer.c
  src/parse.c
  src/emit.c
  src/sema.c
  src/codegen.c
  src/backend.c
  src/c_backend.c
  src/config.c
  src/toolchain.c
)

# Sanitizer build (AddressSanitizer + UndefinedBehaviorSanitizer).
# Configure with: cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -USE_SANITIZER=ON
option(USE_SANITIZER "Build rokade with ASAN/UBSAN" OFF)
if(USE_SANITIZER)
  target_compile_options(rokade PRIVATE -fsanitize=address,undefined -g -fno-omit-frame-pointer)
  target_link_options(rokade PRIVATE -fsanitize=address,undefined)
endif()

# ─── Data: commandlist mirror + install (rokade is always built) ──
# Mirror src/libc/commandlist.json into the build tree so a build-tree
# `build/rokade` finds it via its exe-dir discovery (/proc/self/exe) from any
# cwd, without ROKADE_DATA_DIR. Install it to share/rokade for an installed
# rokade to find via <exe>/../share/rokade.
configure_file(
  ${CMAKE_SOURCE_DIR}/src/libc/commandlist.json
  ${CMAKE_BINARY_DIR}/commandlist.json COPYONLY)

# Install the compiler + its data so `cmake --install` produces a usable tree.
install(TARGETS rokade
  RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/src/libc/commandlist.json
  DESTINATION share/rokade)

# ─── Language server (rook-lsp, Rust) ────────────────────────────────
# Optional Rust crate (off for ASAN/UBSAN, which can't link a Rust binary).
option(ROOK_BUILD_LSP "Build the rook-lsp language server" ON)
option(ROOK_LSP_C_HEADERS "Enable libclang project C-header parsing in rook-lsp" OFF)

if(ROOK_BUILD_LSP)
  find_program(CARGO_EXECUTABLE cargo)
  if(NOT CARGO_EXECUTABLE)
    message(FATAL_ERROR
      "ROOK_BUILD_LSP is ON but `cargo` was not found. Install the Rust "
      "toolchain, set CMAKE_PREFIX_PATH, or configure with -DROOK_BUILD_LSP=OFF.")
  endif()

  set(_cargo_manifest ${CMAKE_SOURCE_DIR}/lsp/Cargo.toml)
  set(_cargo_bin ${CMAKE_SOURCE_DIR}/lsp/target/release/rook-lsp)
  set(_lsp_features "")
  if(ROOK_LSP_C_HEADERS)
    list(APPEND _lsp_features --features c-headers)
  endif()

  # One recipe builds via cargo, then places the binary at build/rook-lsp.
  add_custom_target(rook_lsp ALL
    COMMAND ${CARGO_EXECUTABLE} build --release --manifest-path ${_cargo_manifest} ${_lsp_features}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_cargo_bin} ${CMAKE_BINARY_DIR}/rook-lsp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/lsp
    BYPRODUCTS ${_cargo_bin}
    COMMENT "Building rook-lsp (cargo) -> ${CMAKE_BINARY_DIR}/rook-lsp"
    VERBATIM)

  # Install the LSP binary alongside the compiler (only when built).
  install(FILES ${_cargo_bin}
    DESTINATION bin
    OPTIONAL)
endif()


