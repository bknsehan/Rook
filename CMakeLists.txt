cmake_minimum_required(VERSION 3.16)
project(rokade VERSION 0.7.1 LANGUAGES C)

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
  src/lint.c
  src/codegen.c
  src/backend.c
  src/c_backend.c
  src/config.c
  src/toolchain.c
  src/llvm_backend.c
  src/c_import.c
  src/lsp/lsp_json.c
  src/lsp/lsp_doc.c
  src/lsp/lsp_workspace.c
  src/lsp/lsp_features.c
  src/lsp/lsp_server.c
  src/resolve.c
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rokade PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation)
endif()

target_compile_definitions(rokade PRIVATE ROKADE_VERSION="${PROJECT_VERSION}")
if(WIN32)
  target_compile_definitions(rokade PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
endif()

# ─── Optional LLVM & libclang support ────────────────────────────────
option(ROKADE_ENABLE_LLVM "Enable native LLVM backend and libclang C-interop" ON)

if(ROKADE_ENABLE_LLVM)
  find_package(LLVM CONFIG)
  if(LLVM_FOUND)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    target_compile_definitions(rokade PRIVATE
      ROKADE_HAS_LLVM=1
      ROKADE_LLVM_VERSION="${LLVM_PACKAGE_VERSION}"
      ROKADE_LLVM_VERSION_MAJOR=${LLVM_VERSION_MAJOR}
    )
    target_include_directories(rokade PRIVATE ${LLVM_INCLUDE_DIRS})
    find_library(LLVM_LIB
      NAMES
        LLVM
        libLLVM
        LLVM-${LLVM_VERSION_MAJOR}
        libLLVM-${LLVM_VERSION_MAJOR}
        LLVM-C
        libLLVM-C
      HINTS
        ${LLVM_LIBRARY_DIRS}
    )
    if(LLVM_LIB)
      message(STATUS "Found LLVM library: ${LLVM_LIB}")
      target_link_libraries(rokade PRIVATE ${LLVM_LIB})
    elseif(TARGET LLVM)
      target_link_libraries(rokade PRIVATE LLVM)
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

# ─── Language server (rook-lsp, Native C) ─────────────────────────────
# Builds the native C language server (rook-lsp) sharing the compiler AST/Sema.
add_executable(rook-lsp
  src/lsp/lsp_main.c
  src/lsp/lsp_server.c
  src/lsp/lsp_features.c
  src/lsp/lsp_doc.c
  src/lsp/lsp_workspace.c
  src/lsp/lsp_json.c
  src/diag.c
  src/util.c
  src/ast.c
  src/lexer.c
  src/parse.c
  src/emit.c
  src/sema.c
  src/lint.c
  src/config.c
  src/toolchain.c
  src/c_import.c
  src/resolve.c
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rook-lsp PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation)
endif()

target_compile_definitions(rook-lsp PRIVATE ROKADE_VERSION="${PROJECT_VERSION}")
if(WIN32)
  target_compile_definitions(rook-lsp PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
endif()

if(CLANG_LIB AND CLANG_INCLUDE_DIR)
  target_compile_definitions(rook-lsp PRIVATE ROKADE_HAS_LIBCLANG=1)
  target_include_directories(rook-lsp PRIVATE ${CLANG_INCLUDE_DIR})
  target_link_libraries(rook-lsp PRIVATE ${CLANG_LIB})
endif()

if(M_LIB)
  target_link_libraries(rook-lsp PRIVATE ${M_LIB})
endif()

install(TARGETS rook-lsp
  RUNTIME DESTINATION bin)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/std/
  DESTINATION std
  OPTIONAL)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/editors/
  DESTINATION editors
  OPTIONAL)


