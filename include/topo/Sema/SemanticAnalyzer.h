#ifndef TOPO_SEMA_SEMANTICANALYZER_H
#define TOPO_SEMA_SEMANTICANALYZER_H

#include "topo/AST/ASTNode.h"
#include "topo/Basic/Diagnostic.h"
#include "topo/Sema/SymbolTable.h"

namespace topo {

class TypeResolver;

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(DiagnosticEngine& diag);

    SymbolTable analyze(const TopoFile& root);
    SymbolTable analyze(const TopoFile& root, const SymbolTable& importedSymbols);

private:
    // Pass 1: Collect all declarations into the SymbolTable
    void collectDeclarations(const TopoFile& root);
    void visitNamespace(const NamespaceDecl& ns, const std::string& parentPath, Visibility currentVis);
    void visitSection(const VisibilitySection& section, const std::string& nsPath, bool forceInternal = false);
    void visitMember(const ASTNode& member, const std::string& nsPath, Visibility visibility);
    void visitTypeDecl(const TypeDecl& typeNode, const std::string& nsPath, Visibility visibility);
    void visitConstraintTypeDecl(const TypeDecl& constraint, const std::string& nsPath);
    void visitAdaptTypeDecl(const TypeDecl& adapt, const std::string& nsPath);
    void visitInstantiateDataDecl(const DataDecl& inst, const std::string& nsPath);

    // Pass 2: Validate all types
    void validateTypes();
    void validateTemplateArgs(const TypeNode& type, const TypeResolver& resolver);
    void validateOwnershipRules(const TypeNode& type, const std::string& context, const SourceLocation& loc);
    void validateOwnershipGraph();

    // Pass 3: Validate fn blocks
    void validateLogicBlocks();

    void validatePipeline(const std::string& blockName, LogicBlockEntry& block);

    std::string resolveCallee(const std::string& callee, const std::string& nsPrefix) const;

    static std::string normalizeName(const std::string& dottedName);

    DiagnosticEngine& diag_;
    SymbolTable symbols_;
};

} // namespace topo

#endif // TOPO_SEMA_SEMANTICANALYZER_H
