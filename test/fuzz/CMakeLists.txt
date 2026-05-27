# Fuzz testing targets using libFuzzer.
# Only built when TOPO_BUILD_FUZZ=ON. CI-excluded by default.
#
# Usage:
#   cmake -B build -DTOPO_BUILD_FUZZ=ON ...
#   cmake --build build --target fuzz-lexer
#   ./build/topo-core/test/fuzz/fuzz-lexer corpus/ -max_total_time=60

option(TOPO_BUILD_FUZZ "Build libFuzzer fuzz testing targets" OFF)

if(NOT TOPO_BUILD_FUZZ)
    return()
endif()

# Use -fsanitize=fuzzer-no-link for instrumentation, then manually link the
# static fuzzer runtime. This avoids -fsanitize=fuzzer pulling in the UBSan
# dynamic runtime (which depends on LLVM 22's libc++ and conflicts with the
# system libc++ used by the rest of the project).
set(FUZZ_COMPILE_OPTIONS -fsanitize=fuzzer-no-link)

# Locate the libFuzzer runtime archive under the clang resource dir.
# Two probe paths:
#   - Monorepo with the bundled toolchain at topo-llvm/llvm-dev/
#   - Standalone: TOPO_FUZZER_CLANG_RESOURCE_DIR cache var pointing at a
#     clang install whose lib/clang/<ver>/lib/<plat>/ tree provides the
#     archive (CMAKE_CXX_COMPILER's resource dir is the natural default).
set(TOPO_FUZZER_CLANG_RESOURCE_DIR "" CACHE PATH
    "Path to a clang resource directory (lib/clang/<ver>) providing the \
libclang_rt.fuzzer_*.a runtime archive. Leave empty to auto-detect.")
if(TOPO_FUZZER_CLANG_RESOURCE_DIR)
    if(APPLE)
        file(GLOB _FUZZER_RT_LIST
            "${TOPO_FUZZER_CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.fuzzer_osx.a")
    elseif(UNIX)
        file(GLOB _FUZZER_RT_LIST
            "${TOPO_FUZZER_CLANG_RESOURCE_DIR}/lib/linux/libclang_rt.fuzzer-*.a")
    endif()
elseif(EXISTS "${CMAKE_SOURCE_DIR}/topo-llvm/llvm-dev")
    if(APPLE)
        file(GLOB _FUZZER_RT_LIST
            "${CMAKE_SOURCE_DIR}/topo-llvm/llvm-dev/lib/clang/*/lib/darwin/libclang_rt.fuzzer_osx.a")
    elseif(UNIX)
        file(GLOB _FUZZER_RT_LIST
            "${CMAKE_SOURCE_DIR}/topo-llvm/llvm-dev/lib/clang/*/lib/linux/libclang_rt.fuzzer-*.a")
    endif()
endif()
if(NOT _FUZZER_RT_LIST)
    message(WARNING "libFuzzer runtime not found; fuzz targets will not link")
    return()
endif()
list(GET _FUZZER_RT_LIST 0 _FUZZER_RT)
set(FUZZ_LINK_OPTIONS "${_FUZZER_RT}")

# Compatibility shim: the bundled LLVM 22 libFuzzer references
# std::__1::__hash_memory() which is absent from the macOS system libc++.
# FuzzerCompat.cpp provides a compatible implementation.
# On Linux (where libstdc++ is typically used), this shim is unnecessary
# but harmless — the linker will discard unused symbols.
add_library(FuzzCompat STATIC FuzzerCompat.cpp)
target_compile_features(FuzzCompat PRIVATE cxx_std_17)

# Helper: configure a fuzz target with all required flags
function(topo_add_fuzz_target name)
    topo_set_compiler_flags(${name})
    target_compile_options(${name} PRIVATE ${FUZZ_COMPILE_OPTIONS})
    target_link_options(${name} PRIVATE ${FUZZ_LINK_OPTIONS})
    target_link_libraries(${name} PRIVATE FuzzCompat)
endfunction()

# --- fuzz-lexer ---
add_executable(fuzz-lexer LexerFuzz.cpp)
target_link_libraries(fuzz-lexer PRIVATE TopoParser)
topo_add_fuzz_target(fuzz-lexer)

