#ifndef TOPO_SEMA_SMARTSTRUCTMATCHER_H
#define TOPO_SEMA_SMARTSTRUCTMATCHER_H

#include "topo/AST/ASTNode.h"
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace topo {

struct MatchResult {
    std::vector<std::pair<size_t, size_t>> matches; // (source_idx, target_idx)
    std::set<size_t> unmatchedSource;
    std::set<size_t> unmatchedTarget;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
    bool success = true;
};

class SmartStructMatcher {
public:
    /// Match source return params to target function params.
    /// Rules (by priority):
    /// 1. Type unique -> direct bind
    /// 2. Same type multiple candidates -> name match (warning), else error
    /// 3. All target params matched -> success
    /// 4. Extra source fields -> allowed (partial match)
    static MatchResult match(const std::vector<ReturnParam>& sourceFields, const std::vector<Parameter>& targetParams);
};

} // namespace topo

#endif // TOPO_SEMA_SMARTSTRUCTMATCHER_H
