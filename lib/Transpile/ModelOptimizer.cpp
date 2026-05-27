#include "topo/Transpile/ModelOptimizer.h"
#include <algorithm>

namespace topo::transpile {

// =====================================================================
// Expression cloning
// =====================================================================

ExprPtr TempFoldingPass::cloneExpr(const Expr& expr) {
    switch (expr.kind()) {
    case Expr::Kind::Literal: {
        const auto& e = static_cast<const LiteralExpr&>(expr);
        auto c = std::make_unique<LiteralExpr>();
        c->fidelity = e.fidelity;
        c->litKind = e.litKind;
        c->value = e.value;
        return c;
    }
    case Expr::Kind::VarRef: {
        const auto& e = static_cast<const VarRefExpr&>(expr);
        auto c = std::make_unique<VarRefExpr>();
        c->fidelity = e.fidelity;
        c->name = e.name;
        return c;
    }
    case Expr::Kind::BinaryOp: {
        const auto& e = static_cast<const BinaryOpExpr&>(expr);
        auto c = std::make_unique<BinaryOpExpr>();
        c->fidelity = e.fidelity;
        c->op = e.op;
        c->lhs = cloneExpr(*e.lhs);
        c->rhs = cloneExpr(*e.rhs);
        return c;
    }
    case Expr::Kind::UnaryOp: {
        const auto& e = static_cast<const UnaryOpExpr&>(expr);
        auto c = std::make_unique<UnaryOpExpr>();
        c->fidelity = e.fidelity;
        c->op = e.op;
        c->operand = cloneExpr(*e.operand);
        return c;
    }
    case Expr::Kind::Call: {
        const auto& e = static_cast<const CallExpr&>(expr);
        auto c = std::make_unique<CallExpr>();
        c->fidelity = e.fidelity;
        c->callee = e.callee;
        for (const auto& arg : e.args)
            c->args.push_back(cloneExpr(*arg));
        return c;
    }
    case Expr::Kind::MemberAccess: {
        const auto& e = static_cast<const MemberAccessExpr&>(expr);
        auto c = std::make_unique<MemberAccessExpr>();
        c->fidelity = e.fidelity;
        c->object = cloneExpr(*e.object);
        c->member = e.member;
        return c;
    }
    case Expr::Kind::Index: {
        const auto& e = static_cast<const IndexExpr&>(expr);
        auto c = std::make_unique<IndexExpr>();
        c->fidelity = e.fidelity;
        c->object = cloneExpr(*e.object);
        c->index = cloneExpr(*e.index);
        return c;
    }
    case Expr::Kind::Construct: {
        const auto& e = static_cast<const ConstructExpr&>(expr);
        auto c = std::make_unique<ConstructExpr>();
        c->fidelity = e.fidelity;
        c->type = e.type;
        for (const auto& arg : e.args)
            c->args.push_back(cloneExpr(*arg));
        return c;
    }
    case Expr::Kind::Unsupported: {
        const auto& e = static_cast<const UnsupportedExpr&>(expr);
        auto c = std::make_unique<UnsupportedExpr>();
        c->fidelity = e.fidelity;
        c->description = e.description;
        return c;
    }
    case Expr::Kind::Ternary: {
        const auto& e = static_cast<const TernaryExpr&>(expr);
        auto c = std::make_unique<TernaryExpr>();
        c->fidelity = e.fidelity;
        c->condition = cloneExpr(*e.condition);
        c->trueExpr = cloneExpr(*e.trueExpr);
        c->falseExpr = cloneExpr(*e.falseExpr);
        return c;
    }
    case Expr::Kind::CompoundAssign: {
        const auto& e = static_cast<const CompoundAssignExpr&>(expr);
        auto c = std::make_unique<CompoundAssignExpr>();
        c->fidelity = e.fidelity;
        c->op = e.op;
        c->target = cloneExpr(*e.target);
        c->value = cloneExpr(*e.value);
        return c;
    }
    case Expr::Kind::Lambda: {
        // Lambda expressions have complex structure; clone as unsupported for now
        auto c = std::make_unique<UnsupportedExpr>();
        c->description = "lambda-clone";
        return c;
    }
    case Expr::Kind::Throw: {
        const auto& e = static_cast<const ThrowExpr&>(expr);
        auto c = std::make_unique<ThrowExpr>();
        c->fidelity = e.fidelity;
        if (e.operand) c->operand = cloneExpr(*e.operand);
        return c;
    }
    }
    auto c = std::make_unique<UnsupportedExpr>();
    c->description = "clone-fallback";
    return c;
}

// =====================================================================
// TempFoldingPass — reference counting
// =====================================================================

int TempFoldingPass::countRefs(const Expr& expr, const std::string& name) {
    switch (expr.kind()) {
    case Expr::Kind::VarRef: return static_cast<const VarRefExpr&>(expr).name == name ? 1 : 0;
    case Expr::Kind::BinaryOp: {
        const auto& e = static_cast<const BinaryOpExpr&>(expr);
        return countRefs(*e.lhs, name) + countRefs(*e.rhs, name);
    }
    case Expr::Kind::UnaryOp: {
        const auto& e = static_cast<const UnaryOpExpr&>(expr);
        return countRefs(*e.operand, name);
    }
    case Expr::Kind::Call: {
        const auto& e = static_cast<const CallExpr&>(expr);
        int count = 0;
        for (const auto& arg : e.args)
            count += countRefs(*arg, name);
        return count;
    }
    case Expr::Kind::MemberAccess: {
        const auto& e = static_cast<const MemberAccessExpr&>(expr);
        return countRefs(*e.object, name);
    }
    case Expr::Kind::Index: {
        const auto& e = static_cast<const IndexExpr&>(expr);
        return countRefs(*e.object, name) + countRefs(*e.index, name);
    }
    case Expr::Kind::Construct: {
        const auto& e = static_cast<const ConstructExpr&>(expr);
        int count = 0;
        for (const auto& arg : e.args)
            count += countRefs(*arg, name);
        return count;
    }
    case Expr::Kind::Ternary: {
        const auto& e = static_cast<const TernaryExpr&>(expr);
        return countRefs(*e.condition, name) + countRefs(*e.trueExpr, name) + countRefs(*e.falseExpr, name);
    }
    case Expr::Kind::CompoundAssign: {
        const auto& e = static_cast<const CompoundAssignExpr&>(expr);
        return countRefs(*e.target, name) + countRefs(*e.value, name);
    }
    default: return 0;
    }
}

int TempFoldingPass::countRefsInStmt(const Stmt& stmt, const std::string& name) {
    switch (stmt.kind()) {
    case Stmt::Kind::VarDecl: {
        const auto& s = static_cast<const VarDeclStmt&>(stmt);
        return s.init ? countRefs(*s.init, name) : 0;
    }
    case Stmt::Kind::Assign: {
        const auto& s = static_cast<const AssignStmt&>(stmt);
        return countRefs(*s.target, name) + countRefs(*s.value, name);
    }
    case Stmt::Kind::Return: {
        const auto& s = static_cast<const ReturnStmt&>(stmt);
        return s.value ? countRefs(*s.value, name) : 0;
    }
    case Stmt::Kind::ExprStmt: {
        const auto& s = static_cast<const ExprStmt&>(stmt);
        return countRefs(*s.expr, name);
    }
    case Stmt::Kind::If: {
        const auto& s = static_cast<const IfStmt&>(stmt);
        int count = countRefs(*s.condition, name);
        count += countRefsInStmts(s.thenBody, name);
        count += countRefsInStmts(s.elseBody, name);
        return count;
    }
    case Stmt::Kind::For: {
        const auto& s = static_cast<const ForStmt&>(stmt);
        int count = 0;
        if (s.init) count += countRefsInStmt(*s.init, name);
        if (s.condition) count += countRefs(*s.condition, name);
        if (s.increment) count += countRefs(*s.increment, name);
        count += countRefsInStmts(s.body, name);
        return count;
    }
    case Stmt::Kind::While: {
        const auto& s = static_cast<const WhileStmt&>(stmt);
        int count = countRefs(*s.condition, name);
        count += countRefsInStmts(s.body, name);
        return count;
    }
    case Stmt::Kind::Switch: {
        const auto& s = static_cast<const SwitchStmt&>(stmt);
        int count = countRefs(*s.subject, name);
        for (const auto& sc : s.cases) {
            if (sc.value) count += countRefs(*sc.value, name);
            count += countRefsInStmts(sc.body, name);
        }
        return count;
    }
    case Stmt::Kind::TryCatch: {
        const auto& s = static_cast<const TryCatchStmt&>(stmt);
        int count = countRefsInStmts(s.tryBody, name);
        for (const auto& cc : s.catchClauses)
            count += countRefsInStmts(cc.body, name);
        count += countRefsInStmts(s.finallyBody, name);
        return count;
    }
    case Stmt::Kind::Break:
    case Stmt::Kind::Continue:
        return 0;
    }
    return 0;
}

int TempFoldingPass::countRefsInStmts(const std::vector<StmtPtr>& stmts, const std::string& name) {
    int count = 0;
    for (const auto& s : stmts)
        count += countRefsInStmt(*s, name);
    return count;
}

// =====================================================================
// TempFoldingPass — reference replacement
// =====================================================================

ExprPtr TempFoldingPass::replaceRef(ExprPtr expr, const std::string& name, const Expr& replacement) {
    switch (expr->kind()) {
    case Expr::Kind::VarRef: {
        auto& e = static_cast<VarRefExpr&>(*expr);
        if (e.name == name) return cloneExpr(replacement);
        return expr;
    }
    case Expr::Kind::BinaryOp: {
        auto& e = static_cast<BinaryOpExpr&>(*expr);
        e.lhs = replaceRef(std::move(e.lhs), name, replacement);
        e.rhs = replaceRef(std::move(e.rhs), name, replacement);
        return expr;
    }
    case Expr::Kind::UnaryOp: {
        auto& e = static_cast<UnaryOpExpr&>(*expr);
        e.operand = replaceRef(std::move(e.operand), name, replacement);
        return expr;
    }
    case Expr::Kind::Call: {
        auto& e = static_cast<CallExpr&>(*expr);
        for (auto& arg : e.args)
            arg = replaceRef(std::move(arg), name, replacement);
        return expr;
    }
    case Expr::Kind::MemberAccess: {
        auto& e = static_cast<MemberAccessExpr&>(*expr);
        e.object = replaceRef(std::move(e.object), name, replacement);
        return expr;
    }
    case Expr::Kind::Index: {
        auto& e = static_cast<IndexExpr&>(*expr);
        e.object = replaceRef(std::move(e.object), name, replacement);
        e.index = replaceRef(std::move(e.index), name, replacement);
        return expr;
    }
    case Expr::Kind::Construct: {
        auto& e = static_cast<ConstructExpr&>(*expr);
        for (auto& arg : e.args)
            arg = replaceRef(std::move(arg), name, replacement);
        return expr;
    }
    case Expr::Kind::Ternary: {
        auto& e = static_cast<TernaryExpr&>(*expr);
        e.condition = replaceRef(std::move(e.condition), name, replacement);
        e.trueExpr = replaceRef(std::move(e.trueExpr), name, replacement);
        e.falseExpr = replaceRef(std::move(e.falseExpr), name, replacement);
        return expr;
    }
    case Expr::Kind::CompoundAssign: {
        auto& e = static_cast<CompoundAssignExpr&>(*expr);
        e.target = replaceRef(std::move(e.target), name, replacement);
        e.value = replaceRef(std::move(e.value), name, replacement);
        return expr;
    }
    default: return expr;
    }
}

void TempFoldingPass::replaceRefInStmt(Stmt& stmt, const std::string& name, const Expr& replacement) {
    switch (stmt.kind()) {
    case Stmt::Kind::VarDecl: {
        auto& s = static_cast<VarDeclStmt&>(stmt);
        if (s.init) s.init = replaceRef(std::move(s.init), name, replacement);
        break;
    }
    case Stmt::Kind::Assign: {
        auto& s = static_cast<AssignStmt&>(stmt);
        s.target = replaceRef(std::move(s.target), name, replacement);
        s.value = replaceRef(std::move(s.value), name, replacement);
        break;
    }
    case Stmt::Kind::Return: {
        auto& s = static_cast<ReturnStmt&>(stmt);
        if (s.value) s.value = replaceRef(std::move(s.value), name, replacement);
        break;
    }
    case Stmt::Kind::ExprStmt: {
        auto& s = static_cast<ExprStmt&>(stmt);
        s.expr = replaceRef(std::move(s.expr), name, replacement);
        break;
    }
    case Stmt::Kind::If: {
        auto& s = static_cast<IfStmt&>(stmt);
        s.condition = replaceRef(std::move(s.condition), name, replacement);
        for (auto& st : s.thenBody)
            replaceRefInStmt(*st, name, replacement);
        for (auto& st : s.elseBody)
            replaceRefInStmt(*st, name, replacement);
        break;
    }
    case Stmt::Kind::For: {
        auto& s = static_cast<ForStmt&>(stmt);
        if (s.init) replaceRefInStmt(*s.init, name, replacement);
        if (s.condition) s.condition = replaceRef(std::move(s.condition), name, replacement);
        if (s.increment) s.increment = replaceRef(std::move(s.increment), name, replacement);
        for (auto& st : s.body)
            replaceRefInStmt(*st, name, replacement);
        break;
    }
    case Stmt::Kind::While: {
        auto& s = static_cast<WhileStmt&>(stmt);
        s.condition = replaceRef(std::move(s.condition), name, replacement);
        for (auto& st : s.body)
            replaceRefInStmt(*st, name, replacement);
        break;
    }
    case Stmt::Kind::Switch: {
        auto& s = static_cast<SwitchStmt&>(stmt);
        s.subject = replaceRef(std::move(s.subject), name, replacement);
        for (auto& sc : s.cases) {
            if (sc.value) sc.value = replaceRef(std::move(sc.value), name, replacement);
            for (auto& st : sc.body)
                replaceRefInStmt(*st, name, replacement);
        }
        break;
    }
    case Stmt::Kind::TryCatch: {
        auto& s = static_cast<TryCatchStmt&>(stmt);
        for (auto& st : s.tryBody)
            replaceRefInStmt(*st, name, replacement);
        for (auto& cc : s.catchClauses)
            for (auto& st : cc.body)
                replaceRefInStmt(*st, name, replacement);
        for (auto& st : s.finallyBody)
            replaceRefInStmt(*st, name, replacement);
        break;
    }
    case Stmt::Kind::Break:
    case Stmt::Kind::Continue:
        break;
    }
}

// =====================================================================
// TempFoldingPass — core logic
// =====================================================================

void TempFoldingPass::run(TranspileFunction& func) {
    foldBlock(func.body);
}

void TempFoldingPass::foldBlock(std::vector<StmtPtr>& stmts) {
    // First recurse into nested blocks
    for (auto& s : stmts) {
        switch (s->kind()) {
        case Stmt::Kind::If: {
            auto& ifs = static_cast<IfStmt&>(*s);
            foldBlock(ifs.thenBody);
            foldBlock(ifs.elseBody);
            break;
        }
        case Stmt::Kind::For: {
            auto& fs = static_cast<ForStmt&>(*s);
            foldBlock(fs.body);
            break;
        }
        case Stmt::Kind::While: {
            auto& ws = static_cast<WhileStmt&>(*s);
            foldBlock(ws.body);
            break;
        }
        case Stmt::Kind::Switch: {
            auto& sw = static_cast<SwitchStmt&>(*s);
            for (auto& sc : sw.cases)
                foldBlock(sc.body);
            break;
        }
        case Stmt::Kind::TryCatch: {
            auto& tc = static_cast<TryCatchStmt&>(*s);
            foldBlock(tc.tryBody);
            for (auto& cc : tc.catchClauses)
                foldBlock(cc.body);
            foldBlock(tc.finallyBody);
            break;
        }
        default: break;
        }
    }

    // Fold temporaries: scan for VarDecl with _tmp prefix, used exactly
    // once in the immediately following statement and nowhere else.
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i + 1 < stmts.size(); ++i) {
            if (stmts[i]->kind() != Stmt::Kind::VarDecl) continue;

            auto& decl = static_cast<VarDeclStmt&>(*stmts[i]);
            if (decl.name.find("_tmp") != 0) continue;
            if (!decl.init) continue;

            // Count uses in the next statement only
            int refsInNext = countRefsInStmt(*stmts[i + 1], decl.name);
            if (refsInNext != 1) continue;

            // Verify no uses elsewhere (remaining statements)
            int refsElsewhere = 0;
            for (size_t j = i + 2; j < stmts.size(); ++j)
                refsElsewhere += countRefsInStmt(*stmts[j], decl.name);
            if (refsElsewhere != 0) continue;

            // Fold: replace the reference in the next statement, remove the decl
            replaceRefInStmt(*stmts[i + 1], decl.name, *decl.init);
            stmts.erase(stmts.begin() + static_cast<ptrdiff_t>(i));
            changed = true;
            break; // restart scan after mutation
        }
    }
}

