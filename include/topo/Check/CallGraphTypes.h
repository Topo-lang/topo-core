#ifndef TOPO_CHECK_CALLGRAPHTYPES_H
#define TOPO_CHECK_CALLGRAPHTYPES_H

#include <string>

namespace topo::check {

/// Caller → callee edge extracted from host language source.
/// Used by StageIsolationCheck and VisibilityCheck.
struct CallEdge {
    std::string caller;  // qualified name
    std::string callee;  // qualified name
    std::string file;
    int line = 0;
};

/// Function-level access to a global or shared-state symbol.
/// Used by PurityCheck to verify parallel-stage purity.
struct SymbolAccess {
    std::string function;  // qualified name of the accessing function
    std::string symbol;    // qualified name of the accessed global
    bool isWrite = false;
    std::string file;
    int line = 0;
};

} // namespace topo::check

#endif // TOPO_CHECK_CALLGRAPHTYPES_H
