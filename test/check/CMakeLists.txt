# Framework-level checker tests (language-agnostic).
# Language-specific checker E2E tests live in each topo-lang-*/test/.
#
# Links TopoCheckRunner + the five per-language Topo*Plugin libs, each
# of which lives in its corresponding topo-lang-* package. In standalone
# topo-core only the frontend / check / analysis libs are built; the
# plugins arrive via find_package(topo-lang) and the topo-lang-* sibling
# installs. Gate on TopoCheckRunner plus each plugin so the suite skips
# cleanly when any of them is missing rather than failing the configure
# with "target not found".

# ── Standalone harness mode ──────────────────────────────────────────
# When this directory is configured AS THE TOP-LEVEL SOURCE
# (`cmake -S <topo-core>/test/check`), bootstrap every dependency from
# CMAKE_PREFIX_PATH: topo-cli (provides topo::cli::TopoCheckRunner) plus
# the five topo-lang-* plugin packages. This is how topo-cli's CI runs
# the suite against an assembled install prefix. All find_package calls
# are REQUIRED — this mode exists precisely to RUN the suite, so a
# missing package must fail the configure loudly instead of degrading
# into the self-skip below (a skipped suite is not a passing suite).
# In-tree configures (topo-core test tree, meta union build) leave this
# block inert and keep the gate-or-skip behavior unchanged.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    # C is enabled so transitive check_include_file() probes work (e.g.
    # LLVM's libedit/histedit.h check, reached via topo-lang-cpp's
    # LLVM-scope package config -> find_dependency(LLVM/Clang) on macOS).
    project(topo-check-suite LANGUAGES C CXX)

    # Compiler-flag helpers (topo_set_compiler_flags + the
    # topo_apply_std_pch stub) from the repo-local module — the same one
    # the standalone topo-core root build uses.
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/TopoCompilerFlags.cmake)

    find_package(topo-cli CONFIG REQUIRED)
    foreach(_lang IN ITEMS cpp rust java python typescript)
        find_package(topo-lang-${_lang} CONFIG REQUIRED)
    endforeach()

    # Bare-name bridge for the imported namespaced targets (mirrors the
    # alias loop in topo-core/test/CMakeLists.txt) so the gate + link
    # list below work identically in-tree and standalone.
    foreach(_alias_pair IN ITEMS
        "TopoCheckRunner|topo::cli::TopoCheckRunner"
        "TopoCppPlugin|topo::lang-cpp::TopoCppPlugin"
        "TopoRustPlugin|topo::lang-rust::TopoRustPlugin"
        "TopoJavaPlugin|topo::lang-java::TopoJavaPlugin"
        "TopoPythonPlugin|topo::lang-python::TopoPythonPlugin"
        "TopoTypeScriptPlugin|topo::lang-typescript::TopoTypeScriptPlugin"
    )
        string(REPLACE "|" ";" _pair "${_alias_pair}")
        list(GET _pair 0 _bare)
        list(GET _pair 1 _ns)
        if(TARGET ${_ns} AND NOT TARGET ${_bare})
            add_library(${_bare} ALIAS ${_ns})
        endif()
    endforeach()

    enable_testing()
    if(NOT TARGET GTest::gtest)
        include(FetchContent)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    # PRE_TEST: defer gtest case enumeration from post-link to ctest
    # time (same rationale as the topo-core root test setup).
    set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)
    include(GoogleTest)
endif()

set(_topo_check_required_targets
    TopoCheckRunner
    TopoCppPlugin TopoRustPlugin TopoJavaPlugin TopoPythonPlugin TopoTypeScriptPlugin)
foreach(_t IN LISTS _topo_check_required_targets)
    if(NOT TARGET ${_t})
        message(STATUS "topo-core tests: topo-check-tests skipped "
            "(target ${_t} not available — needs find_package(topo-lang) + "
            "the matching topo-lang-* package).")
        return()
    endif()
endforeach()

add_executable(topo-check-tests
    TestMain.cpp
    e2e/CheckRunnerTest.cpp
)

target_link_libraries(topo-check-tests PRIVATE
    TopoCheckRunner
    TopoCppPlugin TopoRustPlugin TopoJavaPlugin TopoPythonPlugin TopoTypeScriptPlugin
    GTest::gtest
)
target_compile_definitions(topo-check-tests PRIVATE
    TOPO_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
topo_set_compiler_flags(topo-check-tests)
topo_apply_std_pch(topo-check-tests)
gtest_discover_tests(topo-check-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS "toolchain\;checker" TIMEOUT 60)
