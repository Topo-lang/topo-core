add_library(TopoStdlib
    Types.cpp
)

target_link_libraries(TopoStdlib PUBLIC TopoBasic)

topo_set_compiler_flags(TopoStdlib)


topo_apply_std_pch(TopoStdlib)
# install component — topo-core-frontend
install(TARGETS TopoStdlib
    EXPORT TopoCoreTargets
    COMPONENT topo-core-frontend
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Stdlib
    DESTINATION include/topo
    COMPONENT topo-core-frontend
    FILES_MATCHING PATTERN "*.h"
)
