add_library(TopoAnalysis
    StageAnalysis.cpp
    PipelineAnalysis.cpp
    VisibilityPolicy.cpp
    PriorityAnalysis.cpp
    LifetimeAnalysis.cpp
    ImportPathCheck.cpp
)
add_library(topo::core::TopoAnalysis ALIAS TopoAnalysis)

# TopoAnalysis depends ONLY on core libraries — zero LLVM dependency.
# Include path topo-core/include/ is provided transitively via TopoSema.
target_link_libraries(TopoAnalysis PUBLIC TopoSema TopoAST)

topo_set_compiler_flags(TopoAnalysis)


topo_apply_std_pch(TopoAnalysis)
# install component — topo-core-check
install(TARGETS TopoAnalysis
    EXPORT TopoCoreTargets
    COMPONENT topo-core-check
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Analysis
    DESTINATION include/topo
    COMPONENT topo-core-check
    FILES_MATCHING PATTERN "*.h"
)