// =====================================================================
// NameHeuristicsPass
// =====================================================================

bool NameHeuristicsPass::isGeneratedName(const std::string& name) {
    if (name.find("_tmp") == 0) return true;
    if (name.find("field") == 0 && name.size() > 5) {
        for (size_t i = 5; i < name.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(name[i]))) return false;
        }
        return true;
    }
    return false;
}

std::string NameHeuristicsPass::nextCounter() {
    static const char* counters[] = {"i", "j", "k", "ii", "jj", "kk"};
    if (counterIdx_ < 6) {
        return counters[counterIdx_++];
    }
    return "idx" + std::to_string(counterIdx_++);
}

std::string NameHeuristicsPass::nextAccumulator() {
    static const char* accums[] = {"sum", "total", "acc"};
    if (accumIdx_ < 3) {
        return accums[accumIdx_++];
    }
    return "sum" + std::to_string(accumIdx_++);
}

std::string NameHeuristicsPass::allocateName(const std::string& preferred) {
    if (usedNames_.count(preferred) == 0) {
        usedNames_.insert(preferred);
        return preferred;
    }
    for (int suffix = 2;; ++suffix) {
        std::string candidate = preferred + std::to_string(suffix);
        if (usedNames_.count(candidate) == 0) {
            usedNames_.insert(candidate);
            return candidate;
        }
    }
}

