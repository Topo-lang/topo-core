#include "topo/Transpile/ModelOptimizer.h"
#include <gtest/gtest.h>
#include <memory>

using namespace topo::transpile;

// =====================================================================
// Helpers
// =====================================================================

static ExprPtr mkLitInt(const std::string& val) {
    auto e = std::make_unique<LiteralExpr>();
    e->litKind = LiteralKind::Integer;
    e->value = val;
    return e;
}

static ExprPtr mkLitBool(bool val) {
    auto e = std::make_unique<LiteralExpr>();
    e->litKind = LiteralKind::Boolean;
    e->value = val ? "true" : "false";
    return e;
}

static ExprPtr mkVarRef(const std::string& name) {
    auto e = std::make_unique<VarRefExpr>();
    e->name = name;
    return e;
}

static ExprPtr mkBinOp(BinaryOp op, ExprPtr lhs, ExprPtr rhs) {
    auto e = std::make_unique<BinaryOpExpr>();
    e->op = op;
    e->lhs = std::move(lhs);
    e->rhs = std::move(rhs);
    return e;
}

static StmtPtr mkVarDecl(const std::string& name, ExprPtr init, const std::string& typeName = "int") {
    auto s = std::make_unique<VarDeclStmt>();
    s->type.nameParts = {typeName};
    s->name = name;
    s->init = std::move(init);
    return s;
}

static StmtPtr mkReturn(ExprPtr val) {
    auto s = std::make_unique<ReturnStmt>();
    s->value = std::move(val);
    return s;
}

static StmtPtr mkExprStmt(ExprPtr expr) {
    auto s = std::make_unique<ExprStmt>();
    s->expr = std::move(expr);
    return s;
}

static StmtPtr mkAssign(const std::string& target, ExprPtr value) {
    auto s = std::make_unique<AssignStmt>();
    auto t = std::make_unique<VarRefExpr>();
    t->name = target;
    s->target = std::move(t);
    s->value = std::move(value);
    return s;
}

static TranspileFunction mkFunc(const std::string& name) {
    TranspileFunction fn;
    fn.qualifiedName = name;
    fn.returnType.nameParts = {"int"};
    return fn;
}

// =====================================================================
// TempFoldingPass
// =====================================================================

TEST(TempFoldingPass, FoldsSingleUseTmp) {
    // _tmp0 = a + b; return _tmp0;  -->  return a + b;
    auto fn = mkFunc("test");
    fn.body.push_back(mkVarDecl("_tmp0", mkBinOp(BinaryOp::Add, mkVarRef("a"), mkVarRef("b"))));
    fn.body.push_back(mkReturn(mkVarRef("_tmp0")));

    TempFoldingPass pass;
    pass.run(fn);

    ASSERT_EQ(fn.body.size(), 1u);
    ASSERT_EQ(fn.body[0]->kind(), Stmt::Kind::Return);
    auto& ret = static_cast<ReturnStmt&>(*fn.body[0]);
    ASSERT_NE(ret.value, nullptr);
    ASSERT_EQ(ret.value->kind(), Expr::Kind::BinaryOp);
    auto& bin = static_cast<BinaryOpExpr&>(*ret.value);
    EXPECT_EQ(bin.op, BinaryOp::Add);
}

TEST(TempFoldingPass, PreservesMultiUseTmp) {
    // _tmp0 = a + b; x = _tmp0 + _tmp0;  -->  no change (used twice)
    auto fn = mkFunc("test");
    fn.body.push_back(mkVarDecl("_tmp0", mkBinOp(BinaryOp::Add, mkVarRef("a"), mkVarRef("b"))));
    fn.body.push_back(mkAssign("x", mkBinOp(BinaryOp::Add, mkVarRef("_tmp0"), mkVarRef("_tmp0"))));

    TempFoldingPass pass;
    pass.run(fn);

    EXPECT_EQ(fn.body.size(), 2u);
    EXPECT_EQ(fn.body[0]->kind(), Stmt::Kind::VarDecl);
}

TEST(TempFoldingPass, PreservesNonTmpVars) {
    // result = a + b; return result;  -->  no change (not _tmp prefix)
    auto fn = mkFunc("test");
    fn.body.push_back(mkVarDecl("result", mkBinOp(BinaryOp::Add, mkVarRef("a"), mkVarRef("b"))));
    fn.body.push_back(mkReturn(mkVarRef("result")));

    TempFoldingPass pass;
    pass.run(fn);

    EXPECT_EQ(fn.body.size(), 2u);
}

