add_library(TopoPlatform
    Process.cpp
    AnalysisProcess.cpp
    ToolResolution.cpp
    FileGlob.cpp
    SharedLibrary.cpp
    Memory.cpp
    TempFile.cpp
    Sanitize.cpp
    FileLock.cpp
)
add_library(topo::core::TopoPlatform ALIAS TopoPlatform)

target_include_directories(TopoPlatform PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(TopoPlatform PRIVATE
    TOPO_LLVM_BINDIR="${TOPO_LLVM_BINDIR}"
    TOPO_LLVM_MAJOR="${TOPO_LLVM_MAJOR}"
)

# dlopen/dlsym on Unix (no-op on Windows)
target_link_libraries(TopoPlatform PRIVATE ${CMAKE_DL_LIBS})

# reproc++ powers Process.cpp (subprocess driver). PRIVATE link without a
# BUILD_INTERFACE wrap: TopoPlatform is a STATIC library, so its .o files
# carry unresolved reproc::process::* symbols regardless of how CMake
# describes the dependency. A BUILD_INTERFACE wrap hides reproc from
# TopoCoreTargets.cmake but the symbols still need resolving at downstream
# link time — third_party/reproc/CMakeLists.txt now ships reproc artifacts
# into the same install prefix, and topo-coreConfig.cmake.in does
# find_dependency(reproc++ CONFIG) so downstream find_package(topo-core)
# gets reproc transparently.
target_link_libraries(TopoPlatform PRIVATE reproc++)

topo_set_compiler_flags(TopoPlatform)


topo_apply_std_pch(TopoPlatform)
# Process.cpp wraps reproc++ and intentionally ignores the Windows / POSIX
# private fields on PipedProcess (kept in the public header for ABI
# stability). Suppress the resulting -Wunused-private-field locally rather
# than mutating the header.
if(NOT MSVC)
    set_source_files_properties(Process.cpp PROPERTIES
        COMPILE_FLAGS "-Wno-unused-private-field")
endif()

# install component — topo-core-frontend
install(TARGETS TopoPlatform
    EXPORT TopoCoreTargets
    COMPONENT topo-core-frontend
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Platform
    DESTINATION include/topo
    COMPONENT topo-core-frontend
    FILES_MATCHING PATTERN "*.h"
)

option(TOPO_ENABLE_EXPERIMENTAL_LINUX_ANALYSIS
    "Build experimental Linux resource-isolation API (does not enable strict audit capability)" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TOPO_ENABLE_EXPERIMENTAL_LINUX_ANALYSIS)
    target_sources(TopoPlatform PRIVATE LinuxAnalysisSandbox.cpp)
    target_compile_definitions(TopoPlatform PUBLIC TOPO_ENABLE_EXPERIMENTAL_LINUX_ANALYSIS=1)
endif()

# A native entry marker establishes that this invocation has entered the OS sandbox.
# The launcher uses only the system runtime and receives the fixed environment.
if(APPLE OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TOPO_ENABLE_EXPERIMENTAL_LINUX_ANALYSIS))
    add_executable(topo-analysis-launcher AnalysisLauncher.cpp)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(topo-analysis-launcher PRIVATE LinuxAnalysisSandbox.cpp)
    endif()
    topo_set_compiler_flags(topo-analysis-launcher)
    target_compile_definitions(TopoPlatform PRIVATE
        TOPO_ANALYSIS_LAUNCHER_PATH="$<TARGET_FILE:topo-analysis-launcher>"
        TOPO_ANALYSIS_INSTALLED_LAUNCHER_PATH="${CMAKE_INSTALL_PREFIX}/bin/topo-analysis-launcher")
    add_dependencies(TopoPlatform topo-analysis-launcher)
    install(TARGETS topo-analysis-launcher COMPONENT topo-core-frontend RUNTIME DESTINATION bin)
endif()
