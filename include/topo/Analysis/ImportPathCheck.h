#ifndef TOPO_ANALYSIS_IMPORTPATHCHECK_H
#define TOPO_ANALYSIS_IMPORTPATHCHECK_H

#include "topo/Check/CheckTypes.h"
#include "topo/Basic/HostLanguage.h"
#include "topo/Sema/SymbolTable.h"

#include <string>
#include <vector>

namespace topo::analysis {

/// Configuration for std::import path validation.
struct ImportPathConfig {
    std::string projectDir;              // project base directory
    std::vector<std::string> searchDirs; // include dirs (C++) or source roots
    HostLanguage language = HostLanguage::Cpp;
    bool warnOnly = false; // true = warnings instead of errors
};

/// Validate that all std::import paths in the symbol table resolve to existing files.
void checkImportPaths(const SymbolTable& symbols, const ImportPathConfig& config, check::CheckResult& result);

} // namespace topo::analysis

#endif // TOPO_ANALYSIS_IMPORTPATHCHECK_H
