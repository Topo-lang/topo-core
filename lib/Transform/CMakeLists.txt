add_library(TopoTransform
    TokenStreamRewriter.cpp
    SemanticVerifier.cpp
)
add_library(topo::core::TopoTransform ALIAS TopoTransform)

target_link_libraries(TopoTransform PUBLIC TopoLexer TopoSema)

topo_set_compiler_flags(TopoTransform)


topo_apply_std_pch(TopoTransform)
# install component — topo-core-fmt
install(TARGETS TopoTransform
    EXPORT TopoCoreTargets
    COMPONENT topo-core-fmt
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Transform
    DESTINATION include/topo
    COMPONENT topo-core-fmt
    FILES_MATCHING PATTERN "*.h"
)
