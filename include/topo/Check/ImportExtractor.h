#ifndef TOPO_CHECK_IMPORTEXTRACTOR_H
#define TOPO_CHECK_IMPORTEXTRACTOR_H

#include "topo/Check/ContainmentTypes.h"

#include <string>
#include <vector>

namespace topo::check {

/// Abstract interface for extracting import statements from host language source files.
/// Each language SDK provides a concrete implementation.
class ImportExtractor {
public:
    virtual ~ImportExtractor() = default;

    /// Extract imports from a single source file.
    virtual std::vector<HostImport> extractImports(const std::string& filePath) = 0;

    /// Extract imports from multiple source files.
    std::vector<HostImport> extractAll(const std::vector<std::string>& sourceFiles);
};

} // namespace topo::check

#endif // TOPO_CHECK_IMPORTEXTRACTOR_H
