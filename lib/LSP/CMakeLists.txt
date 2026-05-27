add_library(TopoLSPBridge
    LSPBridge.cpp
)

target_include_directories(TopoLSPBridge PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(TopoLSPBridge PUBLIC TopoPlatform nlohmann_json::nlohmann_json)

topo_set_compiler_flags(TopoLSPBridge)
topo_apply_std_pch(TopoLSPBridge)

# install component — topo-core-lsp
install(TARGETS TopoLSPBridge
    EXPORT TopoCoreTargets
    COMPONENT topo-core-lsp
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/LSP
    DESTINATION include/topo
    COMPONENT topo-core-lsp
    FILES_MATCHING PATTERN "*.h"
)
