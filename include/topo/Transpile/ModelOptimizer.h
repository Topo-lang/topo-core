#ifndef TOPO_TRANSPILE_MODELOPTIMIZER_H
#define TOPO_TRANSPILE_MODELOPTIMIZER_H

#include "topo/Transpile/TranspileModel.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace topo::transpile {

// --- Pass interface ---

class ModelPass {
public:
    virtual ~ModelPass() = default;
    virtual void run(TranspileFunction& func) = 0;
};

// --- Pass 1: Temporary variable folding ---
// Inlines single-use _tmp variables when the definition immediately
// precedes the use site.
class TempFoldingPass : public ModelPass {
public:
    void run(TranspileFunction& func) override;

private:
    void foldBlock(std::vector<StmtPtr>& stmts);
    static int countRefs(const Expr& expr, const std::string& name);
    static int countRefsInStmts(const std::vector<StmtPtr>& stmts, const std::string& name);
    static int countRefsInStmt(const Stmt& stmt, const std::string& name);
    static ExprPtr replaceRef(ExprPtr expr, const std::string& name, const Expr& replacement);
    static void replaceRefInStmt(Stmt& stmt, const std::string& name, const Expr& replacement);
    static ExprPtr cloneExpr(const Expr& expr);
};

// --- Pass 2: Variable naming heuristics ---
// Renames generated names (_tmp0, field0) to meaningful names based on
// usage patterns.
class NameHeuristicsPass : public ModelPass {
public:
    void run(TranspileFunction& func) override;

private:
    void detectLoopCounters(const std::vector<StmtPtr>& stmts);
    void detectAccumulators(const std::vector<StmtPtr>& stmts);
    void detectBooleans(const std::vector<StmtPtr>& stmts);
    void detectIndices(const std::vector<StmtPtr>& stmts);
    void applyRenames(std::vector<StmtPtr>& stmts);

    static bool isGeneratedName(const std::string& name);
    static void renameInExpr(Expr& expr, const std::unordered_map<std::string, std::string>& renames);
    static void renameInStmts(std::vector<StmtPtr>& stmts, const std::unordered_map<std::string, std::string>& renames);
    static void renameInStmt(Stmt& stmt, const std::unordered_map<std::string, std::string>& renames);
    static bool exprUsedAsIndex(const Expr& expr, const std::string& name);
    static bool exprUsedAsIndexInStmts(const std::vector<StmtPtr>& stmts, const std::string& name);

    std::unordered_map<std::string, std::string> renames_;
    std::unordered_set<std::string> usedNames_;
    int counterIdx_ = 0;
    int accumIdx_ = 0;

    std::string nextCounter();
    std::string nextAccumulator();
    std::string allocateName(const std::string& preferred);
};

// --- Pass 3: Dead code elimination ---
// Removes unreachable statements after unconditional return/break/continue.
class DeadCodeElimPass : public ModelPass {
public:
    void run(TranspileFunction& func) override;

private:
    static void elimBlock(std::vector<StmtPtr>& stmts);
    static bool isTerminator(const Stmt& stmt);
};

// --- Optimizer pipeline ---

class ModelOptimizer {
public:
    void addPass(std::unique_ptr<ModelPass> pass);
    void optimize(TranspileModule& module);

    // Convenience: create with the standard L3 idiomatization passes
    static ModelOptimizer createIdiomaticPipeline();

private:
    std::vector<std::unique_ptr<ModelPass>> passes_;
};

} // namespace topo::transpile

#endif // TOPO_TRANSPILE_MODELOPTIMIZER_H
