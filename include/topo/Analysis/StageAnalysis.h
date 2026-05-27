#ifndef TOPO_ANALYSIS_STAGEANALYSIS_H
#define TOPO_ANALYSIS_STAGEANALYSIS_H

#include "topo/Sema/SymbolTable.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace topo {
namespace analysis {

/// Result of stage analysis: maps qualified callee names to their stage
/// numbers, extracted from SymbolTable logic blocks.
struct StageAnalysisResult {
    /// Qualified callee name -> stage number.
    /// Built by namespace-qualifying the simple callee names in each
    /// logic block's calledFunctions/stages parallel arrays.
    std::unordered_map<std::string, int> calleeStageMap;

    /// Qualified names of functions that have associated logic blocks.
    std::unordered_set<std::string> logicBlockFunctions;
};

/// Analyze stage ordering from SymbolTable logic blocks.
///
/// Extracts the callee-to-stage mapping by iterating over all logic blocks,
/// qualifying simple callee names with the enclosing namespace, and recording
/// stage numbers. Assignment entries (prefixed with "<assign:") are skipped.
///
/// This is a pure language-level analysis — no LLVM dependency.
StageAnalysisResult analyzeStages(const SymbolTable& symbols);

} // namespace analysis
} // namespace topo

#endif // TOPO_ANALYSIS_STAGEANALYSIS_H
