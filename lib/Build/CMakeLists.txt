# TopoBuildConfig: INTERFACE library for type-only consumers (BuildTypes.h, HostLanguage.h).
add_library(TopoBuildConfig INTERFACE)
add_library(topo::core::TopoBuildConfig ALIAS TopoBuildConfig)
target_include_directories(TopoBuildConfig INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# TopoBuildLib: build-infrastructure library (Config, Protocol, Cache, etc.)
add_library(TopoBuildLib
    BackendProtocol.cpp
    ConfigValidator.cpp
    IncrementalCache.cpp
    PassCategoryRegistry.cpp
    ProfileDefaults.cpp
    SymbolTableJson.cpp
)
add_library(topo::core::TopoBuildLib ALIAS TopoBuildLib)
target_include_directories(TopoBuildLib PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(TopoBuildLib
    PUBLIC TopoPlatform TopoSema topo::lang::TopoLang
    PUBLIC nlohmann_json::nlohmann_json
)
topo_set_compiler_flags(TopoBuildLib)

topo_apply_std_pch(TopoBuildLib)
# install component — topo-core-build.
# TopoBuildConfig is INTERFACE (no artifact), TopoBuildLib is STATIC/SHARED.
# These targets are intentionally NOT in the TopoCoreTargets export set:
# TopoBuildLib PUBLIC-depends on TopoLang (owned by the topo-lang
# package), which would require cross-package find_dependency() plumbing in
# the package config. The artifact install still works; downstream
# in-tree consumers continue using the raw target names. A later iteration may revisit
# once a clean topo-lang export surface exists (the namespaced
# target topo::core::build is therefore deferred).
install(TARGETS TopoBuildConfig TopoBuildLib
    EXPORT TopoCoreTargets
    COMPONENT topo-core-build
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Build
    DESTINATION include/topo
    COMPONENT topo-core-build
    FILES_MATCHING PATTERN "*.h"
)
# BackendTypes.h lives under topo/Backend/ but is part of the build subsystem
# (consumed via topo/Build/BackendProtocol.h). Ship it with topo-core-build.
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Backend
    DESTINATION include/topo
    COMPONENT topo-core-build
    FILES_MATCHING PATTERN "*.h"
)
