# TopoDistribution: on-demand backend distribution — the `topo backend` CLI,
# topo-backend.toml manifest parsing, registry index parsing, SemVer range
# matching, the ~/.topo/backends/ cache, and a pure-C++ SHA-256.
# Zero LLVM dependency; depends only on TopoPlatform plus header-only
# toml++ / nlohmann_json.
add_library(TopoDistribution
    Sha256.cpp
    SemVer.cpp
    BackendManifest.cpp
    RegistryIndex.cpp
    BackendCache.cpp
    BackendCli.cpp
)
target_include_directories(TopoDistribution PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

find_path(TOMLPLUSPLUS_INCLUDE_DIR toml++/toml.hpp)
target_include_directories(TopoDistribution PRIVATE ${TOMLPLUSPLUS_INCLUDE_DIR})

target_link_libraries(TopoDistribution
    PUBLIC TopoPlatform
    PRIVATE nlohmann_json::nlohmann_json
)
topo_set_compiler_flags(TopoDistribution)

topo_apply_std_pch(TopoDistribution)
# install component — topo-core-distribution.
# TopoDistribution PUBLIC-depends only on TopoPlatform, which stays inside
# topo-core, so it is safe to add to the TopoCoreTargets export set.
install(TARGETS TopoDistribution
    EXPORT TopoCoreTargets
    COMPONENT topo-core-distribution
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Distribution
    DESTINATION include/topo
    COMPONENT topo-core-distribution
    FILES_MATCHING PATTERN "*.h"
)
