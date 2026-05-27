# topo-build unit tests — zero LLVM.
#
# Historically these tests pulled .cpp sources from
# `${PROJECT_SOURCE_DIR}/topo-cli/tools/topo-build/` (the topo-build CLI
# implementation), because both lived in the same monorepo. After the
# repo split, topo-cli is a sibling repository whose source tree is not
# present in a standalone topo-core build. To preserve the in-monorepo
# behaviour without forcing the standalone build to know where topo-cli
# lives, the suite gates on:
#   - TopoBuildLib                  — the plugin-aware build lib target
#                                     (present only when find_package(
#                                     topo-lang) succeeded at top level)
#   - TOPO_CORE_TOPO_CLI_SOURCE_DIR — a CACHE PATH the consumer (or the
#                                     monorepo CMakeLists.txt) sets to a
#                                     topo-cli source tree containing
#                                     tools/topo-build/Config.cpp
#
# When either is missing the suite emits a single informational status
# line and registers nothing.
if(NOT TARGET TopoBuildLib)
    message(STATUS "topo-core tests: topo-build-tests skipped "
        "(TopoBuildLib not built — needs find_package(topo-lang)).")
    return()
endif()
if(NOT TOPO_CORE_TOPO_CLI_SOURCE_DIR
        OR NOT EXISTS "${TOPO_CORE_TOPO_CLI_SOURCE_DIR}/topo-build/Config.cpp")
    message(STATUS "topo-core tests: topo-build-tests skipped "
        "(TOPO_CORE_TOPO_CLI_SOURCE_DIR not set or does not contain "
        "topo-build/Config.cpp — set it to a topo-cli tools dir to enable).")
    return()
endif()

find_path(TOMLPLUSPLUS_INCLUDE_DIR_TESTS toml++/toml.hpp)

add_executable(topo-build-tests
    unit/IncrementalCacheTest.cpp
    unit/ConfigValidatorTest.cpp
    unit/ConfigLoadTest.cpp
    unit/SymbolTableJsonHintTest.cpp
    ${TOPO_CORE_TOPO_CLI_SOURCE_DIR}/topo-build/Config.cpp
)
# TopoBuildLib PUBLIC-propagates TopoPlatform + TopoSema + TopoLang;
# TopoSema PUBLIC-propagates TopoParser. Explicit TopoParser / TopoSema /
# TopoPlatform were the duplicates.
target_link_libraries(topo-build-tests PRIVATE
    TopoBuildLib TopoBuildConfig
    nlohmann_json::nlohmann_json
    GTest::gtest_main
)
target_include_directories(topo-build-tests PRIVATE
    ${TOPO_CORE_TOPO_CLI_SOURCE_DIR}/topo-build
    ${TOMLPLUSPLUS_INCLUDE_DIR_TESTS}
)
target_compile_definitions(topo-build-tests PRIVATE
    TOPO_TEST_FIXTURES_DIR="${TOPO_CORE_FIXTURES_DIR}"
)
topo_set_compiler_flags(topo-build-tests)
topo_apply_std_pch(topo-build-tests)
gtest_discover_tests(topo-build-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS "toolchain\;build" TIMEOUT 60)

# Smoke test: config validation
add_test(NAME smoke.build.config
    COMMAND topo-build-tests --gtest_filter="ConfigValidatorTest.*:ConfigTest.*")
set_tests_properties(smoke.build.config PROPERTIES
    LABELS "smoke;build" TIMEOUT 15)

# Shell E2E tests for topo-build
if(TOPO_ENABLE_LLVM AND TARGET topo-build)
    add_subdirectory(e2e)
endif()

# Feature dimension labels
set_property(DIRECTORY APPEND PROPERTY
    TEST_INCLUDE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/FeatureMap.cmake")
