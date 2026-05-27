#ifndef TOPO_CHECK_COMPLETENESSCHECK_H
#define TOPO_CHECK_COMPLETENESSCHECK_H

#include "topo/Check/CheckTypes.h"
#include "topo/Check/SymbolExtractor.h"
#include "topo/Sema/SymbolTable.h"
#include "topo/Sema/VisibilityCollector.h"

#include <string>
#include <vector>

namespace topo::check {

/// Configuration for completeness checking.
struct CompletenessConfig {
    bool ignoreConstructors = false;
    bool ignoreDestructors = false;
    bool ignoreMain = true;
    std::vector<std::string> ignorePatterns; // glob patterns, e.g. "test::*"
};

/// Run completeness check: compare host symbols against .topo declarations.
///
/// Three passes:
///   1. Undeclared symbols: host code has it, .topo doesn't -> Error
///   2. Dangling declarations: .topo has it, host code doesn't -> Warning
///   3. Visibility mismatch: both have it, but visibility contradicts -> Error/Warning
void checkCompleteness(const std::vector<HostSymbol>& hostSymbols,
                       const SymbolTable& symbols,
                       const std::vector<VisibilityEntry>& visEntries,
                       const CompletenessConfig& config,
                       CheckResult& result);

} // namespace topo::check

#endif // TOPO_CHECK_COMPLETENESSCHECK_H
