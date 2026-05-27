#include "topo/Check/StageIsolationCheck.h"

#include <unordered_map>
#include <unordered_set>

namespace topo::check {

void checkStageIsolation(const SymbolTable& symbols,
                         const std::vector<CallEdge>& callEdges,
                         CheckResult& result) {
    // Build callee -> stage mapping from logic blocks.
    // A callee's stage is the stage number at which it appears in a logic block.
    std::unordered_map<std::string, int> calleeStage;

    for (const auto& [blockName, block] : symbols.logicBlocks()) {
        for (size_t i = 0; i < block.calledFunctions.size(); ++i) {
            int stage = (i < block.stages.size()) ? block.stages[i] : 0;
            calleeStage[block.calledFunctions[i]] = stage;
        }
    }

    if (calleeStage.empty()) return;

    // Build callee -> set of callees it transitively invokes (from host call graph)
    std::unordered_map<std::string, std::unordered_set<std::string>> callGraph;
    for (const auto& edge : callEdges) {
        callGraph[edge.caller].insert(edge.callee);
    }

    // For each logic block, verify stage ordering:
    // A function at stage N should not call a function that only appears at stage N+1.
    for (const auto& [blockName, block] : symbols.logicBlocks()) {
        for (size_t i = 0; i < block.calledFunctions.size(); ++i) {
            const auto& callee = block.calledFunctions[i];
            int stage = (i < block.stages.size()) ? block.stages[i] : 0;

            // Check direct host-level calls from this callee
            auto it = callGraph.find(callee);
            if (it == callGraph.end()) continue;

            for (const auto& target : it->second) {
                auto stageIt = calleeStage.find(target);
                if (stageIt == calleeStage.end()) continue;

                if (stageIt->second > stage) {
                    CheckDiagnostic diag;
                    diag.severity = Severity::Error;
                    diag.check = "stage-isolation";
                    diag.message = "function '" + callee + "' at stage " + std::to_string(stage) + " calls '" + target +
                                   "' which belongs to later stage " + std::to_string(stageIt->second);
                    // Try to find location from call edges
                    for (const auto& edge : callEdges) {
                        if (edge.caller == callee && edge.callee == target) {
                            diag.file = edge.file;
                            diag.line = edge.line;
                            break;
                        }
                    }
                    result.diagnostics.push_back(std::move(diag));
                    result.errorCount++;
                }
            }
        }
    }
}

} // namespace topo::check
