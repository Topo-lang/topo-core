add_executable(topo
    main.cpp
)

# topo's main.cpp uses only the frontend (Lexer/Parser/Sema/AST) plus
# Analysis::ImportPathCheck — it calls no Check-subsystem symbol. TopoAnalysis
# PUBLIC-propagates TopoSema → TopoParser → TopoLexer and pulls TopoAST, so it
# supplies the full frontend transitively. Linking TopoCheck dragged in the
# whole check subsystem (Containment/Visibility/Purity/... + SymbolExtractor)
# that this CLI never references.
# TopoDistribution backs the `topo backend` subcommand.
target_link_libraries(topo PRIVATE TopoAnalysis TopoDistribution)

topo_set_compiler_flags(topo)

install(TARGETS topo
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT topo-core-tools
)
