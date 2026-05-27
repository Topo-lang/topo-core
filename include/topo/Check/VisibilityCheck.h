#ifndef TOPO_CHECK_VISIBILITYCHECK_H
#define TOPO_CHECK_VISIBILITYCHECK_H

#include "topo/Check/CallGraphTypes.h"
#include "topo/Check/CheckTypes.h"
#include "topo/Sema/SymbolTable.h"
#include "topo/Sema/VisibilityCollector.h"

namespace topo::check {

/// Verify that private/internal functions are not called from outside their scope.
/// Uses call graph from host language to detect violations.
void checkVisibilityConsistency(const SymbolTable& symbols,
                                const std::vector<VisibilityEntry>& visEntries,
                                const std::vector<CallEdge>& callEdges,
                                CheckResult& result);

} // namespace topo::check

#endif // TOPO_CHECK_VISIBILITYCHECK_H