TEST(TempFoldingPass, FoldsChainedTmps) {
    // _tmp0 = a + b; _tmp1 = _tmp0 * c; return _tmp1;
    // --> return (a + b) * c;
    auto fn = mkFunc("test");
    fn.body.push_back(mkVarDecl("_tmp0", mkBinOp(BinaryOp::Add, mkVarRef("a"), mkVarRef("b"))));
    fn.body.push_back(mkVarDecl("_tmp1", mkBinOp(BinaryOp::Mul, mkVarRef("_tmp0"), mkVarRef("c"))));
    fn.body.push_back(mkReturn(mkVarRef("_tmp1")));

    TempFoldingPass pass;
    pass.run(fn);

    ASSERT_EQ(fn.body.size(), 1u);
    ASSERT_EQ(fn.body[0]->kind(), Stmt::Kind::Return);
    auto& ret = static_cast<ReturnStmt&>(*fn.body[0]);
    ASSERT_EQ(ret.value->kind(), Expr::Kind::BinaryOp);
    auto& mul = static_cast<BinaryOpExpr&>(*ret.value);
    EXPECT_EQ(mul.op, BinaryOp::Mul);
    ASSERT_EQ(mul.lhs->kind(), Expr::Kind::BinaryOp);
}

TEST(TempFoldingPass, PreservesTmpUsedLater) {
    // _tmp0 = a + b; foo(); return _tmp0;
    // refsInNext for foo() is 0, so no fold
    auto fn = mkFunc("test");
    fn.body.push_back(mkVarDecl("_tmp0", mkBinOp(BinaryOp::Add, mkVarRef("a"), mkVarRef("b"))));

    auto call = std::make_unique<CallExpr>();
    call->callee = "foo";
    fn.body.push_back(mkExprStmt(std::move(call)));
    fn.body.push_back(mkReturn(mkVarRef("_tmp0")));

    TempFoldingPass pass;
    pass.run(fn);

    EXPECT_EQ(fn.body.size(), 3u);
}

TEST(TempFoldingPass, FoldsInsideNestedBlocks) {
    // if (cond) { _tmp0 = a; return _tmp0; }
    auto fn = mkFunc("test");

    auto ifStmt = std::make_unique<IfStmt>();
    ifStmt->condition = mkVarRef("cond");
    ifStmt->thenBody.push_back(mkVarDecl("_tmp0", mkVarRef("a")));
    ifStmt->thenBody.push_back(mkReturn(mkVarRef("_tmp0")));
    fn.body.push_back(std::move(ifStmt));

    TempFoldingPass pass;
    pass.run(fn);

    ASSERT_EQ(fn.body.size(), 1u);
    auto& ifs = static_cast<IfStmt&>(*fn.body[0]);
    ASSERT_EQ(ifs.thenBody.size(), 1u);
    EXPECT_EQ(ifs.thenBody[0]->kind(), Stmt::Kind::Return);
}

// =====================================================================
// NameHeuristicsPass
// =====================================================================

TEST(NameHeuristicsPass, RenamesLoopCounter) {
    auto fn = mkFunc("test");

    auto forStmt = std::make_unique<ForStmt>();
    forStmt->init = mkVarDecl("_tmp0", mkLitInt("0"));
    forStmt->condition = mkBinOp(BinaryOp::Less, mkVarRef("_tmp0"), mkLitInt("10"));
    forStmt->increment = mkBinOp(BinaryOp::Add, mkVarRef("_tmp0"), mkLitInt("1"));

    auto call = std::make_unique<CallExpr>();
    call->callee = "println";
    forStmt->body.push_back(mkExprStmt(std::move(call)));
    fn.body.push_back(std::move(forStmt));

    NameHeuristicsPass pass;
    pass.run(fn);

    auto& fs = static_cast<ForStmt&>(*fn.body[0]);
    auto& init = static_cast<VarDeclStmt&>(*fs.init);
    EXPECT_EQ(init.name, "i");
}

TEST(NameHeuristicsPass, RenamesAccumulator) {
    auto fn = mkFunc("test");
    fn.body.push_back(mkVarDecl("_tmp0", mkLitInt("0")));

    auto forStmt = std::make_unique<ForStmt>();
    forStmt->init = mkVarDecl("i", mkLitInt("0"));
    forStmt->condition = mkBinOp(BinaryOp::Less, mkVarRef("i"), mkLitInt("10"));
    forStmt->increment = mkBinOp(BinaryOp::Add, mkVarRef("i"), mkLitInt("1"));

    forStmt->body.push_back(mkAssign("_tmp0", mkBinOp(BinaryOp::Add, mkVarRef("_tmp0"), mkVarRef("x"))));
    fn.body.push_back(std::move(forStmt));

    NameHeuristicsPass pass;
    pass.run(fn);

    auto& decl = static_cast<VarDeclStmt&>(*fn.body[0]);
    EXPECT_EQ(decl.name, "sum");
}

