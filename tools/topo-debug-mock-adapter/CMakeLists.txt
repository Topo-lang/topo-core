add_executable(topo-debug-mock-adapter main.cpp)
target_link_libraries(topo-debug-mock-adapter PRIVATE
    TopoDebug
    nlohmann_json::nlohmann_json
)
topo_set_compiler_flags(topo-debug-mock-adapter)

install(TARGETS topo-debug-mock-adapter
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT topo-core-tools
)
