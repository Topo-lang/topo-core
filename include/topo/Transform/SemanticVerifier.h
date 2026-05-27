#ifndef TOPO_TRANSFORM_SEMANTICVERIFIER_H
#define TOPO_TRANSFORM_SEMANTICVERIFIER_H

#include "topo/Sema/SymbolTable.h"
#include <string>
#include <vector>

namespace topo::transform {

struct VerificationResult {
    bool equivalent = true;
    std::vector<std::string> differences;
};

class SemanticVerifier {
public:
    /// Compare two SymbolTables for structural equivalence.
    /// Returns a result indicating whether the tables are equivalent,
    /// with human-readable difference descriptions if they are not.
    VerificationResult verify(const SymbolTable& original, const SymbolTable& transformed);

private:
    void compareFunctions(const SymbolTable& a, const SymbolTable& b, std::vector<std::string>& diffs);
    void compareClasses(const SymbolTable& a, const SymbolTable& b, std::vector<std::string>& diffs);
    void compareLogicBlocks(const SymbolTable& a, const SymbolTable& b, std::vector<std::string>& diffs);
    void compareConstraints(const SymbolTable& a, const SymbolTable& b, std::vector<std::string>& diffs);
};

} // namespace topo::transform

#endif // TOPO_TRANSFORM_SEMANTICVERIFIER_H
