# TopoCompilerFlags.cmake — Shared compiler settings for standalone topo-core.
#
# Carved out of the monorepo's cmake/TopoCompilerFlags.cmake. Standalone
# topo-core drops the LLVM-flag helper (no LLVM-linking targets here — that
# lives in topo-llvm) and replaces the PCH-reuse helper with a no-op so
# every lib/*/CMakeLists.txt that calls `topo_apply_std_pch(<tgt>)` keeps
# working without modification. PCH reuse is a build-time optimization
# (saves ~1s/TU); the no-op path still produces correct code, only slower.

# RPATH configuration for Unix shared library builds
if(NOT WIN32)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    if(APPLE)
        set(CMAKE_MACOSX_RPATH ON)
    endif()
endif()

# ── Sanitizer support ─────────────────────────────────
# Usage: cmake -B build -DTOPO_SANITIZER=address
#        cmake -B build -DTOPO_SANITIZER=undefined
#        cmake -B build -DTOPO_SANITIZER=address,undefined
set(TOPO_SANITIZER "" CACHE STRING
    "Enable sanitizers (address, undefined, thread, memory)")

if(TOPO_SANITIZER)
    message(STATUS "topo-core sanitizers enabled: ${TOPO_SANITIZER}")
endif()

function(topo_apply_sanitizer target)
    if(NOT TOPO_SANITIZER)
        return()
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target}
            PRIVATE -fsanitize=${TOPO_SANITIZER} -fno-omit-frame-pointer)
        target_link_options(${target}
            PRIVATE -fsanitize=${TOPO_SANITIZER})
    endif()
endfunction()

function(topo_set_compiler_flags target)
    target_compile_features(${target} PUBLIC cxx_std_17)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    endif()

    topo_apply_sanitizer(${target})
endfunction()

# Standalone-mode PCH stub: no-op. Lib CMakeLists keep calling this; build
# is correct, just slower than the monorepo PCH-reuse path.
function(topo_apply_std_pch target)
    # intentionally empty in standalone topo-core
endfunction()
