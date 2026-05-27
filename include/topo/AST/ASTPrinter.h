#ifndef TOPO_AST_ASTPRINTER_H
#define TOPO_AST_ASTPRINTER_H

#include "topo/AST/ASTNode.h"
#include <ostream>

namespace topo {

class ASTPrinter {
public:
    explicit ASTPrinter(std::ostream& os) : os_(os) {}

    void print(const ASTNode& node);

private:
    void printNode(const ASTNode& node, int indent);
    void printIndent(int indent);

    void printTopoFile(const TopoFile& node, int indent);
    void printImport(const Import& node, int indent);
    void printDataDecl(const DataDecl& node, int indent);
    void printNamespaceDecl(const NamespaceDecl& node, int indent);
    void printVisibilitySection(const VisibilitySection& node, int indent);
    void printFnDecl(const FnDecl& node, int indent);
    void printFnLogicBlock(const FnLogicBlock& node, int indent);
    void printOperationDecl(const OperationDecl& node, int indent);
    void printTypeDecl(const TypeDecl& node, int indent);
    void printIfDecl(const IfDecl& node, int indent);
    void printDebugDecl(const DebugDecl& node, int indent);

    std::ostream& os_;
};

} // namespace topo

#endif // TOPO_AST_ASTPRINTER_H
