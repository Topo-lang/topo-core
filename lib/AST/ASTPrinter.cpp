#include "topo/AST/ASTPrinter.h"

namespace topo {

void ASTPrinter::print(const ASTNode& node) {
    printNode(node, 0);
}

void ASTPrinter::printIndent(int indent) {
    for (int i = 0; i < indent; ++i) {
        os_ << "  ";
    }
}

void ASTPrinter::printNode(const ASTNode& node, int indent) {
    switch (node.kind) {
    case ASTKind::TopoFile: printTopoFile(static_cast<const TopoFile&>(node), indent); break;
    case ASTKind::Import: printImport(static_cast<const Import&>(node), indent); break;
    case ASTKind::DataDecl: printDataDecl(static_cast<const DataDecl&>(node), indent); break;
    case ASTKind::NamespaceDecl: printNamespaceDecl(static_cast<const NamespaceDecl&>(node), indent); break;
    case ASTKind::VisibilitySection: printVisibilitySection(static_cast<const VisibilitySection&>(node), indent); break;
    case ASTKind::FnDecl: printFnDecl(static_cast<const FnDecl&>(node), indent); break;
    case ASTKind::FnLogicBlock: printFnLogicBlock(static_cast<const FnLogicBlock&>(node), indent); break;
    case ASTKind::OperationDecl: printOperationDecl(static_cast<const OperationDecl&>(node), indent); break;
    case ASTKind::TypeDecl: printTypeDecl(static_cast<const TypeDecl&>(node), indent); break;
    case ASTKind::IfDecl: printIfDecl(static_cast<const IfDecl&>(node), indent); break;
    case ASTKind::DebugDecl: printDebugDecl(static_cast<const DebugDecl&>(node), indent); break;
    case ASTKind::BinaryExpr:
    case ASTKind::UnaryExpr:
    case ASTKind::LiteralExpr:
    case ASTKind::IdentifierExpr:
    case ASTKind::CallExpr:
        printIndent(indent);
        os_ << "Expr\n";
        break;
    }
}

void ASTPrinter::printTopoFile(const TopoFile& node, int indent) {
    printIndent(indent);
    os_ << "TopoFile\n";
    for (const auto& decl : node.declarations) {
        printNode(*decl, indent + 1);
    }
}

void ASTPrinter::printImport(const Import& node, int indent) {
    printIndent(indent);
    os_ << "Import '" << node.path << ".topo'";
    if (!node.selectedSymbols.empty()) {
        os_ << " { ";
        for (size_t i = 0; i < node.selectedSymbols.size(); ++i) {
            if (i > 0) os_ << ", ";
            os_ << node.selectedSymbols[i];
        }
        os_ << " }";
    }
    os_ << "\n";
}

void ASTPrinter::printDataDecl(const DataDecl& node, int indent) {
    printIndent(indent);
    if (node.isStdImport()) {
        os_ << "StdImportDecl \"" << node.importPath << "\"";
        if (!node.name.empty()) os_ << " -> " << node.name;
        os_ << "\n";
    } else if (node.isLifetime()) {
        os_ << "LifetimeDecl \"" << node.name << "\" = " << node.startFunc;
        if (!node.endFunc.empty())
            os_ << ".." << node.endFunc;
        else if (node.isOpenEnded)
            os_ << "..";
        os_ << "\n";
    } else if (node.isInstantiate()) {
        os_ << "InstantiateDecl " << node.type.toString() << "\n";
    } else if (!node.type.nameParts.empty() && !node.name.empty()) {
        // Could be type alias or member variable
        // If type has a name and there's a name, check context
        // For now, print as TypeAliasDecl if type looks like a binding
        os_ << "DataDecl '" << node.name << "' = " << node.type.toString() << "\n";
    } else if (!node.name.empty()) {
        os_ << "DataDecl '" << node.name << "'\n";
    } else {
        os_ << "DataDecl\n";
    }
}

void ASTPrinter::printNamespaceDecl(const NamespaceDecl& node, int indent) {
    printIndent(indent);
    os_ << "NamespaceDecl '" << node.pathString() << "'";
    if (node.isInternal) os_ << " [internal]";
    os_ << "\n";
    for (const auto& section : node.sections) {
        printNode(*section, indent + 1);
    }
}

void ASTPrinter::printVisibilitySection(const VisibilitySection& node, int indent) {
    printIndent(indent);
    os_ << "VisibilitySection '" << visibilityName(node.visibility) << "'\n";
    for (const auto& member : node.members) {
        printNode(*member, indent + 1);
    }
}

void ASTPrinter::printFnDecl(const FnDecl& node, int indent) {
    printIndent(indent);
    if (node.isConstructor) {
        os_ << "ConstructorDecl '" << node.signature() << "'\n";
    } else if (node.isDestructor) {
        os_ << "DestructorDecl '" << node.signature() << "'\n";
    } else if (node.isOperator()) {
        os_ << "OperatorDecl " << node.signature() << "\n";
    } else if (node.isComptime()) {
        os_ << "ComptimeFn \"" << node.name << "\" -> " << node.returnType.toString() << "\n";
    } else if (node.hasModifier(ModifierData::Handler)) {
        // Surfaced distinctly so the `handler` keyword survives a
        // parse → print round-trip and is never re-rendered as a plain
        // function declaration.
        os_ << "HandlerDecl '" << node.signature() << "'\n";
    } else {
        os_ << "FunctionDecl '" << node.signature() << "'\n";
    }
}

void ASTPrinter::printFnLogicBlock(const FnLogicBlock& node, int indent) {
    printIndent(indent);
    if (node.isPipeline()) {
        // A flow round-trips as a flow, not as an anonymous pipeline block.
        os_ << (node.hasModifier(ModifierData::Flow) ? "FlowBlock '" : "PipelineBlock '") << node.name << "'\n";
        for (const auto& edge : node.pipelineEdges) {
            printIndent(indent + 1);
            os_ << "Edge " << edge.source << " -> ";
            if (edge.isTerminal) {
                os_ << edge.terminalType << " [terminal]";
            } else {
                os_ << edge.target;
            }
            os_ << "\n";
        }
    } else {
        os_ << "FunctionLogicBlock '" << node.name << "'\n";
        for (const auto& op : node.operations) {
            printNode(*op, indent + 1);
        }
    }
}

void ASTPrinter::printOperationDecl(const OperationDecl& node, int indent) {
    printIndent(indent);
    os_ << "OperationDecl " << node.description() << "\n";
}

void ASTPrinter::printTypeDecl(const TypeDecl& node, int indent) {
    printIndent(indent);
    if (node.isConstraint()) {
        os_ << "ConstraintDecl \"" << node.name << "\"";
        if (node.parentConstraint) os_ << " : " << *node.parentConstraint;
        os_ << "\n";
        for (const auto& m : node.constraintMembers) {
            printIndent(indent + 1);
            os_ << (m.isFunction ? "FunctionReq" : "MemberReq") << " " << m.type.toString() << " " << m.name;
            if (m.isFunction) {
                os_ << "(";
                for (size_t i = 0; i < m.params.size(); ++i) {
                    if (i > 0) os_ << ", ";
                    os_ << m.params[i].toString();
                }
                os_ << ")";
            }
            os_ << "\n";
        }
    } else if (node.isAdapt()) {
        os_ << "AdaptDecl \"" << node.adaptConstraintName << "\" for " << node.adaptTargetType.toString() << "\n";
        for (const auto& m : node.adaptMappings) {
            printIndent(indent + 1);
            os_ << m.memberName << " = " << m.targetName << "\n";
        }
    } else if (node.isTypeFn()) {
        os_ << "TypeFn \"" << node.name << "\" match " << node.matchTarget << " (" << node.matchArms.size()
            << " arms)\n";
    } else {
        os_ << "TypeDecl '" << node.name << "'";
        if (node.baseClass) {
            os_ << " : public " << node.baseClass->toString();
        }
        os_ << "\n";
        for (const auto& section : node.sections) {
            printNode(*section, indent + 1);
        }
    }
}

void ASTPrinter::printIfDecl(const IfDecl& node, int indent) {
    printIndent(indent);
    os_ << "IfDecl";
    if (node.hasModifier(ModifierData::Comptime)) os_ << " [comptime]";
    os_ << "\n";
    printIndent(indent + 1);
    os_ << "Then: " << node.thenBody.size() << " members\n";
    if (!node.elseBody.empty()) {
        printIndent(indent + 1);
        os_ << "Else: " << node.elseBody.size() << " members\n";
    }
}

void ASTPrinter::printDebugDecl(const DebugDecl& node, int indent) {
    printIndent(indent);
    os_ << "DebugDecl '" << node.targetTypeName << "'\n";
    for (const auto& v : node.views) {
        printIndent(indent + 1);
        os_ << "view " << v.name << " = " << v.slice.toString() << "\n";
    }
    if (node.summaryTemplate) {
        printIndent(indent + 1);
        os_ << "summary \"" << *node.summaryTemplate << "\"\n";
    }
    for (const auto& r : node.inactiveRegions) {
        printIndent(indent + 1);
        os_ << "inactive_region " << r.region.toString() << " [" << debugInactiveModeName(r.mode) << "]\n";
    }
    for (const auto& rd : node.renderDecls) {
        printIndent(indent + 1);
        os_ << "render method=" << (rd.method.empty() ? "?" : rd.method) << " (" << rd.rawBody.size()
            << " bytes raw)\n";
    }
}

} // namespace topo
