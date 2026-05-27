#ifndef TOPO_CHECK_STAGEISOLATIONCHECK_H
#define TOPO_CHECK_STAGEISOLATIONCHECK_H

#include "topo/Check/CallGraphTypes.h"
#include "topo/Check/CheckTypes.h"
#include "topo/Sema/SymbolTable.h"

namespace topo::check {

/// Verify that stage N does not depend on stage N+1 outputs.
/// Uses call graph from host language to trace actual data flow.
void checkStageIsolation(const SymbolTable& symbols,
                         const std::vector<CallEdge>& callEdges,
                         CheckResult& result);

} // namespace topo::check

#endif // TOPO_CHECK_STAGEISOLATIONCHECK_H