void NameHeuristicsPass::detectLoopCounters(const std::vector<StmtPtr>& stmts) {
    for (const auto& s : stmts) {
        if (s->kind() == Stmt::Kind::For) {
            const auto& fs = static_cast<const ForStmt&>(*s);
            if (fs.init && fs.init->kind() == Stmt::Kind::VarDecl) {
                const auto& decl = static_cast<const VarDeclStmt&>(*fs.init);
                if (isGeneratedName(decl.name)) {
                    renames_[decl.name] = allocateName(nextCounter());
                }
            }
            detectLoopCounters(fs.body);
        } else if (s->kind() == Stmt::Kind::While) {
            detectLoopCounters(static_cast<const WhileStmt&>(*s).body);
        } else if (s->kind() == Stmt::Kind::If) {
            const auto& ifs = static_cast<const IfStmt&>(*s);
            detectLoopCounters(ifs.thenBody);
            detectLoopCounters(ifs.elseBody);
        } else if (s->kind() == Stmt::Kind::Switch) {
            const auto& sw = static_cast<const SwitchStmt&>(*s);
            for (const auto& sc : sw.cases)
                detectLoopCounters(sc.body);
        } else if (s->kind() == Stmt::Kind::TryCatch) {
            const auto& tc = static_cast<const TryCatchStmt&>(*s);
            detectLoopCounters(tc.tryBody);
            for (const auto& cc : tc.catchClauses)
                detectLoopCounters(cc.body);
            detectLoopCounters(tc.finallyBody);
        }
    }
}

