#ifndef TOPO_TRANSPILE_EMISSIONVALIDATION_H
#define TOPO_TRANSPILE_EMISSIONVALIDATION_H

#include "topo/Transpile/Emitter.h"
#include "topo/Transpile/TranspileTraversal.h"

namespace topo::transpile {

inline std::vector<EmitError> validateEmission(const TranspileModule& module,
                                             bool supportsJavaMethodReferences = false) {
    std::vector<EmitError> errors;
    visitModuleExprs(module, [&](const Expr& expr) {
        std::string location;
        if (expr.sourceSpan) location = expr.sourceSpan->file + ":" + std::to_string(expr.sourceSpan->startLine);
        if (expr.kind() == Expr::Kind::MethodReference) {
            const auto& ref = static_cast<const MethodReferenceExpr&>(expr);
            if (!supportsJavaMethodReferences) {
                errors.push_back({"methodref", "Target cannot preserve method-reference binding and receiver evaluation semantics", location});
            } else {
                bool enclosingInstance = ref.referenceKind == "super";
                visitExpr(ref.receiver.get(), [&](const Expr& receiver) {
                    if (receiver.kind() == Expr::Kind::VarRef) {
                        const auto& name = static_cast<const VarRefExpr&>(receiver).name;
                        enclosingInstance |= name == "this" || name == "super";
                    } else if (receiver.kind() == Expr::Kind::MemberAccess) {
                        const auto& member = static_cast<const MemberAccessExpr&>(receiver).member;
                        enclosingInstance |= member == "this" || member == "super";
                    }
                });
                if (enclosingInstance)
                    errors.push_back({"methodref", "Emitter has no original enclosing instance for this/super reference", location});
            }
            if (supportsJavaMethodReferences && ((ref.referenceKind == "bound" && (!ref.receiver || !ref.evaluateReceiverAtCreation)) ||
                       ((ref.referenceKind == "type" || ref.referenceKind == "constructor") && ref.ownerType.nameParts.empty()) ||
                       (ref.referenceKind != "bound" && ref.referenceKind != "type" &&
                        ref.referenceKind != "super" && ref.referenceKind != "constructor") ||
                       (ref.referenceKind != "constructor" && ref.callee.empty()))) {
                errors.push_back({"methodref", "Incomplete method-reference binding", location});
            }
        }
    });
    // Current struct emitters synthesize constructors/fields and cannot preserve
    // source initialization order or static-vs-instance execution.
    for (const auto& type : module.types) {
        if (!type.initializers.empty())
            errors.push_back({"initializer", "Type initializer execution is not supported by this emitter", type.qualifiedName});
        for (const auto& field : type.fields)
            if (field.init)
                errors.push_back({"initializer", "Field initializer execution is not supported by this emitter", type.qualifiedName + "::" + field.name});
    }
    return errors;
}

} // namespace topo::transpile
#endif
