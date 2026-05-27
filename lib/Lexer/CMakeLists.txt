add_library(TopoLexer
    Lexer.cpp
)

target_link_libraries(TopoLexer PUBLIC TopoBasic)

topo_set_compiler_flags(TopoLexer)


topo_apply_std_pch(TopoLexer)
# install component — topo-core-frontend
install(TARGETS TopoLexer
    EXPORT TopoCoreTargets
    COMPONENT topo-core-frontend
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Lexer
    DESTINATION include/topo
    COMPONENT topo-core-frontend
    FILES_MATCHING PATTERN "*.h"
)
