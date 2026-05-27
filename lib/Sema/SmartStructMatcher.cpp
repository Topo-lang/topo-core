#include "topo/Sema/SmartStructMatcher.h"
#include <unordered_map>

namespace topo {

MatchResult SmartStructMatcher::match(const std::vector<ReturnParam>& sourceFields,
                                      const std::vector<Parameter>& targetParams) {
    MatchResult result;

    // Initialize unmatched sets
    for (size_t i = 0; i < sourceFields.size(); ++i)
        result.unmatchedSource.insert(i);
    for (size_t i = 0; i < targetParams.size(); ++i)
        result.unmatchedTarget.insert(i);

    // Build type -> indices maps
    std::unordered_map<std::string, std::vector<size_t>> sourceByType;
    std::unordered_map<std::string, std::vector<size_t>> targetByType;

    for (size_t i = 0; i < sourceFields.size(); ++i)
        sourceByType[sourceFields[i].type.toString()].push_back(i);
    for (size_t i = 0; i < targetParams.size(); ++i)
        targetByType[targetParams[i].type.toString()].push_back(i);

    // For each target param, try to find a matching source
    for (size_t ti = 0; ti < targetParams.size(); ++ti) {
        if (result.unmatchedTarget.find(ti) == result.unmatchedTarget.end()) continue;

        const auto& targetType = targetParams[ti].type.toString();
        const auto& targetName = targetParams[ti].name;

        auto srcIt = sourceByType.find(targetType);
        if (srcIt == sourceByType.end() || srcIt->second.empty()) {
            // No source with matching type
            result.errors.push_back("target parameter '" + targetName + "' (type " + targetType +
                                    ") has no matching source field");
            result.success = false;
            continue;
        }

        // Collect available (unmatched) source indices of this type
        std::vector<size_t> available;
        for (size_t si : srcIt->second) {
            if (result.unmatchedSource.count(si)) available.push_back(si);
        }

        if (available.empty()) {
            result.errors.push_back("target parameter '" + targetName + "' (type " + targetType +
                                    ") — all matching source fields already consumed");
            result.success = false;
            continue;
        }

        if (available.size() == 1) {
            // Type unique — direct bind
            size_t si = available[0];
            result.matches.emplace_back(si, ti);
            result.unmatchedSource.erase(si);
            result.unmatchedTarget.erase(ti);
        } else {
            // Multiple candidates — try name match
            size_t nameMatch = SIZE_MAX;
            for (size_t si : available) {
                if (sourceFields[si].name == targetName) {
                    nameMatch = si;
                    break;
                }
            }
            if (nameMatch != SIZE_MAX) {
                result.matches.emplace_back(nameMatch, ti);
                result.unmatchedSource.erase(nameMatch);
                result.unmatchedTarget.erase(ti);
                result.warnings.push_back("target parameter '" + targetName +
                                          "' matched by name (multiple type candidates)");
            } else {
                result.errors.push_back("target parameter '" + targetName + "' (type " + targetType +
                                        ") has multiple source candidates but none match by name");
                result.success = false;
            }
        }
    }

    return result;
}

} // namespace topo
