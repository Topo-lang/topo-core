#ifndef TOPO_SEMA_TYPERESOLVER_H
#define TOPO_SEMA_TYPERESOLVER_H

#include "topo/AST/ASTNode.h"
#include "topo/Sema/SymbolTable.h"
#include "topo/Sema/TypeRegistry.h"
#include <set>
#include <string>

namespace topo {

class TypeResolver {
public:
    explicit TypeResolver(const SymbolTable& symbols);
    TypeResolver(const SymbolTable& symbols, const TypeRegistry& registry);

    // Returns true if the type is valid. On false, 'reason' describes why.
    bool isValidType(const TypeNode& type, std::string& reason) const;

    // Add template parameter names that should be treated as valid types
    void addTemplateParamNames(const std::vector<TemplateParamDecl>& params);
    void clearTemplateParamNames();

    const TypeRegistry& registry() const { return registry_; }

private:
    const SymbolTable& symbols_;
    TypeRegistry registry_;
    std::set<std::string> templateParamNames_;
};

} // namespace topo

#endif // TOPO_SEMA_TYPERESOLVER_H
