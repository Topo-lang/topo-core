#ifndef TOPO_CHECK_PURITYCHECK_H
#define TOPO_CHECK_PURITYCHECK_H

#include "topo/Check/CallGraphTypes.h"
#include "topo/Check/CheckTypes.h"
#include "topo/Sema/SymbolTable.h"

namespace topo::check {

/// Verify that functions in parallel stages only depend on their parameters
/// (no global state access, no shared mutable state).
/// This is the safety guarantee for loop-auto-parallel.
void checkPurity(const SymbolTable& symbols,
                 const std::vector<SymbolAccess>& accesses,
                 CheckResult& result);

} // namespace topo::check

#endif // TOPO_CHECK_PURITYCHECK_H
