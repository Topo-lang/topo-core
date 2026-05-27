#ifndef TOPO_CHECK_CALLEDGEEXTRACTOR_H
#define TOPO_CHECK_CALLEDGEEXTRACTOR_H

#include "topo/Check/CallGraphTypes.h"

#include <string>
#include <vector>

namespace topo::check {

/// Abstract interface for extracting caller → callee edges from host language source files.
/// Each language SDK provides a concrete implementation.
class CallEdgeExtractor {
public:
    virtual ~CallEdgeExtractor() = default;

    /// Extract call edges from a single source file.
    virtual std::vector<CallEdge> extractCallEdges(const std::string& filePath) = 0;
};

} // namespace topo::check

#endif // TOPO_CHECK_CALLEDGEEXTRACTOR_H
