#ifndef TOPO_TRANSPILE_TRANSPILETRAVERSAL_H
#define TOPO_TRANSPILE_TRANSPILETRAVERSAL_H

#include "topo/Transpile/TranspileModel.h"
#include <functional>

namespace topo::transpile {

using ExprVisitor = std::function<void(const Expr&)>;
inline void visitExpr(const Expr* expr, const ExprVisitor& visit);
inline void visitStmt(const Stmt* stmt, const ExprVisitor& visit);

template <typename Statements>
void visitStatements(const Statements& body, const ExprVisitor& visit) {
    for (const auto& stmt : body) visitStmt(stmt.get(), visit);
}

inline void visitExpr(const Expr* expr, const ExprVisitor& visit) {
    if (!expr) return;
    visit(*expr);
    switch (expr->kind()) {
    case Expr::Kind::BinaryOp: {
        const auto& e = static_cast<const BinaryOpExpr&>(*expr);
        visitExpr(e.lhs.get(), visit); visitExpr(e.rhs.get(), visit); break;
    }
    case Expr::Kind::UnaryOp:
        visitExpr(static_cast<const UnaryOpExpr&>(*expr).operand.get(), visit); break;
    case Expr::Kind::Call: {
        const auto& e = static_cast<const CallExpr&>(*expr);
        visitExpr(e.receiver.get(), visit);
        for (const auto& arg : e.args) visitExpr(arg.get(), visit);
        break;
    }
    case Expr::Kind::MemberAccess:
        visitExpr(static_cast<const MemberAccessExpr&>(*expr).object.get(), visit); break;
    case Expr::Kind::Index: {
        const auto& e = static_cast<const IndexExpr&>(*expr);
        visitExpr(e.object.get(), visit); visitExpr(e.index.get(), visit); break;
    }
    case Expr::Kind::Construct:
        for (const auto& arg : static_cast<const ConstructExpr&>(*expr).args) visitExpr(arg.get(), visit);
        break;
    case Expr::Kind::Lambda:
        visitStatements(static_cast<const LambdaExpr&>(*expr).body, visit); break;
    case Expr::Kind::MethodReference:
        visitExpr(static_cast<const MethodReferenceExpr&>(*expr).receiver.get(), visit); break;
    case Expr::Kind::Throw:
        visitExpr(static_cast<const ThrowExpr&>(*expr).operand.get(), visit); break;
    case Expr::Kind::Ternary: {
        const auto& e = static_cast<const TernaryExpr&>(*expr);
        visitExpr(e.condition.get(), visit); visitExpr(e.trueExpr.get(), visit); visitExpr(e.falseExpr.get(), visit);
        break;
    }
    case Expr::Kind::CompoundAssign: {
        const auto& e = static_cast<const CompoundAssignExpr&>(*expr);
        visitExpr(e.target.get(), visit); visitExpr(e.value.get(), visit); break;
    }
    case Expr::Kind::Literal: case Expr::Kind::VarRef: case Expr::Kind::Unsupported: break;
    }
}

inline void visitStmt(const Stmt* stmt, const ExprVisitor& visit) {
    if (!stmt) return;
    switch (stmt->kind()) {
    case Stmt::Kind::VarDecl:
        visitExpr(static_cast<const VarDeclStmt&>(*stmt).init.get(), visit); break;
    case Stmt::Kind::Assign: {
        const auto& s = static_cast<const AssignStmt&>(*stmt);
        visitExpr(s.target.get(), visit); visitExpr(s.value.get(), visit); break;
    }
    case Stmt::Kind::Return:
        visitExpr(static_cast<const ReturnStmt&>(*stmt).value.get(), visit); break;
    case Stmt::Kind::If: {
        const auto& s = static_cast<const IfStmt&>(*stmt);
        visitExpr(s.condition.get(), visit);
        visitStatements(s.thenBody, visit); visitStatements(s.elseBody, visit); break;
    }
    case Stmt::Kind::For: {
        const auto& s = static_cast<const ForStmt&>(*stmt);
        visitStmt(s.init.get(), visit); visitExpr(s.condition.get(), visit); visitExpr(s.increment.get(), visit);
        visitStatements(s.body, visit); break;
    }
    case Stmt::Kind::While: {
        const auto& s = static_cast<const WhileStmt&>(*stmt);
        visitExpr(s.condition.get(), visit); visitStatements(s.body, visit); break;
    }
    case Stmt::Kind::ExprStmt:
        visitExpr(static_cast<const ExprStmt&>(*stmt).expr.get(), visit); break;
    case Stmt::Kind::TryCatch: {
        const auto& s = static_cast<const TryCatchStmt&>(*stmt);
        visitStatements(s.tryBody, visit);
        for (const auto& clause : s.catchClauses) visitStatements(clause.body, visit);
        visitStatements(s.finallyBody, visit); break;
    }
    case Stmt::Kind::Switch: {
        const auto& s = static_cast<const SwitchStmt&>(*stmt);
        visitExpr(s.subject.get(), visit);
        for (const auto& clause : s.cases) {
            visitExpr(clause.value.get(), visit); visitStatements(clause.body, visit);
        }
        break;
    }
    case Stmt::Kind::Break: case Stmt::Kind::Continue: break;
    }
}

inline void visitModuleExprs(const TranspileModule& module, const ExprVisitor& visit) {
    for (const auto& fn : module.functions) visitStatements(fn.body, visit);
    for (const auto& type : module.types) {
        for (const auto& field : type.fields) visitExpr(field.init.get(), visit);
        visitStatements(type.initializers, visit);
    }
}

} // namespace topo::transpile
#endif
