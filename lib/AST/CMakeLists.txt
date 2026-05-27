add_library(TopoAST
    ASTPrinter.cpp
)

target_link_libraries(TopoAST PUBLIC TopoBasic TopoStdlib)

topo_set_compiler_flags(TopoAST)


topo_apply_std_pch(TopoAST)
# install component — topo-core-frontend
install(TARGETS TopoAST
    EXPORT TopoCoreTargets
    COMPONENT topo-core-frontend
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/AST
    DESTINATION include/topo
    COMPONENT topo-core-frontend
    FILES_MATCHING PATTERN "*.h"
)