TEST(NameHeuristicsPass, RenamesBooleanVar) {
    auto fn = mkFunc("test");
    fn.body.push_back(mkVarDecl("_tmp0", mkLitBool(true)));
    fn.body.push_back(mkReturn(mkVarRef("_tmp0")));

    NameHeuristicsPass pass;
    pass.run(fn);

    auto& decl = static_cast<VarDeclStmt&>(*fn.body[0]);
    EXPECT_EQ(decl.name, "isReady");
}

TEST(NameHeuristicsPass, RenamesBooleanFromComparison) {
    auto fn = mkFunc("test");
    fn.body.push_back(mkVarDecl("_tmp0", mkBinOp(BinaryOp::Greater, mkVarRef("a"), mkVarRef("b"))));
    fn.body.push_back(mkReturn(mkVarRef("_tmp0")));

    NameHeuristicsPass pass;
    pass.run(fn);

    auto& decl = static_cast<VarDeclStmt&>(*fn.body[0]);
    EXPECT_EQ(decl.name, "isReady");
}

TEST(NameHeuristicsPass, RenamesIndexVar) {
    auto fn = mkFunc("test");
    fn.body.push_back(mkVarDecl("_tmp0", mkLitInt("5")));

    auto idx = std::make_unique<IndexExpr>();
    idx->object = mkVarRef("arr");
    idx->index = mkVarRef("_tmp0");
    fn.body.push_back(mkReturn(std::move(idx)));

    NameHeuristicsPass pass;
    pass.run(fn);

    auto& decl = static_cast<VarDeclStmt&>(*fn.body[0]);
    EXPECT_EQ(decl.name, "idx");
}

TEST(NameHeuristicsPass, SkipsNonGeneratedNames) {
    auto fn = mkFunc("test");
    fn.body.push_back(mkVarDecl("result", mkLitInt("0")));
    fn.body.push_back(mkReturn(mkVarRef("result")));

    NameHeuristicsPass pass;
    pass.run(fn);

    auto& decl = static_cast<VarDeclStmt&>(*fn.body[0]);
    EXPECT_EQ(decl.name, "result");
}

TEST(NameHeuristicsPass, MultipleLoopCounters) {
    auto fn = mkFunc("test");

    auto inner = std::make_unique<ForStmt>();
    inner->init = mkVarDecl("_tmp1", mkLitInt("0"));
    inner->condition = mkBinOp(BinaryOp::Less, mkVarRef("_tmp1"), mkLitInt("5"));
    inner->increment = mkBinOp(BinaryOp::Add, mkVarRef("_tmp1"), mkLitInt("1"));
    auto call = std::make_unique<CallExpr>();
    call->callee = "process";
    inner->body.push_back(mkExprStmt(std::move(call)));

    auto outer = std::make_unique<ForStmt>();
    outer->init = mkVarDecl("_tmp0", mkLitInt("0"));
    outer->condition = mkBinOp(BinaryOp::Less, mkVarRef("_tmp0"), mkLitInt("10"));
    outer->increment = mkBinOp(BinaryOp::Add, mkVarRef("_tmp0"), mkLitInt("1"));
    outer->body.push_back(std::move(inner));
    fn.body.push_back(std::move(outer));

    NameHeuristicsPass pass;
    pass.run(fn);

    auto& outerFs = static_cast<ForStmt&>(*fn.body[0]);
    auto& outerInit = static_cast<VarDeclStmt&>(*outerFs.init);
    EXPECT_EQ(outerInit.name, "i");

    auto& innerFs = static_cast<ForStmt&>(*outerFs.body[0]);
    auto& innerInit = static_cast<VarDeclStmt&>(*innerFs.init);
    EXPECT_EQ(innerInit.name, "j");
}

// =====================================================================
// DeadCodeElimPass
// =====================================================================

TEST(DeadCodeElimPass, RemovesAfterReturn) {
    auto fn = mkFunc("test");
    fn.body.push_back(mkReturn(mkLitInt("0")));
    fn.body.push_back(mkAssign("x", mkLitInt("1")));

    auto call = std::make_unique<CallExpr>();
    call->callee = "foo";
    fn.body.push_back(mkExprStmt(std::move(call)));

    DeadCodeElimPass pass;
    pass.run(fn);

    ASSERT_EQ(fn.body.size(), 1u);
    EXPECT_EQ(fn.body[0]->kind(), Stmt::Kind::Return);
}

