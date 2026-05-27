#ifndef TOPO_ANALYSIS_LIFETIMEANALYSIS_H
#define TOPO_ANALYSIS_LIFETIMEANALYSIS_H

#include "topo/Sema/SymbolTable.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace topo {
namespace analysis {

/// Maps a lifetime group name to the set of functions it spans.
struct LifetimeScope {
    std::string name;
    std::unordered_set<std::string> coveredFunctions; // qualified names
};

/// Result of lifetime analysis: resolves lifetime group declarations
/// into concrete function sets using stage ordering from SymbolTable.
struct LifetimeAnalysisResult {
    /// Group name -> resolved scope (functions covered by the lifetime)
    std::unordered_map<std::string, LifetimeScope> scopes;

    /// Functions that return or accept lifetime-annotated types
    std::unordered_set<std::string> lifetimeAnnotatedFunctions;
};

/// Analyze lifetime scoping from SymbolTable.
///
/// Resolves lifetime group declarations (e.g., `lifetime frame = create..render`)
/// into the set of functions they cover, using fn block stage ordering to
/// determine which functions fall within the range.
///
/// This is a pure language-level analysis — no LLVM dependency.
LifetimeAnalysisResult analyzeLifetimes(const SymbolTable& symbols);

} // namespace analysis
} // namespace topo

#endif // TOPO_ANALYSIS_LIFETIMEANALYSIS_H
