add_executable(topo-fmt
    main.cpp
)

# TopoTransform PUBLIC-propagates TopoSema → TopoParser.
target_link_libraries(topo-fmt PRIVATE TopoFormat TopoTransform)

topo_set_compiler_flags(topo-fmt)

install(TARGETS topo-fmt
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT topo-core-tools
)
