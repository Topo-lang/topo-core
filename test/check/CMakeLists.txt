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
