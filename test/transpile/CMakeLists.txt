# Transpile equivalence tests — compile + run emitter output in 4 languages.
#
# Links every per-host transpile lib plus the V8-shared codegen. The
# Topo*Transpile libs arrive via topo-lang (parent caller already gated
# on TOPO_CORE_WITH_LANG); TopoV8Codegen lives in topo-v8. If topo-v8 is
# not yet on the prefix path, skip the suite cleanly.
set(_topo_transpile_required_targets
    TopoCppTranspile TopoRustTranspile TopoJavaTranspile TopoPythonTranspile
    TopoV8Codegen TopoTranspile)
foreach(_t IN LISTS _topo_transpile_required_targets)
    if(NOT TARGET ${_t})
        message(STATUS "topo-core tests: topo-transpile-equivalence-tests "
            "skipped (target ${_t} not available — needs find_package("
            "topo-lang) + topo-v8).")
        return()
    endif()
endforeach()

add_executable(topo-transpile-equivalence-tests
    equivalence/TranspileEquivalenceTest.cpp
)
# TopoTranspile PUBLIC-propagates TopoSema and TopoPlatform.
target_link_libraries(topo-transpile-equivalence-tests PRIVATE
    TopoCppTranspile TopoRustTranspile TopoJavaTranspile TopoPythonTranspile
    TopoV8Codegen
    TopoTranspile
    GTest::gtest_main
)
topo_set_compiler_flags(topo-transpile-equivalence-tests)

topo_apply_std_pch(topo-transpile-equivalence-tests)
# TypeScript-as-source leg: the test drives the real topo-transpile CLI,
# which spawns the real topo-extract-typescript launcher resolved via PATH.
# Hand the test the CLI binary path and the staged tool directory; depend on
# both targets so the CLI is built and the extractor's POST_BUILD staging
# (npm install + launcher placement) has run before the test executes.
if(TARGET topo-transpile AND TARGET topo-extract-typescript-stage)
    target_compile_definitions(topo-transpile-equivalence-tests PRIVATE
        TOPO_TRANSPILE_CLI_BINARY="$<TARGET_FILE:topo-transpile>"
        TOPO_EXTRACT_TS_TOOL_DIR="$<TARGET_FILE_DIR:topo-build-typescript>"
    )
    add_dependencies(topo-transpile-equivalence-tests
        topo-transpile topo-extract-typescript-stage)
endif()

# C++-as-source leg: drives the real topo-transpile CLI, which spawns the
# real topo-extract-cpp resolved by bare name via PATH. Gated on the LLVM
# extractor being built (libclang).
if(TARGET topo-transpile AND TARGET topo-extract-cpp)
    target_compile_definitions(topo-transpile-equivalence-tests PRIVATE
        TOPO_TRANSPILE_CLI_BINARY="$<TARGET_FILE:topo-transpile>"
        TOPO_EXTRACT_CPP_TOOL_DIR="$<TARGET_FILE_DIR:topo-extract-cpp>"
    )
    add_dependencies(topo-transpile-equivalence-tests
        topo-transpile topo-extract-cpp)
endif()

# Python-as-source leg: drives the real topo-transpile CLI, which spawns
# the topo-extract-python launcher (a python3 shim) resolved by bare
# name via PATH. The launcher is staged alongside topo-build-python by
# the topo-extract-python-stage target; we hand the test that binary
# directory and let it prepend it to PATH at runtime.
if(TARGET topo-transpile AND TARGET topo-extract-python-stage AND TARGET topo-build-python)
    target_compile_definitions(topo-transpile-equivalence-tests PRIVATE
        TOPO_TRANSPILE_CLI_BINARY="$<TARGET_FILE:topo-transpile>"
        TOPO_EXTRACT_PY_TOOL_DIR="$<TARGET_FILE_DIR:topo-build-python>"
    )
    add_dependencies(topo-transpile-equivalence-tests
        topo-transpile topo-extract-python-stage)
endif()

# Rust-as-source leg: drives the real topo-transpile CLI, which spawns
# the cargo-built `topo-extract-rust` binary resolved by bare name via
# PATH. Unlike the other extractors, the Rust binary is produced by
# cargo into a source-tree `target/release/` directory (not staged into
# the cmake binary tree), so we hand the test that source-tree path.
if(TARGET topo-transpile AND TARGET topo-extract-rust)
    target_compile_definitions(topo-transpile-equivalence-tests PRIVATE
        TOPO_TRANSPILE_CLI_BINARY="$<TARGET_FILE:topo-transpile>"
        TOPO_EXTRACT_RUST_TOOL_DIR="${PROJECT_SOURCE_DIR}/topo-lang-rust/topo-check/extractor/target/release"
    )
    add_dependencies(topo-transpile-equivalence-tests
        topo-transpile topo-extract-rust)
endif()

gtest_discover_tests(topo-transpile-equivalence-tests DISCOVERY_TIMEOUT 30
    PROPERTIES
        LABELS "toolchain;transpile_equivalence"
        TIMEOUT 300
)
