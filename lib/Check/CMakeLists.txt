add_library(TopoCheck
    StageIsolationCheck.cpp
    VisibilityCheck.cpp
    PurityCheck.cpp
    VerificationOrchestrator.cpp
    StubRewriter.cpp
    PruningOrchestrator.cpp
    StubVerificationCheck.cpp
    SymbolExtractor.cpp
    CompletenessCheck.cpp
    CapabilityCatalog.cpp
    ContainmentCheck.cpp
    ImportExtractor.cpp
)
add_library(topo::core::TopoCheck ALIAS TopoCheck)

# TopoCheck depends on core libraries + Platform for subprocess execution.
# Zero LLVM dependency.
target_link_libraries(TopoCheck PUBLIC TopoSema TopoAST TopoAnalysis PRIVATE TopoPlatform)

topo_set_compiler_flags(TopoCheck)


topo_apply_std_pch(TopoCheck)
# install component — topo-core-check
install(TARGETS TopoCheck
    EXPORT TopoCoreTargets
    COMPONENT topo-core-check
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Check
    DESTINATION include/topo
    COMPONENT topo-core-check
    FILES_MATCHING PATTERN "*.h"
)
