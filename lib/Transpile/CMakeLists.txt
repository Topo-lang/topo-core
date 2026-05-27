add_library(TopoTranspile
    TranspileModelJson.cpp
    ModelOptimizer.cpp
    TranspileDriver.cpp
    TopoSourceBuilder.cpp
    AdapterResolver.cpp
)
add_library(topo::core::TopoTranspile ALIAS TopoTranspile)

target_link_libraries(TopoTranspile PUBLIC TopoAST TopoSema TopoPlatform topo::lang::TopoLang)
target_link_libraries(TopoTranspile PRIVATE nlohmann_json::nlohmann_json)

topo_set_compiler_flags(TopoTranspile)


topo_apply_std_pch(TopoTranspile)
# install component — topo-core-transpile.
# TopoTranspile is intentionally NOT in the TopoCoreTargets export set:
# it PUBLIC-depends on TopoLang (owned by the topo-lang package),
# which would require cross-package find_dependency() plumbing in the
# package config. The artifact install still works; downstream in-tree
# consumers continue using the raw target name. A later iteration may revisit once
# a clean topo-lang export surface exists (the namespaced target
# topo::core::transpile is therefore deferred).
install(TARGETS TopoTranspile
    EXPORT TopoCoreTargets
    COMPONENT topo-core-transpile
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Transpile
    DESTINATION include/topo
    COMPONENT topo-core-transpile
    FILES_MATCHING PATTERN "*.h"
)
