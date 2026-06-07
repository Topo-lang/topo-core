# Relocation ctest (label: relocate).
#
# Verifies the BYO-priority LLVM toolchain resolver in
# topo-core/lib/Platform/ToolResolution.cpp: a binary resolves a
# differently-located LLVM via TOPO_LLVM_DIR, and an explicit-but-bad path
# fails loudly. The probe links TopoPlatform (which carries the resolver and
# its compile-time defaults); the script drives it across env scenarios.

# Driver script is bash + symlinks; Windows runners don't ship bash on PATH.
if(WIN32)
    return()
endif()

add_executable(relocation_probe relocation_probe.cpp)
target_link_libraries(relocation_probe PRIVATE topo::core::TopoPlatform)
topo_set_compiler_flags(relocation_probe)

add_test(
    NAME relocate.llvm-toolchain-resolver
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/run-relocation-check.sh
            $<TARGET_FILE:relocation_probe>
)
set_tests_properties(relocate.llvm-toolchain-resolver PROPERTIES
    LABELS "relocate"
    SKIP_RETURN_CODE 77
    TIMEOUT 60
)