TEST(DeadCodeElimPass, PreservesStatementsBeforeReturn) {
    auto fn = mkFunc("test");
    fn.body.push_back(mkAssign("x", mkLitInt("1")));
    fn.body.push_back(mkReturn(mkVarRef("x")));

    DeadCodeElimPass pass;
    pass.run(fn);

    EXPECT_EQ(fn.body.size(), 2u);
}

TEST(DeadCodeElimPass, ElimsInsideNestedBlocks) {
    auto fn = mkFunc("test");

    auto ifStmt = std::make_unique<IfStmt>();
    ifStmt->condition = mkVarRef("c");
    ifStmt->thenBody.push_back(mkReturn(mkLitInt("1")));
    ifStmt->thenBody.push_back(mkAssign("x", mkLitInt("2")));
    fn.body.push_back(std::move(ifStmt));

    DeadCodeElimPass pass;
    pass.run(fn);

    auto& ifs = static_cast<IfStmt&>(*fn.body[0]);
    ASSERT_EQ(ifs.thenBody.size(), 1u);
    EXPECT_EQ(ifs.thenBody[0]->kind(), Stmt::Kind::Return);
}

TEST(DeadCodeElimPass, IfReturningBothBranchesIsTerminator) {
    auto fn = mkFunc("test");

    auto ifStmt = std::make_unique<IfStmt>();
    ifStmt->condition = mkVarRef("c");
    ifStmt->thenBody.push_back(mkReturn(mkLitInt("1")));
    ifStmt->elseBody.push_back(mkReturn(mkLitInt("2")));
    fn.body.push_back(std::move(ifStmt));
    fn.body.push_back(mkAssign("x", mkLitInt("3")));

    DeadCodeElimPass pass;
    pass.run(fn);

    ASSERT_EQ(fn.body.size(), 1u);
    EXPECT_EQ(fn.body[0]->kind(), Stmt::Kind::If);
}

TEST(DeadCodeElimPass, NoChangeWhenNoDeadCode) {
    auto fn = mkFunc("test");
    fn.body.push_back(mkAssign("x", mkLitInt("1")));
    fn.body.push_back(mkAssign("y", mkLitInt("2")));

    DeadCodeElimPass pass;
    pass.run(fn);

    EXPECT_EQ(fn.body.size(), 2u);
}

TEST(DeadCodeElimPass, EmptyBodyNoOp) {
    auto fn = mkFunc("test");

    DeadCodeElimPass pass;
    pass.run(fn);

    EXPECT_EQ(fn.body.size(), 0u);
}

// =====================================================================
// ModelOptimizer pipeline
// =====================================================================

TEST(ModelOptimizer, IdiomaticPipelineRunsAllPasses) {
    TranspileModule mod;
    auto fn = mkFunc("test");
    fn.body.push_back(mkVarDecl("_tmp0", mkBinOp(BinaryOp::Add, mkVarRef("a"), mkVarRef("b"))));
    fn.body.push_back(mkReturn(mkVarRef("_tmp0")));
    mod.functions.push_back(std::move(fn));

    auto opt = ModelOptimizer::createIdiomaticPipeline();
    opt.optimize(mod);

    ASSERT_EQ(mod.functions[0].body.size(), 1u);
    EXPECT_EQ(mod.functions[0].body[0]->kind(), Stmt::Kind::Return);
}

TEST(ModelOptimizer, OptimizesMultipleFunctions) {
    TranspileModule mod;

    auto fn1 = mkFunc("func1");
    fn1.body.push_back(mkReturn(mkLitInt("1")));
    fn1.body.push_back(mkAssign("dead", mkLitInt("0")));
    mod.functions.push_back(std::move(fn1));

    auto fn2 = mkFunc("func2");
    fn2.body.push_back(mkReturn(mkLitInt("2")));
    fn2.body.push_back(mkAssign("dead", mkLitInt("0")));
    mod.functions.push_back(std::move(fn2));

    auto opt = ModelOptimizer::createIdiomaticPipeline();
    opt.optimize(mod);

    EXPECT_EQ(mod.functions[0].body.size(), 1u);
    EXPECT_EQ(mod.functions[1].body.size(), 1u);
}

TEST(ModelOptimizer, EmptyPipelineIsNoOp) {
    TranspileModule mod;
    auto fn = mkFunc("test");
    fn.body.push_back(mkReturn(mkLitInt("1")));
    fn.body.push_back(mkAssign("dead", mkLitInt("0")));
    mod.functions.push_back(std::move(fn));

    ModelOptimizer opt;
    opt.optimize(mod);

    EXPECT_EQ(mod.functions[0].body.size(), 2u);
}
