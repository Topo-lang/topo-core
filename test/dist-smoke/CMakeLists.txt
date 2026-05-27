# minimal-dist smoke ctest.
#
# Verifies that the frontend + check install components produce a
# self-contained subset — required libs/headers are present and the
# optional components (transpile / build / fmt / transform) do NOT
# leak into a check-only install.
#
# The test depends on `cmake --install build --component <comp>` having
# been wired by the install-component rules in each per-lib CMakeLists.txt.
# It exercises the install rules; it does NOT exercise compile/run of the
# installed artifacts (that is later work, gated on benchmark data).

# Driver script is bash-only (uses bash arrays + [[ ]]); Windows runners
# don't ship bash on PATH. Skip on Windows.
if(WIN32)
    return()
endif()

add_test(
    NAME topo-core-minimal-dist-check-only
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/check-only-install.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(topo-core-minimal-dist-check-only PROPERTIES
    LABELS "minimal-dist"
    ENVIRONMENT "BUILD_DIR=${CMAKE_BINARY_DIR}"
    TIMEOUT 60
)
