add_library(TopoPlatform
    Process.cpp
    ToolResolution.cpp
    FileGlob.cpp
    SharedLibrary.cpp
    Memory.cpp
    TempFile.cpp
    Sanitize.cpp
    FileLock.cpp
)

target_include_directories(TopoPlatform PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(TopoPlatform PRIVATE
    TOPO_LLVM_BINDIR="${TOPO_LLVM_BINDIR}"
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
