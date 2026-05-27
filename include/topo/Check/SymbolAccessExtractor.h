#ifndef TOPO_CHECK_SYMBOLACCESSEXTRACTOR_H
#define TOPO_CHECK_SYMBOLACCESSEXTRACTOR_H

#include "topo/Check/CallGraphTypes.h"

#include <string>
#include <vector>

namespace topo::check {

/// Abstract interface for extracting global/shared-state symbol accesses
/// (reads and writes) from host language source files.
/// Each language SDK provides a concrete implementation.
class SymbolAccessExtractor {
public:
    virtual ~SymbolAccessExtractor() = default;

    /// Extract symbol accesses from a single source file.
    virtual std::vector<SymbolAccess> extractSymbolAccesses(const std::string& filePath) = 0;
};

} // namespace topo::check

#endif // TOPO_CHECK_SYMBOLACCESSEXTRACTOR_H
