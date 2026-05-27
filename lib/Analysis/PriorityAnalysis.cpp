#include "topo/Analysis/PriorityAnalysis.h"

#include <queue>
#include <set>
#include <vector>

namespace topo {
namespace analysis {

/// Return an integer rank for priority ordering (higher = more urgent).
static int priorityRank(PriorityLevel p) {
    switch (p) {
    case PriorityLevel::Critical: return 4;
    case PriorityLevel::High: return 3;
    case PriorityLevel::Normal: return 2;
    case PriorityLevel::Low: return 1;
    case PriorityLevel::Background: return 0;
    }
    return 2;
}

PriorityAnalysisResult analyzePriority(const SymbolTable& symbols) {
    PriorityAnalysisResult result;

    // Track which functions have explicit priority annotations
    std::set<std::string> explicitlyAnnotated;

    // Step 1: Collect explicit priorities from FunctionSymbol declarations
    for (const auto& [name, fn] : symbols.functions()) {
        result.effectivePriority[name] = fn.priority;
        if (fn.priority != PriorityLevel::Normal) {
            explicitlyAnnotated.insert(name);
        }
    }

    // Step 2: Build caller->callee edges from logic blocks
    // caller qualified name -> list of callee qualified names
    std::unordered_map<std::string, std::vector<std::string>> callGraph;

    for (const auto& [blockName, block] : symbols.logicBlocks()) {
        // Extract namespace prefix
        std::string nsPrefix;
        auto lastSep = blockName.rfind("::");
        if (lastSep != std::string::npos) {
            nsPrefix = blockName.substr(0, lastSep + 2);
        }

        for (const auto& callee : block.calledFunctions) {
            // Skip assignment entries
            if (callee.size() > 8 && callee.substr(0, 8) == "<assign:") {
                continue;
            }
            std::string qualifiedCallee = nsPrefix + callee;
            callGraph[blockName].push_back(qualifiedCallee);
        }
    }

    // Step 3: BFS propagation from high-priority callers downward.
    // If a caller has Critical or High priority, its callees inherit
    // that priority (unless they have an explicit annotation or already
    // have a higher propagated priority).
    std::queue<std::string> worklist;

    // Seed worklist with functions that have elevated priority
    for (const auto& [name, priority] : result.effectivePriority) {
        if (priorityRank(priority) > priorityRank(PriorityLevel::Normal)) {
            worklist.push(name);
        }
    }

    while (!worklist.empty()) {
        std::string caller = worklist.front();
        worklist.pop();

        auto graphIt = callGraph.find(caller);
        if (graphIt == callGraph.end()) continue;

        PriorityLevel callerPri = result.effectivePriority[caller];

        for (const auto& callee : graphIt->second) {
            // Skip if callee has explicit annotation
            if (explicitlyAnnotated.count(callee)) continue;

            auto it = result.effectivePriority.find(callee);
            PriorityLevel currentPri = (it != result.effectivePriority.end()) ? it->second : PriorityLevel::Normal;

            // Propagate if caller has higher priority than callee's current
            if (priorityRank(callerPri) > priorityRank(currentPri)) {
                result.effectivePriority[callee] = callerPri;
                worklist.push(callee);
            }
        }
    }

    return result;
}

} // namespace analysis
} // namespace topo
