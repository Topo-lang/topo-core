# Shell E2E tests for topo-build CLI. POSIX-shell only — Windows runners
# don't ship `sh` on PATH by default and the test scripts use POSIX-only
# constructs. Skip registration entirely on Windows.
if(WIN32)
    return()
endif()

set(SHELL_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

set(SHELL_TEST_ENV
    "TOPO_BUILD_EXE=$<TARGET_FILE:topo-build>"
    "TOPO_FIXTURES_DIR=${TOPO_CORE_BUILD_FIXTURES_DIR}"
    "TOPO_PROJECT_ROOT=${PROJECT_SOURCE_DIR}"
    "TOPO_LLVM_BINDIR=${TOPO_LLVM_BINDIR}"
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
