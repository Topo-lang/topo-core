#ifndef TOPO_CHECK_CALLSITEEXTRACTOR_H
#define TOPO_CHECK_CALLSITEEXTRACTOR_H

#include "topo/Check/ContainmentTypes.h"

#include <string>
#include <vector>

namespace topo::check {

/// Abstract interface for extracting API call sites from host language source files.
/// Each language SDK provides a concrete implementation.
class CallSiteExtractor {
public:
    virtual ~CallSiteExtractor() = default;

    /// Extract call sites from a single source file.
    virtual std::vector<DetectedCallSite> extractCallSites(const std::string& filePath) = 0;
};

} // namespace topo::check

#endif // TOPO_CHECK_CALLSITEEXTRACTOR_H
