add_library(TopoSema
    SymbolTable.cpp
    TypeRegistry.cpp
    TypeResolver.cpp
    TypeBinder.cpp
    SemanticAnalyzer.cpp
    ImportResolver.cpp
    SmartStructMatcher.cpp
    VisibilityCollector.cpp
)

target_link_libraries(TopoSema PUBLIC TopoAST TopoParser TopoStdlib)

topo_set_compiler_flags(TopoSema)


topo_apply_std_pch(TopoSema)
# install component — topo-core-frontend
install(TARGETS TopoSema
    EXPORT TopoCoreTargets
    COMPONENT topo-core-frontend
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Sema
    DESTINATION include/topo
    COMPONENT topo-core-frontend
    FILES_MATCHING PATTERN "*.h"
)