# --- fuzz-parser ---
add_executable(fuzz-parser ParserFuzz.cpp)
target_link_libraries(fuzz-parser PRIVATE TopoParser)
topo_add_fuzz_target(fuzz-parser)

# --- fuzz-sema ---
add_executable(fuzz-sema SemaFuzz.cpp)
target_link_libraries(fuzz-sema PRIVATE TopoSema TopoParser)
topo_add_fuzz_target(fuzz-sema)

# --- fuzz-formatter ---
# Sources topo::lsp::Formatter through the topo-lsp back-compat shim
# at topo-lsp/lib/Formatter.h. The shim is a sibling-repo header in
# standalone topo-core, so the include directory is wired via the
# TOPO_CORE_TOPO_LSP_SOURCE_DIR cache var (unset in standalone → skip).
set(TOPO_CORE_TOPO_LSP_SOURCE_DIR "" CACHE PATH
    "Path to a topo-lsp source tree exposing lib/Formatter.h back-compat shim.")
if(TOPO_CORE_TOPO_LSP_SOURCE_DIR
        AND EXISTS "${TOPO_CORE_TOPO_LSP_SOURCE_DIR}/lib/Formatter.h")
    add_executable(fuzz-formatter FormatterFuzz.cpp)
    target_link_libraries(fuzz-formatter PRIVATE TopoFormat)
    target_include_directories(fuzz-formatter PRIVATE
        ${TOPO_CORE_TOPO_LSP_SOURCE_DIR}/lib)
    topo_add_fuzz_target(fuzz-formatter)
else()
    message(STATUS "topo-core fuzz: fuzz-formatter skipped "
        "(TOPO_CORE_TOPO_LSP_SOURCE_DIR not set or shim header missing).")
endif()

# Fuzzers that link TopoBuildLib (plugin-aware) gate on its presence. In
# standalone topo-core without find_package(topo-lang), TopoBuildLib is
# not built and the entire group below is suppressed.
if(TARGET TopoBuildLib)
    # --- fuzz-symbol-table ---
    add_executable(fuzz-symbol-table SymbolTableSerializerFuzz.cpp)
    target_link_libraries(fuzz-symbol-table PRIVATE TopoBuildLib)
    topo_add_fuzz_target(fuzz-symbol-table)

    # --- fuzz-backend-request ---
    add_executable(fuzz-backend-request BackendRequestParseFuzz.cpp)
    target_link_libraries(fuzz-backend-request PRIVATE TopoBuildLib)
    topo_add_fuzz_target(fuzz-backend-request)

    # --- fuzz-incremental-cache ---
    # Verifies corrupted .topo-cache/manifest.json degrades gracefully (no crash).
    add_executable(fuzz-incremental-cache IncrementalCacheFuzz.cpp)
    target_link_libraries(fuzz-incremental-cache PRIVATE TopoBuildLib)
    topo_add_fuzz_target(fuzz-incremental-cache)

    # --- fuzz-config-validator ---
    # Feeds random-but-TOML-parseable input into validateConfig; ensures the
    # validator never crashes on any combination of BuildConfig fields.
    find_path(TOMLPLUSPLUS_INCLUDE_DIR toml++/toml.hpp)
    add_executable(fuzz-config-validator ConfigValidatorFuzz.cpp)
    target_link_libraries(fuzz-config-validator PRIVATE TopoBuildLib TopoBuildConfig)
    target_include_directories(fuzz-config-validator PRIVATE ${TOMLPLUSPLUS_INCLUDE_DIR})
    topo_add_fuzz_target(fuzz-config-validator)
else()
    message(STATUS "topo-core fuzz: TopoBuildLib-dependent fuzzers skipped "
        "(needs find_package(topo-lang)).")
endif()

