#include "topo/Analysis/LifetimeAnalysis.h"

#include <map>

namespace topo {
namespace analysis {

LifetimeAnalysisResult analyzeLifetimes(const SymbolTable& symbols) {
    LifetimeAnalysisResult result;

    // For each logic block, build function -> stage mapping
    // Then resolve lifetime ranges to function sets
    for (const auto& group : symbols.lifetimeGroups()) {
        LifetimeScope scope;
        scope.name = group.name;

        // Find the logic block that contains the start/end functions
        for (const auto& [blockName, block] : symbols.logicBlocks()) {
            std::string nsPrefix;
            auto lastSep = blockName.rfind("::");
            if (lastSep != std::string::npos) {
                nsPrefix = blockName.substr(0, lastSep + 2);
            }

            // Build function -> stage map for this block
            std::map<int, std::vector<std::string>> stageFuncs;
            for (size_t i = 0; i < block.calledFunctions.size(); ++i) {
                if (block.calledFunctions[i].substr(0, 8) == "<assign:") continue;
                int stage = (i < block.stages.size()) ? block.stages[i] : -1;
                const auto& funcName = block.calledFunctions[i];
                // calledFunctions may already be qualified; avoid double-prefix
                std::string qualified =
                    (!nsPrefix.empty() && funcName.find("::") == std::string::npos) ? nsPrefix + funcName : funcName;
                stageFuncs[stage].push_back(qualified);
            }

            // Resolve start/end function stages
            int startStage = -1, endStage = -1;
            for (const auto& [stage, funcs] : stageFuncs) {
                for (const auto& f : funcs) {
                    std::string simpleName = f;
                    auto sep = f.rfind("::");
                    if (sep != std::string::npos) simpleName = f.substr(sep + 2);

                    if (simpleName == group.startFunc) startStage = stage;
                    if (!group.endFunc.empty() && simpleName == group.endFunc) endStage = stage;
                }
            }

            if (startStage < 0) continue; // start func not in this block

            if (group.isSingleFunc) {
                endStage = startStage;
            } else if (group.isOpenEnded) {
                // Open-ended: cover from start to the last stage
                if (!stageFuncs.empty()) endStage = stageFuncs.rbegin()->first;
            }

            // Collect all functions in the range [startStage, endStage]
            for (const auto& [stage, funcs] : stageFuncs) {
                if (stage >= startStage && (endStage < 0 || stage <= endStage)) {
                    for (const auto& f : funcs) {
                        scope.coveredFunctions.insert(f);
                    }
                }
            }
        }

        result.scopes[group.name] = std::move(scope);
    }

    // Identify functions with lifetime-annotated types
    for (const auto& [name, func] : symbols.functions()) {
        // Check return type
        if (!func.returnType.nameParts.empty() && func.returnType.nameParts[0] == "lifetime") {
            result.lifetimeAnnotatedFunctions.insert(name);
        }
        // Check parameters
        for (const auto& param : func.params) {
            if (!param.type.nameParts.empty() && param.type.nameParts[0] == "lifetime") {
                result.lifetimeAnnotatedFunctions.insert(name);
                break;
            }
        }
    }

    return result;
}

} // namespace analysis
} // namespace topo
