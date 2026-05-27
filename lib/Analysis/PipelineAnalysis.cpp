#include "topo/Analysis/PipelineAnalysis.h"

#include <algorithm>
#include <map>

namespace topo {
namespace analysis {

std::vector<std::pair<int, std::vector<std::string>>> groupNodesByStage(const PipelineAnalysis& analysis) {
    std::map<int, std::vector<std::string>> stageMap;
    for (const auto& [node, stage] : analysis.stages)
        stageMap[stage].push_back(node);

    std::vector<std::pair<int, std::vector<std::string>>> result;
    result.reserve(stageMap.size());
    for (auto& [stage, nodes] : stageMap)
        result.emplace_back(stage, std::move(nodes));

    return result;
}

std::vector<std::string> findUpstreamNodes(const std::string& nodeName, const std::vector<PipelineEdge>& edges) {
    std::vector<std::string> upstream;
    for (const auto& edge : edges) {
        if (edge.target == nodeName) upstream.push_back(edge.source);
    }
    return upstream;
}

bool isSourceNode(const std::string& nodeName, const std::vector<std::string>& sourceNodes) {
    return std::find(sourceNodes.begin(), sourceNodes.end(), nodeName) != sourceNodes.end();
}

std::string resolveNodeCallee(const std::string& nodeName, const std::vector<std::string>& calledFunctions) {
    for (const auto& calledFunc : calledFunctions) {
        // Exact match
        if (calledFunc == nodeName) return calledFunc;

        // Last-component match
        auto pos = calledFunc.rfind("::");
        std::string simpleName = (pos != std::string::npos) ? calledFunc.substr(pos + 2) : calledFunc;
        if (simpleName == nodeName) return calledFunc;

        // Suffix match: calledFunc ends with "::" + nodeName
        std::string suffix = "::" + nodeName;
        if (calledFunc.size() > suffix.size() &&
            calledFunc.compare(calledFunc.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return calledFunc;
        }
    }

    return {};
}

} // namespace analysis
} // namespace topo
