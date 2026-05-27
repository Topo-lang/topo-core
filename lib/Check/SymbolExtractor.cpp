#include "topo/Check/SymbolExtractor.h"

namespace topo::check {

std::vector<HostSymbol> SymbolExtractor::extractAll(const std::vector<std::string>& sourceFiles) {
    std::vector<HostSymbol> result;
    for (const auto& file : sourceFiles) {
        auto symbols = extractSymbols(file);
        result.insert(result.end(), std::make_move_iterator(symbols.begin()), std::make_move_iterator(symbols.end()));
    }
    return result;
}

} // namespace topo::check
