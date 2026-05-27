#ifndef TOPO_ANALYSIS_PIPELINEANALYSIS_H
#define TOPO_ANALYSIS_PIPELINEANALYSIS_H

#include "topo/Sema/SymbolTable.h"

#include <string>
#include <utility>
#include <vector>

namespace topo {
namespace analysis {

/// Group pipeline DAG nodes by stage and return them sorted ascending.
/// Returns vector of (stage, node-names) pairs.
///
/// This is a pure language-level utility — no LLVM dependency.
std::vector<std::pair<int, std::vector<std::string>>> groupNodesByStage(const PipelineAnalysis& analysis);

/// Find which nodes feed into a given node by examining pipeline edges.
/// Returns the list of source node names for the given target.
std::vector<std::string> findUpstreamNodes(const std::string& nodeName, const std::vector<PipelineEdge>& edges);

/// Check if a node is a source node (no incoming edges).
bool isSourceNode(const std::string& nodeName, const std::vector<std::string>& sourceNodes);

/// Resolve a simple or partially-qualified node name to a fully-qualified
/// callee name using the logic block's calledFunctions list.
/// Tries exact match, last-component match, and suffix match.
/// Returns empty string if no match found.
std::string resolveNodeCallee(const std::string& nodeName, const std::vector<std::string>& calledFunctions);

} // namespace analysis
} // namespace topo

#endif // TOPO_ANALYSIS_PIPELINEANALYSIS_H
