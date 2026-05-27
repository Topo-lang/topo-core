add_library(TopoParser
    Parser.cpp
)

target_link_libraries(TopoParser PUBLIC TopoLexer TopoAST)

topo_set_compiler_flags(TopoParser)


topo_apply_std_pch(TopoParser)
# install component — topo-core-frontend
install(TARGETS TopoParser
    EXPORT TopoCoreTargets
    COMPONENT topo-core-frontend
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Parser
    DESTINATION include/topo
    COMPONENT topo-core-frontend
    FILES_MATCHING PATTERN "*.h"
)
