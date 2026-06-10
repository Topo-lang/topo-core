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
# TopoTranspile joins the TopoCoreLangTargets export set — NOT
# TopoCoreTargets — because it PUBLIC-depends on topo::lang::TopoLang (see
# the matching note in lib/Build/CMakeLists.txt and the staged include in
# topo-coreConfig.cmake.in).
install(TARGETS TopoTranspile
    EXPORT TopoCoreLangTargets
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
