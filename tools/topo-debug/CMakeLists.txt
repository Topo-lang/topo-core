add_executable(topo-debug main.cpp)
target_link_libraries(topo-debug PRIVATE
    TopoDebug
    TopoPlatform
    nlohmann_json::nlohmann_json
)
topo_set_compiler_flags(topo-debug)

# bake the vendor directory path into the binary so
# `topo debug serve` can default to a useful location when no --vendor-path
# is given. `TOPO_DEBUG_VENDOR_BUILD_DIR` is published by
# topo-core/lib/Debug/CMakeLists.txt.
target_compile_definitions(topo-debug PRIVATE
    TOPO_DEBUG_VENDOR_DEFAULT_DIR="${TOPO_DEBUG_VENDOR_BUILD_DIR}")
# bake the default templates directory path so
# `topo debug serve` serves GET /templates/<name> with no --template-path.
# `TOPO_DEBUG_TEMPLATES_BUILD_DIR` is published by
# topo-core/lib/Debug/CMakeLists.txt.
target_compile_definitions(topo-debug PRIVATE
    TOPO_DEBUG_TEMPLATES_DEFAULT_DIR="${TOPO_DEBUG_TEMPLATES_BUILD_DIR}")

install(TARGETS topo-debug
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT topo-core-tools
)
# Install the vendor + templates trees alongside the binary so the runtime
# fallback in main.cpp (<exe_dir>/../share/topo-core/topo-debug/{vendor,
# templates}) can find them after install.
install(DIRECTORY ${TOPO_DEBUG_VENDOR_BUILD_DIR}/
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/topo-core/topo-debug/vendor
    COMPONENT topo-core-tools)
install(DIRECTORY ${TOPO_DEBUG_TEMPLATES_BUILD_DIR}/
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/topo-core/topo-debug/templates
    COMPONENT topo-core-tools)
# Note: e2e tests for this binary live in topo-core/test/CMakeLists.txt —
# enable_testing() is set up by root AFTER add_subdirectory(topo-core), so
# add_test() at this level is silently dropped.