void NameHeuristicsPass::detectAccumulators(const std::vector<StmtPtr>& stmts) {
    for (size_t i = 0; i < stmts.size(); ++i) {
        if (stmts[i]->kind() != Stmt::Kind::VarDecl) continue;

        const auto& decl = static_cast<const VarDeclStmt&>(*stmts[i]);
        if (!isGeneratedName(decl.name) || renames_.count(decl.name)) continue;
        if (!decl.init || decl.init->kind() != Expr::Kind::Literal) continue;

        const auto& initLit = static_cast<const LiteralExpr&>(*decl.init);
        if (initLit.value != "0" && initLit.value != "0.0") continue;

        // Look for a loop following this decl that uses += on the variable
        for (size_t j = i + 1; j < stmts.size(); ++j) {
            const std::vector<StmtPtr>* loopBody = nullptr;
            if (stmts[j]->kind() == Stmt::Kind::For)
                loopBody = &static_cast<const ForStmt&>(*stmts[j]).body;
            else if (stmts[j]->kind() == Stmt::Kind::While)
                loopBody = &static_cast<const WhileStmt&>(*stmts[j]).body;
            if (!loopBody) continue;

            for (const auto& bs : *loopBody) {
                if (bs->kind() != Stmt::Kind::Assign) continue;
                const auto& as = static_cast<const AssignStmt&>(*bs);
                if (as.target->kind() != Expr::Kind::VarRef) continue;
                if (static_cast<const VarRefExpr&>(*as.target).name != decl.name) continue;
                if (as.value->kind() != Expr::Kind::BinaryOp) continue;
                const auto& binop = static_cast<const BinaryOpExpr&>(*as.value);
                if (binop.op != BinaryOp::Add) continue;

                bool lhsIsAccum = (binop.lhs->kind() == Expr::Kind::VarRef &&
                                   static_cast<const VarRefExpr&>(*binop.lhs).name == decl.name);
                bool rhsIsAccum = (binop.rhs->kind() == Expr::Kind::VarRef &&
                                   static_cast<const VarRefExpr&>(*binop.rhs).name == decl.name);
                if (lhsIsAccum || rhsIsAccum) {
                    renames_[decl.name] = allocateName(nextAccumulator());
                    break;
                }
            }
            break;
        }

        // Recurse into nested blocks
        if (stmts[i]->kind() == Stmt::Kind::If) {
            const auto& ifs = static_cast<const IfStmt&>(*stmts[i]);
            detectAccumulators(ifs.thenBody);
            detectAccumulators(ifs.elseBody);
        } else if (stmts[i]->kind() == Stmt::Kind::For) {
            detectAccumulators(static_cast<const ForStmt&>(*stmts[i]).body);
        } else if (stmts[i]->kind() == Stmt::Kind::While) {
            detectAccumulators(static_cast<const WhileStmt&>(*stmts[i]).body);
        } else if (stmts[i]->kind() == Stmt::Kind::Switch) {
            const auto& sw = static_cast<const SwitchStmt&>(*stmts[i]);
            for (const auto& sc : sw.cases)
                detectAccumulators(sc.body);
        } else if (stmts[i]->kind() == Stmt::Kind::TryCatch) {
            const auto& tc = static_cast<const TryCatchStmt&>(*stmts[i]);
            detectAccumulators(tc.tryBody);
            for (const auto& cc : tc.catchClauses)
                detectAccumulators(cc.body);
            detectAccumulators(tc.finallyBody);
        }
    }
}

