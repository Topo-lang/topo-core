#include "topo/Analysis/StageAnalysis.h"

namespace topo {
namespace analysis {

StageAnalysisResult analyzeStages(const SymbolTable& symbols) {
    StageAnalysisResult result;

    for (const auto& [blockName, block] : symbols.logicBlocks()) {
        result.logicBlockFunctions.insert(blockName);

        // Extract namespace prefix from the block's qualified name
        std::string nsPrefix;
        auto lastSep = blockName.rfind("::");
        if (lastSep != std::string::npos) {
            nsPrefix = blockName.substr(0, lastSep + 2);
        }

        for (size_t i = 0; i < block.calledFunctions.size(); ++i) {
            const auto& callee = block.calledFunctions[i];

            // Skip assignment entries
            if (callee.size() > 8 && callee.substr(0, 8) == "<assign:") {
                continue;
            }

            std::string qualifiedCallee = nsPrefix + callee;
            result.calleeStageMap[qualifiedCallee] = block.stages[i];
        }
    }

    return result;
}

} // namespace analysis
} // namespace topo
