#ifndef TOPO_ANALYSIS_PRIORITYANALYSIS_H
#define TOPO_ANALYSIS_PRIORITYANALYSIS_H

#include "topo/Sema/SymbolTable.h"

#include <string>
#include <unordered_map>

namespace topo {
namespace analysis {

struct PriorityAnalysisResult {
    /// Qualified function name -> effective priority level.
    /// Explicit annotations always override propagation.
    std::unordered_map<std::string, PriorityLevel> effectivePriority;
};

/// Analyze priority assignments from SymbolTable.
///
/// 1. Read explicit priorities from FunctionSymbol
/// 2. Build call graph from LogicBlockEntry.calledFunctions
/// 3. Propagate from critical/high callers downward (callees inherit
///    the highest priority of their callers, unless explicitly annotated)
/// 4. Explicit annotations always override propagation
///
/// This is a pure language-level analysis — no LLVM dependency.
PriorityAnalysisResult analyzePriority(const SymbolTable& symbols);

} // namespace analysis
} // namespace topo

#endif // TOPO_ANALYSIS_PRIORITYANALYSIS_H