void NameHeuristicsPass::detectBooleans(const std::vector<StmtPtr>& stmts) {
    for (const auto& s : stmts) {
        if (s->kind() == Stmt::Kind::VarDecl) {
            const auto& decl = static_cast<const VarDeclStmt&>(*s);
            if (!isGeneratedName(decl.name) || renames_.count(decl.name)) continue;

            if (decl.init && decl.init->kind() == Expr::Kind::Literal) {
                const auto& lit = static_cast<const LiteralExpr&>(*decl.init);
                if (lit.litKind == LiteralKind::Boolean) renames_[decl.name] = allocateName("isReady");
            } else if (decl.init && decl.init->kind() == Expr::Kind::BinaryOp) {
                const auto& bin = static_cast<const BinaryOpExpr&>(*decl.init);
                if (bin.op == BinaryOp::Eq || bin.op == BinaryOp::NotEq || bin.op == BinaryOp::Less ||
                    bin.op == BinaryOp::Greater || bin.op == BinaryOp::LessEq || bin.op == BinaryOp::GreaterEq)
                    renames_[decl.name] = allocateName("isReady");
            }
        }
        // Recurse
        if (s->kind() == Stmt::Kind::If) {
            const auto& ifs = static_cast<const IfStmt&>(*s);
            detectBooleans(ifs.thenBody);
            detectBooleans(ifs.elseBody);
        } else if (s->kind() == Stmt::Kind::For) {
            detectBooleans(static_cast<const ForStmt&>(*s).body);
        } else if (s->kind() == Stmt::Kind::While) {
            detectBooleans(static_cast<const WhileStmt&>(*s).body);
        } else if (s->kind() == Stmt::Kind::Switch) {
            const auto& sw = static_cast<const SwitchStmt&>(*s);
            for (const auto& sc : sw.cases)
                detectBooleans(sc.body);
        } else if (s->kind() == Stmt::Kind::TryCatch) {
            const auto& tc = static_cast<const TryCatchStmt&>(*s);
            detectBooleans(tc.tryBody);
            for (const auto& cc : tc.catchClauses)
                detectBooleans(cc.body);
            detectBooleans(tc.finallyBody);
        }
    }
}

