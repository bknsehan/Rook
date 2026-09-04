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
  src/llvm_backend.c
  src/c_import.c
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rokade PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation)
endif()

# ─── Optional LLVM & libclang support ────────────────────────────────
option(ROKADE_ENABLE_LLVM "Enable experimental LLVM backend and libclang C-interop" ON)

if(ROKADE_ENABLE_LLVM)
  find_package(LLVM CONFIG)
  if(LLVM_FOUND)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    target_compile_definitions(rokade PRIVATE ROKADE_HAS_LLVM=1 ROKADE_LLVM_VERSION="${LLVM_PACKAGE_VERSION}")
    target_include_directories(rokade PRIVATE ${LLVM_INCLUDE_DIRS})
    find_library(LLVM_LIB NAMES LLVM libLLVM HINTS ${LLVM_LIBRARY_DIRS})
    if(LLVM_LIB)
      target_link_libraries(rokade PRIVATE ${LLVM_LIB})
    endif()

    find_library(CLANG_LIB NAMES clang libclang HINTS ${LLVM_LIBRARY_DIRS})
    find_path(CLANG_INCLUDE_DIR clang-c/Index.h HINTS ${LLVM_INCLUDE_DIRS})
    if(CLANG_LIB AND CLANG_INCLUDE_DIR)
      message(STATUS "Found libclang: ${CLANG_LIB}")
      target_compile_definitions(rokade PRIVATE ROKADE_HAS_LIBCLANG=1)
      target_include_directories(rokade PRIVATE ${CLANG_INCLUDE_DIR})
      target_link_libraries(rokade PRIVATE ${CLANG_LIB})
    endif()
    find_library(M_LIB m)
    if(M_LIB)
      target_link_libraries(rokade PRIVATE ${M_LIB})
    endif()
    set_target_properties(rokade PROPERTIES ENABLE_EXPORTS ON)
  else()
    message(STATUS "LLVM not found; building with C backend only.")
  endif()
endif()

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

install(DIRECTORY ${CMAKE_SOURCE_DIR}/std/
  DESTINATION std
  OPTIONAL)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/editors/
  DESTINATION editors
  OPTIONAL)


