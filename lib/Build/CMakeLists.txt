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
# Both join the TopoCoreLangTargets export set — NOT TopoCoreTargets —
# because TopoBuildLib PUBLIC-depends on topo::lang::TopoLang while
# topo-lang's own Targets file references topo::core::{Check,Sema,
# LSPBridge} back. Keeping the lang-linked targets in a second export lets
# the installed config load base targets → find_dependency(topo-lang) →
# lang-linked targets, which is the only order that satisfies both sides
# of the cycle (see topo-coreConfig.cmake.in).
install(TARGETS TopoBuildConfig TopoBuildLib
    EXPORT TopoCoreLangTargets
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