bool NameHeuristicsPass::exprUsedAsIndex(const Expr& expr, const std::string& name) {
    if (expr.kind() == Expr::Kind::Index) {
        const auto& idx = static_cast<const IndexExpr&>(expr);
        if (idx.index->kind() == Expr::Kind::VarRef && static_cast<const VarRefExpr&>(*idx.index).name == name)
            return true;
        return exprUsedAsIndex(*idx.object, name) || exprUsedAsIndex(*idx.index, name);
    }
    switch (expr.kind()) {
    case Expr::Kind::BinaryOp: {
        const auto& e = static_cast<const BinaryOpExpr&>(expr);
        return exprUsedAsIndex(*e.lhs, name) || exprUsedAsIndex(*e.rhs, name);
    }
    case Expr::Kind::UnaryOp: return exprUsedAsIndex(*static_cast<const UnaryOpExpr&>(expr).operand, name);
    case Expr::Kind::Call: {
        const auto& e = static_cast<const CallExpr&>(expr);
        for (const auto& arg : e.args)
            if (exprUsedAsIndex(*arg, name)) return true;
        return false;
    }
    case Expr::Kind::MemberAccess: return exprUsedAsIndex(*static_cast<const MemberAccessExpr&>(expr).object, name);
    case Expr::Kind::Ternary: {
        const auto& e = static_cast<const TernaryExpr&>(expr);
        return exprUsedAsIndex(*e.condition, name) || exprUsedAsIndex(*e.trueExpr, name) || exprUsedAsIndex(*e.falseExpr, name);
    }
    case Expr::Kind::CompoundAssign: {
        const auto& e = static_cast<const CompoundAssignExpr&>(expr);
        return exprUsedAsIndex(*e.target, name) || exprUsedAsIndex(*e.value, name);
    }
    default: return false;
    }
}

bool NameHeuristicsPass::exprUsedAsIndexInStmts(const std::vector<StmtPtr>& stmts, const std::string& name) {
    for (const auto& s : stmts) {
        switch (s->kind()) {
        case Stmt::Kind::VarDecl: {
            const auto& vd = static_cast<const VarDeclStmt&>(*s);
            if (vd.init && exprUsedAsIndex(*vd.init, name)) return true;
            break;
        }
        case Stmt::Kind::Assign: {
            const auto& as = static_cast<const AssignStmt&>(*s);
            if (exprUsedAsIndex(*as.target, name) || exprUsedAsIndex(*as.value, name)) return true;
            break;
        }
        case Stmt::Kind::Return: {
            const auto& rs = static_cast<const ReturnStmt&>(*s);
            if (rs.value && exprUsedAsIndex(*rs.value, name)) return true;
            break;
        }
        case Stmt::Kind::ExprStmt: {
            const auto& es = static_cast<const ExprStmt&>(*s);
            if (exprUsedAsIndex(*es.expr, name)) return true;
            break;
        }
        case Stmt::Kind::If: {
            const auto& ifs = static_cast<const IfStmt&>(*s);
            if (exprUsedAsIndex(*ifs.condition, name)) return true;
            if (exprUsedAsIndexInStmts(ifs.thenBody, name)) return true;
            if (exprUsedAsIndexInStmts(ifs.elseBody, name)) return true;
            break;
        }
        case Stmt::Kind::For: {
            const auto& fs = static_cast<const ForStmt&>(*s);
            if (fs.condition && exprUsedAsIndex(*fs.condition, name)) return true;
            if (fs.increment && exprUsedAsIndex(*fs.increment, name)) return true;
            if (exprUsedAsIndexInStmts(fs.body, name)) return true;
            break;
        }
        case Stmt::Kind::While: {
            const auto& ws = static_cast<const WhileStmt&>(*s);
            if (exprUsedAsIndex(*ws.condition, name)) return true;
            if (exprUsedAsIndexInStmts(ws.body, name)) return true;
            break;
        }
        case Stmt::Kind::Switch: {
            const auto& sw = static_cast<const SwitchStmt&>(*s);
            if (exprUsedAsIndex(*sw.subject, name)) return true;
            for (const auto& sc : sw.cases) {
                if (sc.value && exprUsedAsIndex(*sc.value, name)) return true;
                if (exprUsedAsIndexInStmts(sc.body, name)) return true;
            }
            break;
        }
        case Stmt::Kind::TryCatch: {
            const auto& tc = static_cast<const TryCatchStmt&>(*s);
            if (exprUsedAsIndexInStmts(tc.tryBody, name)) return true;
            for (const auto& cc : tc.catchClauses)
                if (exprUsedAsIndexInStmts(cc.body, name)) return true;
            if (exprUsedAsIndexInStmts(tc.finallyBody, name)) return true;
            break;
        }
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            break;
        }
    }
    return false;
}

