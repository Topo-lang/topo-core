add_library(TopoFormat
    Formatter.cpp
)

target_link_libraries(TopoFormat PUBLIC TopoAST TopoLexer)

topo_set_compiler_flags(TopoFormat)


topo_apply_std_pch(TopoFormat)
# install component — topo-core-fmt
install(TARGETS TopoFormat
    EXPORT TopoCoreTargets
    COMPONENT topo-core-fmt
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Format
    DESTINATION include/topo
    COMPONENT topo-core-fmt
    FILES_MATCHING PATTERN "*.h"
)