# fuzz-transpile-differential needs every per-language transpile lib.
if(TARGET TopoCppTranspile AND TARGET TopoRustTranspile
        AND TARGET TopoJavaTranspile AND TARGET TopoPythonTranspile)
    # --- fuzz-transpile-differential ---
    # Drives all 4 emitters (C++, Rust, Java, Python) with the same parsed
    # .topo input -> SymbolTable -> minimal TranspileModule. Catches crashes
    # in shared model-walking paths across emitters.
    add_executable(fuzz-transpile-differential TranspileDifferentialFuzz.cpp)
    # Each per-language Topo*Transpile lib propagates TopoTranspile PUBLIC,
    # which in turn propagates TopoSema + TopoParser (via TopoSema → TopoParser).
    # Listing them again here triggers a duplicate-library link warning.
    target_link_libraries(fuzz-transpile-differential PRIVATE
        TopoCppTranspile TopoRustTranspile TopoJavaTranspile TopoPythonTranspile
    )
    topo_add_fuzz_target(fuzz-transpile-differential)
else()
    message(STATUS "topo-core fuzz: fuzz-transpile-differential skipped "
        "(needs all four Topo*Transpile libs from topo-lang).")
endif()

# --- CTest registration (short smoke runs, CI-excluded via 'fuzz' label) ---
set(_FUZZ_CORPUS "${CMAKE_CURRENT_SOURCE_DIR}/corpus")
set(_FUZZ_CORPUS_SYMTAB "${CMAKE_CURRENT_SOURCE_DIR}/corpus/symbol-table")
set(_FUZZ_CORPUS_BREQ "${CMAKE_CURRENT_SOURCE_DIR}/corpus/backend-request")
set(_FUZZ_ARGS -max_total_time=10 -rss_limit_mb=256)

# Always-built fuzzers (target the topo-core frontend; no plugin dependency).
add_test(NAME fuzz.lexer COMMAND fuzz-lexer ${_FUZZ_CORPUS} ${_FUZZ_ARGS})
set_tests_properties(fuzz.lexer PROPERTIES LABELS "fuzz" TIMEOUT 30)

add_test(NAME fuzz.parser COMMAND fuzz-parser ${_FUZZ_CORPUS} ${_FUZZ_ARGS})
set_tests_properties(fuzz.parser PROPERTIES LABELS "fuzz" TIMEOUT 30)

add_test(NAME fuzz.sema COMMAND fuzz-sema ${_FUZZ_CORPUS} ${_FUZZ_ARGS})
set_tests_properties(fuzz.sema PROPERTIES LABELS "fuzz" TIMEOUT 30)

if(TARGET fuzz-formatter)
    add_test(NAME fuzz.formatter COMMAND fuzz-formatter ${_FUZZ_CORPUS} ${_FUZZ_ARGS})
    set_tests_properties(fuzz.formatter PROPERTIES LABELS "fuzz" TIMEOUT 30)
endif()

if(TARGET fuzz-symbol-table)
    add_test(NAME fuzz.symbol-table COMMAND fuzz-symbol-table ${_FUZZ_CORPUS_SYMTAB} ${_FUZZ_ARGS})
    set_tests_properties(fuzz.symbol-table PROPERTIES LABELS "fuzz" TIMEOUT 30)
endif()

if(TARGET fuzz-backend-request)
    add_test(NAME fuzz.backend-request COMMAND fuzz-backend-request ${_FUZZ_CORPUS_BREQ} ${_FUZZ_ARGS})
    set_tests_properties(fuzz.backend-request PROPERTIES LABELS "fuzz" TIMEOUT 30)
endif()

# IncrementalCache + ConfigValidator have no .topo corpus; run from scratch.
if(TARGET fuzz-incremental-cache)
    add_test(NAME fuzz.incremental-cache COMMAND fuzz-incremental-cache ${_FUZZ_ARGS})
    set_tests_properties(fuzz.incremental-cache PROPERTIES LABELS "fuzz" TIMEOUT 30)
endif()

# TranspileDifferential reuses the .topo corpus (same input shape).
if(TARGET fuzz-transpile-differential)
    add_test(NAME fuzz.transpile-differential COMMAND fuzz-transpile-differential ${_FUZZ_CORPUS} ${_FUZZ_ARGS})
    set_tests_properties(fuzz.transpile-differential PROPERTIES LABELS "fuzz" TIMEOUT 30)
endif()

if(TARGET fuzz-config-validator)
    add_test(NAME fuzz.config-validator COMMAND fuzz-config-validator ${_FUZZ_ARGS})
    set_tests_properties(fuzz.config-validator PROPERTIES LABELS "fuzz" TIMEOUT 30)
endif()