void NameHeuristicsPass::detectIndices(const std::vector<StmtPtr>& stmts) {
    for (const auto& s : stmts) {
        if (s->kind() == Stmt::Kind::VarDecl) {
            const auto& decl = static_cast<const VarDeclStmt&>(*s);
            if (!isGeneratedName(decl.name) || renames_.count(decl.name)) continue;

            if (exprUsedAsIndexInStmts(stmts, decl.name)) renames_[decl.name] = allocateName("idx");
        }
        // Recurse
        if (s->kind() == Stmt::Kind::If) {
            const auto& ifs = static_cast<const IfStmt&>(*s);
            detectIndices(ifs.thenBody);
            detectIndices(ifs.elseBody);
        } else if (s->kind() == Stmt::Kind::For) {
            detectIndices(static_cast<const ForStmt&>(*s).body);
        } else if (s->kind() == Stmt::Kind::While) {
            detectIndices(static_cast<const WhileStmt&>(*s).body);
        } else if (s->kind() == Stmt::Kind::Switch) {
            const auto& sw = static_cast<const SwitchStmt&>(*s);
            for (const auto& sc : sw.cases)
                detectIndices(sc.body);
        } else if (s->kind() == Stmt::Kind::TryCatch) {
            const auto& tc = static_cast<const TryCatchStmt&>(*s);
            detectIndices(tc.tryBody);
            for (const auto& cc : tc.catchClauses)
                detectIndices(cc.body);
            detectIndices(tc.finallyBody);
        }
    }
}

// --- Rename application ---

void NameHeuristicsPass::renameInExpr(Expr& expr, const std::unordered_map<std::string, std::string>& renames) {
    switch (expr.kind()) {
    case Expr::Kind::VarRef: {
        auto& e = static_cast<VarRefExpr&>(expr);
        auto it = renames.find(e.name);
        if (it != renames.end()) e.name = it->second;
        break;
    }
    case Expr::Kind::BinaryOp: {
        auto& e = static_cast<BinaryOpExpr&>(expr);
        renameInExpr(*e.lhs, renames);
        renameInExpr(*e.rhs, renames);
        break;
    }
    case Expr::Kind::UnaryOp: {
        auto& e = static_cast<UnaryOpExpr&>(expr);
        renameInExpr(*e.operand, renames);
        break;
    }
    case Expr::Kind::Call: {
        auto& e = static_cast<CallExpr&>(expr);
        for (auto& arg : e.args)
            renameInExpr(*arg, renames);
        break;
    }
    case Expr::Kind::MemberAccess: {
        auto& e = static_cast<MemberAccessExpr&>(expr);
        renameInExpr(*e.object, renames);
        break;
    }
    case Expr::Kind::Index: {
        auto& e = static_cast<IndexExpr&>(expr);
        renameInExpr(*e.object, renames);
        renameInExpr(*e.index, renames);
        break;
    }
    case Expr::Kind::Construct: {
        auto& e = static_cast<ConstructExpr&>(expr);
        for (auto& arg : e.args)
            renameInExpr(*arg, renames);
        break;
    }
    case Expr::Kind::Ternary: {
        auto& e = static_cast<TernaryExpr&>(expr);
        renameInExpr(*e.condition, renames);
        renameInExpr(*e.trueExpr, renames);
        renameInExpr(*e.falseExpr, renames);
        break;
    }
    case Expr::Kind::CompoundAssign: {
        auto& e = static_cast<CompoundAssignExpr&>(expr);
        renameInExpr(*e.target, renames);
        renameInExpr(*e.value, renames);
        break;
    }
    default: break;
    }
}

void NameHeuristicsPass::renameInStmt(Stmt& stmt, const std::unordered_map<std::string, std::string>& renames) {
    switch (stmt.kind()) {
    case Stmt::Kind::VarDecl: {
        auto& s = static_cast<VarDeclStmt&>(stmt);
        auto it = renames.find(s.name);
        if (it != renames.end()) s.name = it->second;
        if (s.init) renameInExpr(*s.init, renames);
        break;
    }
    case Stmt::Kind::Assign: {
        auto& s = static_cast<AssignStmt&>(stmt);
        renameInExpr(*s.target, renames);
        renameInExpr(*s.value, renames);
        break;
    }
    case Stmt::Kind::Return: {
        auto& s = static_cast<ReturnStmt&>(stmt);
        if (s.value) renameInExpr(*s.value, renames);
        break;
    }
    case Stmt::Kind::ExprStmt: {
        auto& s = static_cast<ExprStmt&>(stmt);
        renameInExpr(*s.expr, renames);
        break;
    }
    case Stmt::Kind::If: {
        auto& s = static_cast<IfStmt&>(stmt);
        renameInExpr(*s.condition, renames);
        renameInStmts(s.thenBody, renames);
        renameInStmts(s.elseBody, renames);
        break;
    }
    case Stmt::Kind::For: {
        auto& s = static_cast<ForStmt&>(stmt);
        if (s.init) renameInStmt(*s.init, renames);
        if (s.condition) renameInExpr(*s.condition, renames);
        if (s.increment) renameInExpr(*s.increment, renames);
        renameInStmts(s.body, renames);
        break;
    }
    case Stmt::Kind::While: {
        auto& s = static_cast<WhileStmt&>(stmt);
        renameInExpr(*s.condition, renames);
        renameInStmts(s.body, renames);
        break;
    }
    case Stmt::Kind::Switch: {
        auto& s = static_cast<SwitchStmt&>(stmt);
        renameInExpr(*s.subject, renames);
        for (auto& sc : s.cases) {
            if (sc.value) renameInExpr(*sc.value, renames);
            renameInStmts(sc.body, renames);
        }
        break;
    }
    case Stmt::Kind::TryCatch: {
        auto& s = static_cast<TryCatchStmt&>(stmt);
        renameInStmts(s.tryBody, renames);
        for (auto& cc : s.catchClauses)
            renameInStmts(cc.body, renames);
        renameInStmts(s.finallyBody, renames);
        break;
    }
    case Stmt::Kind::Break:
    case Stmt::Kind::Continue:
        break;
    }
}

