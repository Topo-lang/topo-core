# Shell E2E tests for topo-build CLI. POSIX-shell only — Windows runners
# don't ship `sh` on PATH by default and the test scripts use POSIX-only
# constructs. Skip registration entirely on Windows.
if(WIN32)
    return()
endif()

# Self-gating: every cpp fixture build dispatches to the topo-build-llvm-cpp
# backend tool, so the suite needs both the CLI and the backend tool as live
# targets (TOPO_ENABLE_LLVM alone is the wrong gate — a tools-off LLVM scope
# has the option ON but no backend binary). Consumers add this directory
# unconditionally; it registers nothing when the targets are absent.
if(NOT TARGET topo-build OR NOT TARGET topo-build-llvm-cpp)
    message(STATUS "topo-core tests: shell e2e suite skipped "
        "(needs topo-build + topo-build-llvm-cpp targets).")
    return()
endif()

set(SHELL_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Run the build fixtures from a build-tree COPY, never the in-source tree.
# topo-build emits each project's executable, .topo-cache/, LLVM IR and (for
# the rust fixture) a cargo target/ next to its Topo.toml; doing that in-source
# permanently dirtied the checkout and forced fixtures/.gitignore to enumerate
# every output by name. Copying to the build dir keeps the source pristine and
# makes the per-binary ignore list unnecessary.
set(E2E_FIXTURES_RUN_DIR "${CMAKE_CURRENT_BINARY_DIR}/run-fixtures")
file(COPY "${TOPO_CORE_BUILD_FIXTURES_DIR}/" DESTINATION "${E2E_FIXTURES_RUN_DIR}")

# TOPO_BACKEND_TOOL_DIR: topo-build resolves backend tools via its exe dir,
# then PATH; the scripts prepend this dir so dispatch hits the build-tree
# topo-build-llvm-cpp instead of whatever (if anything) is installed.
set(SHELL_TEST_ENV
    "TOPO_BUILD_EXE=$<TARGET_FILE:topo-build>"
    "TOPO_FIXTURES_DIR=${E2E_FIXTURES_RUN_DIR}"
    "TOPO_BACKEND_TOOL_DIR=$<TARGET_FILE_DIR:topo-build-llvm-cpp>"
)

add_test(NAME e2e.shell.basic_build
    COMMAND sh "${SHELL_TEST_DIR}/test_basic_build.sh")
add_test(NAME e2e.shell.error_handling
    COMMAND sh "${SHELL_TEST_DIR}/test_error_handling.sh")
add_test(NAME e2e.shell.incremental
    COMMAND sh "${SHELL_TEST_DIR}/test_incremental.sh")
add_test(NAME e2e.shell.config_combos
    COMMAND sh "${SHELL_TEST_DIR}/test_config_combos.sh")
add_test(NAME e2e.shell.error_reporting
    COMMAND sh "${SHELL_TEST_DIR}/test_error_reporting.sh")
add_test(NAME e2e.shell.build_modes
    COMMAND sh "${SHELL_TEST_DIR}/test_build_modes.sh")

set_tests_properties(
    e2e.shell.basic_build
    e2e.shell.error_handling
    e2e.shell.incremental
    e2e.shell.config_combos
    e2e.shell.error_reporting
    e2e.shell.build_modes
    PROPERTIES
        ENVIRONMENT "${SHELL_TEST_ENV}"
        LABELS "e2e;shell"
        RESOURCE_LOCK "topo_e2e_build"
        TIMEOUT 120
)
