# Embedded parents add this after the language plugins and CLI targets. The
# normal standalone test path remains in topo-core/CMakeLists.txt.
if(NOT TOPO_CORE_BUILD_TESTS OR NOT TOPO_CORE_DEFER_TESTS)
    return()
endif()
if(NOT TARGET TopoTranspile OR NOT TARGET topo::lang::TopoLang)
    message(FATAL_ERROR "topo-core/test-late requires the deferred language libraries first")
endif()
if(TARGET topo-core-tests)
    message(FATAL_ERROR "topo-core tests have already been registered")
endif()

# Test helpers use the package root, not the embedding meta project's root.
get_filename_component(PROJECT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PROJECT_BINARY_DIR "${topo-core_BINARY_DIR}")
set(TOPO_CORE_WITH_LANG ON)
enable_testing()
if(NOT TARGET GTest::gtest_main)
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)
include(GoogleTest)
add_subdirectory("${PROJECT_SOURCE_DIR}/test" "${CMAKE_CURRENT_BINARY_DIR}/test")
