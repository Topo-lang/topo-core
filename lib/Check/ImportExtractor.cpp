#include "topo/Check/ImportExtractor.h"

namespace topo::check {

std::vector<HostImport> ImportExtractor::extractAll(const std::vector<std::string>& sourceFiles) {
    std::vector<HostImport> result;
    for (const auto& file : sourceFiles) {
        auto imports = extractImports(file);
        result.insert(result.end(), std::make_move_iterator(imports.begin()), std::make_move_iterator(imports.end()));
    }
    return result;
}

} // namespace topo::check
