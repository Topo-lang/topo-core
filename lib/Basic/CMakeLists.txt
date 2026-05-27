add_library(TopoBasic
    Diagnostic.cpp
)
add_library(topo::core::TopoBasic ALIAS TopoBasic)

target_include_directories(TopoBasic PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

topo_set_compiler_flags(TopoBasic)


topo_apply_std_pch(TopoBasic)
# install component — topo-core-frontend
install(TARGETS TopoBasic
    EXPORT TopoCoreTargets
    COMPONENT topo-core-frontend
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Basic
    DESTINATION include/topo
    COMPONENT topo-core-frontend
    FILES_MATCHING PATTERN "*.h"
)
