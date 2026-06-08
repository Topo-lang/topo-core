# topo-init unit tests.
#
# These tests #include + compile-link source files from two locations
# that historically were monorepo siblings of topo-core:
#   - topo-cli/tools/topo-init/{TopoGenerator,InitConfig}.cpp
#   - topo-lang-cpp/ (header path for TopoCppAnalysis include closure)
#
# In a standalone topo-core build neither sibling is present. Gate on:
#   - TopoCppAnalysis              — provided by topo-lang-cpp when it
#                                    is installed and find_package(
#                                    topo-lang) succeeded.
#   - TOPO_CORE_TOPO_CLI_SOURCE_DIR — CACHE PATH to a topo-cli source
#                                    tree containing tools/topo-init/.
# When either is missing the suite emits an informational status line.
if(NOT TARGET TopoCppAnalysis)
    message(STATUS "topo-core tests: topo-init-tests skipped "
        "(TopoCppAnalysis target not found — needs topo-lang-cpp).")
    return()
endif()
if(NOT TOPO_CORE_TOPO_CLI_SOURCE_DIR
        OR NOT EXISTS "${TOPO_CORE_TOPO_CLI_SOURCE_DIR}/topo-init/TopoGenerator.cpp")
    message(STATUS "topo-core tests: topo-init-tests skipped "
        "(TOPO_CORE_TOPO_CLI_SOURCE_DIR not set or does not contain "
        "topo-init/TopoGenerator.cpp — set it to a topo-cli tools dir to enable).")
    return()
endif()

add_executable(topo-init-tests
    unit/TopoInitTest.cpp
)

# TopoCppAnalysis PUBLIC-propagates TopoCppLSP → TopoLSPBridge → TopoPlatform.
target_link_libraries(topo-init-tests PRIVATE
    TopoCppAnalysis
    GTest::gtest_main
)

target_include_directories(topo-init-tests PRIVATE
    ${TOPO_CORE_TOPO_CLI_SOURCE_DIR}/topo-init
)

# Link the implementation files directly (no library target for topo-init)
target_sources(topo-init-tests PRIVATE
    ${TOPO_CORE_TOPO_CLI_SOURCE_DIR}/topo-init/TopoGenerator.cpp
    ${TOPO_CORE_TOPO_CLI_SOURCE_DIR}/topo-init/InitConfig.cpp
)

topo_set_compiler_flags(topo-init-tests)

topo_apply_std_pch(topo-init-tests)
gtest_discover_tests(topo-init-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS "toolchain\;init")