void NameHeuristicsPass::renameInStmts(std::vector<StmtPtr>& stmts,
                                       const std::unordered_map<std::string, std::string>& renames) {
    for (auto& s : stmts)
        renameInStmt(*s, renames);
}

void NameHeuristicsPass::run(TranspileFunction& func) {
    renames_.clear();
    usedNames_.clear();
    counterIdx_ = 0;
    accumIdx_ = 0;

    // Collect existing non-generated names to avoid collisions
    for (const auto& p : func.params)
        usedNames_.insert(p.name);

    // Run detection passes in priority order
    detectLoopCounters(func.body);
    detectAccumulators(func.body);
    detectBooleans(func.body);
    detectIndices(func.body);

    if (!renames_.empty()) applyRenames(func.body);
}

void NameHeuristicsPass::applyRenames(std::vector<StmtPtr>& stmts) {
    renameInStmts(stmts, renames_);
}

// =====================================================================
// DeadCodeElimPass
// =====================================================================

bool DeadCodeElimPass::isTerminator(const Stmt& stmt) {
    if (stmt.kind() == Stmt::Kind::Return) return true;
    if (stmt.kind() == Stmt::Kind::Break) return true;
    if (stmt.kind() == Stmt::Kind::Continue) return true;
    if (stmt.kind() == Stmt::Kind::If) {
        const auto& ifs = static_cast<const IfStmt&>(stmt);
        if (ifs.thenBody.empty() || ifs.elseBody.empty()) return false;
        return isTerminator(*ifs.thenBody.back()) && isTerminator(*ifs.elseBody.back());
    }
    return false;
}

void DeadCodeElimPass::elimBlock(std::vector<StmtPtr>& stmts) {
    // First recurse into nested blocks
    for (auto& s : stmts) {
        switch (s->kind()) {
        case Stmt::Kind::If: {
            auto& ifs = static_cast<IfStmt&>(*s);
            elimBlock(ifs.thenBody);
            elimBlock(ifs.elseBody);
            break;
        }
        case Stmt::Kind::For: {
            auto& fs = static_cast<ForStmt&>(*s);
            elimBlock(fs.body);
            break;
        }
        case Stmt::Kind::While: {
            auto& ws = static_cast<WhileStmt&>(*s);
            elimBlock(ws.body);
            break;
        }
        case Stmt::Kind::Switch: {
            auto& sw = static_cast<SwitchStmt&>(*s);
            for (auto& sc : sw.cases)
                elimBlock(sc.body);
            break;
        }
        case Stmt::Kind::TryCatch: {
            auto& tc = static_cast<TryCatchStmt&>(*s);
            elimBlock(tc.tryBody);
            for (auto& cc : tc.catchClauses)
                elimBlock(cc.body);
            elimBlock(tc.finallyBody);
            break;
        }
        default: break;
        }
    }

    // Find first terminator and remove everything after it
    for (size_t i = 0; i < stmts.size(); ++i) {
        if (isTerminator(*stmts[i]) && i + 1 < stmts.size()) {
            stmts.erase(stmts.begin() + static_cast<ptrdiff_t>(i + 1), stmts.end());
            break;
        }
    }
}

void DeadCodeElimPass::run(TranspileFunction& func) {
    elimBlock(func.body);
}

// =====================================================================
// ModelOptimizer
// =====================================================================

void ModelOptimizer::addPass(std::unique_ptr<ModelPass> pass) {
    passes_.push_back(std::move(pass));
}

void ModelOptimizer::optimize(TranspileModule& module) {
    for (auto& func : module.functions) {
        for (auto& pass : passes_) {
            pass->run(func);
        }
    }
}

ModelOptimizer ModelOptimizer::createIdiomaticPipeline() {
    ModelOptimizer opt;
    opt.addPass(std::make_unique<TempFoldingPass>());
    opt.addPass(std::make_unique<NameHeuristicsPass>());
    opt.addPass(std::make_unique<DeadCodeElimPass>());
    return opt;
}

} // namespace topo::transpile
