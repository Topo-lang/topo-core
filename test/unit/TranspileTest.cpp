#include "topo/Transpile/TranspileModel.h"
#include "topo/Transpile/TranspileModelJson.h"
#include "topo/Transpile/BackendLifter.h"
#include "topo/Sema/TypeBinder.h"
#include "topo/Basic/HostLanguage.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>

using namespace topo::transpile;

// =====================================================================
// Model construction tests
// =====================================================================

TEST(TranspileModel, LambdaExprConstruction) {
    auto lambda = std::make_unique<LambdaExpr>();
    lambda->captures = {{.name = "x", .mode = CaptureMode::ByValue}, {.name = "y", .mode = CaptureMode::ByReference}};
    topo::Parameter p;
    p.type.nameParts = {"int"};
    p.name = "a";
    lambda->params = {p};
    lambda->returnType.nameParts = {"int"};
    auto ret = std::make_unique<ReturnStmt>();
    auto ref = std::make_unique<VarRefExpr>();
    ref->name = "a";
    ret->value = std::move(ref);
    lambda->body.push_back(std::move(ret));
    EXPECT_EQ(lambda->kind(), Expr::Kind::Lambda);
    EXPECT_EQ(lambda->captures.size(), 2u);
    EXPECT_EQ(lambda->captures[0].name, "x");
    EXPECT_EQ(lambda->captures[0].mode, CaptureMode::ByValue);
    EXPECT_EQ(lambda->captures[1].mode, CaptureMode::ByReference);
}

TEST(TranspileModel, ThrowExprConstruction) {
    auto thrw = std::make_unique<ThrowExpr>();
    auto msg = std::make_unique<LiteralExpr>();
    msg->litKind = LiteralKind::String;
    msg->value = "error";
    thrw->operand = std::move(msg);
    EXPECT_EQ(thrw->kind(), Expr::Kind::Throw);
}

TEST(TranspileModel, TryCatchStmtConstruction) {
    auto tc = std::make_unique<TryCatchStmt>();
    auto body = std::make_unique<ExprStmt>();
    auto call = std::make_unique<CallExpr>();
    call->callee = "riskyOp";
    body->expr = std::move(call);
    tc->tryBody.push_back(std::move(body));

    CatchClause clause;
    clause.exceptionType.nameParts = {"std", "exception"};
    clause.varName = "e";
    auto handler = std::make_unique<ExprStmt>();
    auto logCall = std::make_unique<CallExpr>();
    logCall->callee = "log";
    handler->expr = std::move(logCall);
    clause.body.push_back(std::move(handler));
    tc->catchClauses.push_back(std::move(clause));

    EXPECT_EQ(tc->kind(), Stmt::Kind::TryCatch);
    EXPECT_EQ(tc->tryBody.size(), 1u);
    EXPECT_EQ(tc->catchClauses.size(), 1u);
    EXPECT_EQ(tc->catchClauses[0].varName, "e");
}

TEST(TranspileModel, AllExprKinds) {
    auto lit = std::make_unique<LiteralExpr>();
    lit->litKind = LiteralKind::Integer;
    lit->value = "42";
    EXPECT_EQ(lit->kind(), Expr::Kind::Literal);

    auto ref = std::make_unique<VarRefExpr>();
    ref->name = "x";
    EXPECT_EQ(ref->kind(), Expr::Kind::VarRef);

    auto bin = std::make_unique<BinaryOpExpr>();
    bin->op = BinaryOp::Add;
    bin->lhs = std::make_unique<LiteralExpr>();
    bin->rhs = std::make_unique<LiteralExpr>();
    EXPECT_EQ(bin->kind(), Expr::Kind::BinaryOp);

    auto un = std::make_unique<UnaryOpExpr>();
    un->op = UnaryOp::Negate;
    un->operand = std::make_unique<LiteralExpr>();
    EXPECT_EQ(un->kind(), Expr::Kind::UnaryOp);

    auto call = std::make_unique<CallExpr>();
    call->callee = "foo";
    EXPECT_EQ(call->kind(), Expr::Kind::Call);

    auto ma = std::make_unique<MemberAccessExpr>();
    ma->object = std::make_unique<VarRefExpr>();
    ma->member = "field";
    EXPECT_EQ(ma->kind(), Expr::Kind::MemberAccess);

    auto idx = std::make_unique<IndexExpr>();
    idx->object = std::make_unique<VarRefExpr>();
    idx->index = std::make_unique<LiteralExpr>();
    EXPECT_EQ(idx->kind(), Expr::Kind::Index);

    auto ctor = std::make_unique<ConstructExpr>();
    ctor->type.nameParts = {"MyType"};
    EXPECT_EQ(ctor->kind(), Expr::Kind::Construct);

    auto unsup = std::make_unique<UnsupportedExpr>();
    unsup->description = "inline assembly";
    EXPECT_EQ(unsup->kind(), Expr::Kind::Unsupported);
}

TEST(TranspileModel, AllStmtKinds) {
    auto vd = std::make_unique<VarDeclStmt>();
    vd->type.nameParts = {"int"};
    vd->name = "x";
    EXPECT_EQ(vd->kind(), Stmt::Kind::VarDecl);

    auto as = std::make_unique<AssignStmt>();
    as->target = std::make_unique<VarRefExpr>();
    as->value = std::make_unique<LiteralExpr>();
    EXPECT_EQ(as->kind(), Stmt::Kind::Assign);

    auto rs = std::make_unique<ReturnStmt>();
    EXPECT_EQ(rs->kind(), Stmt::Kind::Return);

    auto ifs = std::make_unique<IfStmt>();
    ifs->condition = std::make_unique<LiteralExpr>();
    EXPECT_EQ(ifs->kind(), Stmt::Kind::If);

    auto fs = std::make_unique<ForStmt>();
    EXPECT_EQ(fs->kind(), Stmt::Kind::For);

    auto ws = std::make_unique<WhileStmt>();
    ws->condition = std::make_unique<LiteralExpr>();
    EXPECT_EQ(ws->kind(), Stmt::Kind::While);

    auto es = std::make_unique<ExprStmt>();
    es->expr = std::make_unique<VarRefExpr>();
    EXPECT_EQ(es->kind(), Stmt::Kind::ExprStmt);
}

TEST(TranspileModel, TranspileFunctionConstruction) {
    TranspileFunction fn;
    fn.qualifiedName = "app::compute";
    fn.returnType.nameParts = {"int"};

    topo::Parameter p1;
    p1.type.nameParts = {"int"};
    p1.name = "a";
    topo::Parameter p2;
    p2.type.nameParts = {"int"};
    p2.name = "b";
    fn.params = {p1, p2};

    auto ret = std::make_unique<ReturnStmt>();
    auto add = std::make_unique<BinaryOpExpr>();
    add->op = BinaryOp::Add;
    auto lhs = std::make_unique<VarRefExpr>();
    lhs->name = "a";
    auto rhs = std::make_unique<VarRefExpr>();
    rhs->name = "b";
    add->lhs = std::move(lhs);
    add->rhs = std::move(rhs);
    ret->value = std::move(add);
    fn.body.push_back(std::move(ret));

    EXPECT_EQ(fn.qualifiedName, "app::compute");
    EXPECT_EQ(fn.params.size(), 2u);
    EXPECT_EQ(fn.body.size(), 1u);
}

TEST(TranspileModel, TranspileModuleConstruction) {
    TranspileModule mod;

    TranspileType ty;
    ty.qualifiedName = "app::Point";
    TranspileField fx;
    fx.type.nameParts = {"double"};
    fx.name = "x";
    TranspileField fy;
    fy.type.nameParts = {"double"};
    fy.name = "y";
    ty.fields = {fx, fy};
    mod.types.push_back(std::move(ty));

    TranspileFunction fn;
    fn.qualifiedName = "app::origin";
    fn.returnType.nameParts = {"app", "Point"};
    mod.functions.push_back(std::move(fn));

    EXPECT_EQ(mod.types.size(), 1u);
    EXPECT_EQ(mod.functions.size(), 1u);
    EXPECT_EQ(mod.types[0].fields.size(), 2u);
}

TEST(TranspileModel, FidelityDefaultIsSource) {
    LiteralExpr lit;
    EXPECT_EQ(lit.fidelity, Fidelity::Source);

    VarDeclStmt vd;
    EXPECT_EQ(vd.fidelity, Fidelity::Source);

    TranspileField field;
    EXPECT_EQ(field.fidelity, Fidelity::Source);

    TranspileType ty;
    EXPECT_EQ(ty.fidelity, Fidelity::Source);

    TranspileFunction fn;
    EXPECT_EQ(fn.fidelity, Fidelity::Source);
}

// =====================================================================
// JSON round-trip: Enums
// =====================================================================

TEST(TranspileJsonRoundTrip, FidelityAllValues) {
    for (auto fid : {Fidelity::Source, Fidelity::Recovered, Fidelity::Inferred}) {
        nlohmann::json j;
        to_json(j, fid);
        Fidelity back;
        from_json(j, back);
        EXPECT_EQ(back, fid);
    }
}

TEST(TranspileJsonRoundTrip, BinaryOpSample) {
    for (auto op : {BinaryOp::Add, BinaryOp::Eq, BinaryOp::And}) {
        nlohmann::json j;
        to_json(j, op);
        BinaryOp back;
        from_json(j, back);
        EXPECT_EQ(back, op);
    }
}

TEST(TranspileJsonRoundTrip, UnaryOpBoth) {
    for (auto op : {UnaryOp::Negate, UnaryOp::Not}) {
        nlohmann::json j;
        to_json(j, op);
        UnaryOp back;
        from_json(j, back);
        EXPECT_EQ(back, op);
    }
}

TEST(TranspileJsonRoundTrip, LiteralKindAll) {
    for (auto k : {LiteralKind::Integer, LiteralKind::Float, LiteralKind::Boolean, LiteralKind::String}) {
        nlohmann::json j;
        to_json(j, k);
        LiteralKind back;
        from_json(j, back);
        EXPECT_EQ(back, k);
    }
}

TEST(TranspileJsonRoundTrip, DecompileLevelAll) {
    for (auto l : {DecompileLevel::Direct, DecompileLevel::Structured, DecompileLevel::Idiomatic}) {
        nlohmann::json j;
        to_json(j, l);
        DecompileLevel back;
        from_json(j, back);
        EXPECT_EQ(back, l);
    }
}

// =====================================================================
// JSON round-trip: Expressions
// =====================================================================

TEST(TranspileJsonRoundTrip, LiteralExpr) {
    auto lit = std::make_unique<LiteralExpr>();
    lit->litKind = LiteralKind::Integer;
    lit->value = "42";

    auto j = serializeExpr(*lit);
    auto back = deserializeExpr(j);

    ASSERT_EQ(back->kind(), Expr::Kind::Literal);
    auto* b = static_cast<LiteralExpr*>(back.get());
    EXPECT_EQ(b->litKind, LiteralKind::Integer);
    EXPECT_EQ(b->value, "42");
}

TEST(TranspileJsonRoundTrip, VarRefExpr) {
    auto ref = std::make_unique<VarRefExpr>();
    ref->name = "counter";

    auto j = serializeExpr(*ref);
    auto back = deserializeExpr(j);

    ASSERT_EQ(back->kind(), Expr::Kind::VarRef);
    auto* b = static_cast<VarRefExpr*>(back.get());
    EXPECT_EQ(b->name, "counter");
}

TEST(TranspileJsonRoundTrip, BinaryOpExprNested) {
    auto bin = std::make_unique<BinaryOpExpr>();
    bin->op = BinaryOp::Mul;
    auto lhs = std::make_unique<LiteralExpr>();
    lhs->litKind = LiteralKind::Integer;
    lhs->value = "3";
    auto rhs = std::make_unique<LiteralExpr>();
    rhs->litKind = LiteralKind::Integer;
    rhs->value = "7";
    bin->lhs = std::move(lhs);
    bin->rhs = std::move(rhs);

    auto j = serializeExpr(*bin);
    auto back = deserializeExpr(j);

    ASSERT_EQ(back->kind(), Expr::Kind::BinaryOp);
    auto* b = static_cast<BinaryOpExpr*>(back.get());
    EXPECT_EQ(b->op, BinaryOp::Mul);
    ASSERT_EQ(b->lhs->kind(), Expr::Kind::Literal);
    ASSERT_EQ(b->rhs->kind(), Expr::Kind::Literal);
    EXPECT_EQ(static_cast<LiteralExpr*>(b->lhs.get())->value, "3");
    EXPECT_EQ(static_cast<LiteralExpr*>(b->rhs.get())->value, "7");
}

TEST(TranspileJsonRoundTrip, UnaryOpExpr) {
    auto un = std::make_unique<UnaryOpExpr>();
    un->op = UnaryOp::Not;
    auto operand = std::make_unique<VarRefExpr>();
    operand->name = "flag";
    un->operand = std::move(operand);

    auto j = serializeExpr(*un);
    auto back = deserializeExpr(j);

    ASSERT_EQ(back->kind(), Expr::Kind::UnaryOp);
    auto* b = static_cast<UnaryOpExpr*>(back.get());
    EXPECT_EQ(b->op, UnaryOp::Not);
    ASSERT_EQ(b->operand->kind(), Expr::Kind::VarRef);
    EXPECT_EQ(static_cast<VarRefExpr*>(b->operand.get())->name, "flag");
}

TEST(TranspileJsonRoundTrip, CallExprWithArgs) {
    auto call = std::make_unique<CallExpr>();
    call->callee = "math::add";
    auto a1 = std::make_unique<LiteralExpr>();
    a1->litKind = LiteralKind::Integer;
    a1->value = "1";
    auto a2 = std::make_unique<LiteralExpr>();
    a2->litKind = LiteralKind::Integer;
    a2->value = "2";
    call->args.push_back(std::move(a1));
    call->args.push_back(std::move(a2));

    auto j = serializeExpr(*call);
    auto back = deserializeExpr(j);

    ASSERT_EQ(back->kind(), Expr::Kind::Call);
    auto* b = static_cast<CallExpr*>(back.get());
    EXPECT_EQ(b->callee, "math::add");
    ASSERT_EQ(b->args.size(), 2u);
    EXPECT_EQ(static_cast<LiteralExpr*>(b->args[0].get())->value, "1");
    EXPECT_EQ(static_cast<LiteralExpr*>(b->args[1].get())->value, "2");
}

TEST(TranspileJsonRoundTrip, MemberAccessExpr) {
    auto ma = std::make_unique<MemberAccessExpr>();
    auto obj = std::make_unique<VarRefExpr>();
    obj->name = "point";
    ma->object = std::move(obj);
    ma->member = "x";

    auto j = serializeExpr(*ma);
    auto back = deserializeExpr(j);

    ASSERT_EQ(back->kind(), Expr::Kind::MemberAccess);
    auto* b = static_cast<MemberAccessExpr*>(back.get());
    EXPECT_EQ(b->member, "x");
    ASSERT_EQ(b->object->kind(), Expr::Kind::VarRef);
    EXPECT_EQ(static_cast<VarRefExpr*>(b->object.get())->name, "point");
}

TEST(TranspileJsonRoundTrip, IndexExpr) {
    auto idx = std::make_unique<IndexExpr>();
    auto obj = std::make_unique<VarRefExpr>();
    obj->name = "arr";
    auto ix = std::make_unique<LiteralExpr>();
    ix->litKind = LiteralKind::Integer;
    ix->value = "5";
    idx->object = std::move(obj);
    idx->index = std::move(ix);

    auto j = serializeExpr(*idx);
    auto back = deserializeExpr(j);

    ASSERT_EQ(back->kind(), Expr::Kind::Index);
    auto* b = static_cast<IndexExpr*>(back.get());
    ASSERT_EQ(b->object->kind(), Expr::Kind::VarRef);
    EXPECT_EQ(static_cast<VarRefExpr*>(b->object.get())->name, "arr");
    ASSERT_EQ(b->index->kind(), Expr::Kind::Literal);
    EXPECT_EQ(static_cast<LiteralExpr*>(b->index.get())->value, "5");
}

TEST(TranspileJsonRoundTrip, ConstructExpr) {
    auto ctor = std::make_unique<ConstructExpr>();
    ctor->type.nameParts = {"std", "Vec"};
    auto arg = std::make_unique<LiteralExpr>();
    arg->litKind = LiteralKind::Integer;
    arg->value = "10";
    ctor->args.push_back(std::move(arg));

    auto j = serializeExpr(*ctor);
    auto back = deserializeExpr(j);

    ASSERT_EQ(back->kind(), Expr::Kind::Construct);
    auto* b = static_cast<ConstructExpr*>(back.get());
    ASSERT_EQ(b->type.nameParts.size(), 2u);
    EXPECT_EQ(b->type.nameParts[0], "std");
    EXPECT_EQ(b->type.nameParts[1], "Vec");
    ASSERT_EQ(b->args.size(), 1u);
    EXPECT_EQ(static_cast<LiteralExpr*>(b->args[0].get())->value, "10");
}

TEST(TranspileJsonRoundTrip, UnsupportedExpr) {
    auto unsup = std::make_unique<UnsupportedExpr>();
    unsup->description = "platform-specific intrinsic";

    auto j = serializeExpr(*unsup);
    auto back = deserializeExpr(j);

    ASSERT_EQ(back->kind(), Expr::Kind::Unsupported);
    auto* b = static_cast<UnsupportedExpr*>(back.get());
    EXPECT_EQ(b->description, "platform-specific intrinsic");
}

// =====================================================================
// JSON round-trip: Statements
// =====================================================================

TEST(TranspileJsonRoundTrip, VarDeclStmtWithInit) {
    auto vd = std::make_unique<VarDeclStmt>();
    vd->type.nameParts = {"int"};
    vd->name = "count";
    auto init = std::make_unique<LiteralExpr>();
    init->litKind = LiteralKind::Integer;
    init->value = "0";
    vd->init = std::move(init);

    auto j = serializeStmt(*vd);
    auto back = deserializeStmt(j);

    ASSERT_EQ(back->kind(), Stmt::Kind::VarDecl);
    auto* b = static_cast<VarDeclStmt*>(back.get());
    EXPECT_EQ(b->type.nameParts, std::vector<std::string>{"int"});
    EXPECT_EQ(b->name, "count");
    ASSERT_NE(b->init, nullptr);
    EXPECT_EQ(b->init->kind(), Expr::Kind::Literal);
}

TEST(TranspileJsonRoundTrip, VarDeclStmtNoInit) {
    auto vd = std::make_unique<VarDeclStmt>();
    vd->type.nameParts = {"double"};
    vd->name = "value";
    // init left as nullptr

    auto j = serializeStmt(*vd);
    auto back = deserializeStmt(j);

    ASSERT_EQ(back->kind(), Stmt::Kind::VarDecl);
    auto* b = static_cast<VarDeclStmt*>(back.get());
    EXPECT_EQ(b->name, "value");
    EXPECT_EQ(b->init, nullptr);
}

TEST(TranspileJsonRoundTrip, AssignStmt) {
    auto as = std::make_unique<AssignStmt>();
    auto target = std::make_unique<VarRefExpr>();
    target->name = "x";
    auto val = std::make_unique<LiteralExpr>();
    val->litKind = LiteralKind::Float;
    val->value = "3.14";
    as->target = std::move(target);
    as->value = std::move(val);

    auto j = serializeStmt(*as);
    auto back = deserializeStmt(j);

    ASSERT_EQ(back->kind(), Stmt::Kind::Assign);
    auto* b = static_cast<AssignStmt*>(back.get());
    ASSERT_EQ(b->target->kind(), Expr::Kind::VarRef);
    EXPECT_EQ(static_cast<VarRefExpr*>(b->target.get())->name, "x");
    ASSERT_EQ(b->value->kind(), Expr::Kind::Literal);
    EXPECT_EQ(static_cast<LiteralExpr*>(b->value.get())->value, "3.14");
}

TEST(TranspileJsonRoundTrip, ReturnStmtWithValue) {
    auto rs = std::make_unique<ReturnStmt>();
    auto val = std::make_unique<VarRefExpr>();
    val->name = "result";
    rs->value = std::move(val);

    auto j = serializeStmt(*rs);
    auto back = deserializeStmt(j);

    ASSERT_EQ(back->kind(), Stmt::Kind::Return);
    auto* b = static_cast<ReturnStmt*>(back.get());
    ASSERT_NE(b->value, nullptr);
    EXPECT_EQ(b->value->kind(), Expr::Kind::VarRef);
}

TEST(TranspileJsonRoundTrip, ReturnStmtVoid) {
    auto rs = std::make_unique<ReturnStmt>();
    // value left as nullptr (void return)

    auto j = serializeStmt(*rs);
    auto back = deserializeStmt(j);

    ASSERT_EQ(back->kind(), Stmt::Kind::Return);
    auto* b = static_cast<ReturnStmt*>(back.get());
    EXPECT_EQ(b->value, nullptr);
}

TEST(TranspileJsonRoundTrip, ExprStmt) {
    auto es = std::make_unique<ExprStmt>();
    auto call = std::make_unique<CallExpr>();
    call->callee = "println";
    es->expr = std::move(call);

    auto j = serializeStmt(*es);
    auto back = deserializeStmt(j);

    ASSERT_EQ(back->kind(), Stmt::Kind::ExprStmt);
    auto* b = static_cast<ExprStmt*>(back.get());
    ASSERT_EQ(b->expr->kind(), Expr::Kind::Call);
    EXPECT_EQ(static_cast<CallExpr*>(b->expr.get())->callee, "println");
}

TEST(TranspileJsonRoundTrip, IfStmtWithElse) {
    auto ifs = std::make_unique<IfStmt>();
    auto cond = std::make_unique<VarRefExpr>();
    cond->name = "ready";
    ifs->condition = std::move(cond);

    auto thenRet = std::make_unique<ReturnStmt>();
    auto thenVal = std::make_unique<LiteralExpr>();
    thenVal->litKind = LiteralKind::Integer;
    thenVal->value = "1";
    thenRet->value = std::move(thenVal);
    ifs->thenBody.push_back(std::move(thenRet));

    auto elseRet = std::make_unique<ReturnStmt>();
    auto elseVal = std::make_unique<LiteralExpr>();
    elseVal->litKind = LiteralKind::Integer;
    elseVal->value = "0";
    elseRet->value = std::move(elseVal);
    ifs->elseBody.push_back(std::move(elseRet));

    auto j = serializeStmt(*ifs);
    auto back = deserializeStmt(j);

    ASSERT_EQ(back->kind(), Stmt::Kind::If);
    auto* b = static_cast<IfStmt*>(back.get());
    ASSERT_EQ(b->condition->kind(), Expr::Kind::VarRef);
    EXPECT_EQ(static_cast<VarRefExpr*>(b->condition.get())->name, "ready");
    EXPECT_EQ(b->thenBody.size(), 1u);
    EXPECT_EQ(b->elseBody.size(), 1u);
}

TEST(TranspileJsonRoundTrip, ForStmt) {
    auto fs = std::make_unique<ForStmt>();

    // init: int i = 0
    auto init = std::make_unique<VarDeclStmt>();
    init->type.nameParts = {"int"};
    init->name = "i";
    auto initVal = std::make_unique<LiteralExpr>();
    initVal->litKind = LiteralKind::Integer;
    initVal->value = "0";
    init->init = std::move(initVal);
    fs->init = std::move(init);

    // condition: i < 10
    auto cond = std::make_unique<BinaryOpExpr>();
    cond->op = BinaryOp::Less;
    auto condLhs = std::make_unique<VarRefExpr>();
    condLhs->name = "i";
    auto condRhs = std::make_unique<LiteralExpr>();
    condRhs->litKind = LiteralKind::Integer;
    condRhs->value = "10";
    cond->lhs = std::move(condLhs);
    cond->rhs = std::move(condRhs);
    fs->condition = std::move(cond);

    // increment: i + 1
    auto inc = std::make_unique<BinaryOpExpr>();
    inc->op = BinaryOp::Add;
    auto incLhs = std::make_unique<VarRefExpr>();
    incLhs->name = "i";
    auto incRhs = std::make_unique<LiteralExpr>();
    incRhs->litKind = LiteralKind::Integer;
    incRhs->value = "1";
    inc->lhs = std::move(incLhs);
    inc->rhs = std::move(incRhs);
    fs->increment = std::move(inc);

    // body: println()
    auto body = std::make_unique<ExprStmt>();
    auto call = std::make_unique<CallExpr>();
    call->callee = "println";
    body->expr = std::move(call);
    fs->body.push_back(std::move(body));

    auto j = serializeStmt(*fs);
    auto back = deserializeStmt(j);

    ASSERT_EQ(back->kind(), Stmt::Kind::For);
    auto* b = static_cast<ForStmt*>(back.get());
    ASSERT_NE(b->init, nullptr);
    EXPECT_EQ(b->init->kind(), Stmt::Kind::VarDecl);
    ASSERT_NE(b->condition, nullptr);
    EXPECT_EQ(b->condition->kind(), Expr::Kind::BinaryOp);
    ASSERT_NE(b->increment, nullptr);
    EXPECT_EQ(b->increment->kind(), Expr::Kind::BinaryOp);
    EXPECT_EQ(b->body.size(), 1u);
}

TEST(TranspileJsonRoundTrip, WhileStmt) {
    auto ws = std::make_unique<WhileStmt>();
    auto cond = std::make_unique<VarRefExpr>();
    cond->name = "running";
    ws->condition = std::move(cond);

    auto body = std::make_unique<ExprStmt>();
    auto call = std::make_unique<CallExpr>();
    call->callee = "tick";
    body->expr = std::move(call);
    ws->body.push_back(std::move(body));

    auto j = serializeStmt(*ws);
    auto back = deserializeStmt(j);

    ASSERT_EQ(back->kind(), Stmt::Kind::While);
    auto* b = static_cast<WhileStmt*>(back.get());
    ASSERT_EQ(b->condition->kind(), Expr::Kind::VarRef);
    EXPECT_EQ(static_cast<VarRefExpr*>(b->condition.get())->name, "running");
    EXPECT_EQ(b->body.size(), 1u);
}

// =====================================================================
// JSON round-trip: Module
// =====================================================================

TEST(TranspileJsonRoundTrip, FullModule) {
    TranspileModule mod;

    // Type: Point { x: double, y: double }
    TranspileType ty;
    ty.qualifiedName = "geom::Point";
    TranspileField fx;
    fx.type.nameParts = {"double"};
    fx.name = "x";
    TranspileField fy;
    fy.type.nameParts = {"double"};
    fy.name = "y";
    ty.fields = {fx, fy};
    mod.types.push_back(std::move(ty));

    // Function: distance(a: Point, b: Point) -> double
    TranspileFunction fn;
    fn.qualifiedName = "geom::distance";
    fn.returnType.nameParts = {"double"};
    topo::Parameter pa;
    pa.type.nameParts = {"geom", "Point"};
    pa.name = "a";
    topo::Parameter pb;
    pb.type.nameParts = {"geom", "Point"};
    pb.name = "b";
    fn.params = {pa, pb};

    // body: var dx = a.x - b.x; var dy = a.y - b.y; return sqrt(dx*dx + dy*dy);
    auto vdx = std::make_unique<VarDeclStmt>();
    vdx->type.nameParts = {"double"};
    vdx->name = "dx";
    auto dxInit = std::make_unique<BinaryOpExpr>();
    dxInit->op = BinaryOp::Sub;
    auto ax = std::make_unique<MemberAccessExpr>();
    auto aRef = std::make_unique<VarRefExpr>();
    aRef->name = "a";
    ax->object = std::move(aRef);
    ax->member = "x";
    auto bx = std::make_unique<MemberAccessExpr>();
    auto bRef = std::make_unique<VarRefExpr>();
    bRef->name = "b";
    bx->object = std::move(bRef);
    bx->member = "x";
    dxInit->lhs = std::move(ax);
    dxInit->rhs = std::move(bx);
    vdx->init = std::move(dxInit);
    fn.body.push_back(std::move(vdx));

    auto vdy = std::make_unique<VarDeclStmt>();
    vdy->type.nameParts = {"double"};
    vdy->name = "dy";
    auto dyInit = std::make_unique<LiteralExpr>();
    dyInit->litKind = LiteralKind::Float;
    dyInit->value = "0.0";
    vdy->init = std::move(dyInit);
    fn.body.push_back(std::move(vdy));

    auto ret = std::make_unique<ReturnStmt>();
    auto retVal = std::make_unique<VarRefExpr>();
    retVal->name = "dx";
    ret->value = std::move(retVal);
    fn.body.push_back(std::move(ret));

    mod.functions.push_back(std::move(fn));

    // Round-trip
    auto j = serializeModule(mod);
    auto back = deserializeModule(j);

    // Verify types
    ASSERT_EQ(back.types.size(), 1u);
    EXPECT_EQ(back.types[0].qualifiedName, "geom::Point");
    ASSERT_EQ(back.types[0].fields.size(), 2u);
    EXPECT_EQ(back.types[0].fields[0].name, "x");
    EXPECT_EQ(back.types[0].fields[0].type.nameParts, std::vector<std::string>{"double"});
    EXPECT_EQ(back.types[0].fields[1].name, "y");

    // Verify functions
    ASSERT_EQ(back.functions.size(), 1u);
    EXPECT_EQ(back.functions[0].qualifiedName, "geom::distance");
    EXPECT_EQ(back.functions[0].returnType.nameParts, std::vector<std::string>{"double"});
    ASSERT_EQ(back.functions[0].params.size(), 2u);
    EXPECT_EQ(back.functions[0].params[0].name, "a");
    EXPECT_EQ(back.functions[0].params[1].name, "b");
    EXPECT_EQ(back.functions[0].params[0].type.nameParts, (std::vector<std::string>{"geom", "Point"}));

    // Verify body
    ASSERT_EQ(back.functions[0].body.size(), 3u);
    EXPECT_EQ(back.functions[0].body[0]->kind(), Stmt::Kind::VarDecl);
    EXPECT_EQ(back.functions[0].body[1]->kind(), Stmt::Kind::VarDecl);
    EXPECT_EQ(back.functions[0].body[2]->kind(), Stmt::Kind::Return);
}

// =====================================================================
// JSON round-trip: throwsClause (Java checked-exception list)
// =====================================================================

TEST(TranspileJsonRoundTrip, ThrowsClausePreserved) {
    TranspileFunction fn;
    fn.qualifiedName = "io::readFile";
    fn.returnType.nameParts = {"void"};
    topo::TypeNode ioe;
    ioe.nameParts = {"IOException"};
    topo::TypeNode sqle;
    sqle.nameParts = {"SQLException"};
    fn.throwsClause = {ioe, sqle};

    nlohmann::json j;
    to_json(j, fn);
    TranspileFunction back;
    from_json(j, back);

    ASSERT_EQ(back.throwsClause.size(), 2u);
    EXPECT_EQ(back.throwsClause[0].nameParts,
              std::vector<std::string>{"IOException"});
    EXPECT_EQ(back.throwsClause[1].nameParts,
              std::vector<std::string>{"SQLException"});
}

TEST(TranspileJsonRoundTrip, EmptyThrowsClauseRoundTrips) {
    TranspileFunction fn;
    fn.qualifiedName = "pure::compute";
    fn.returnType.nameParts = {"int"};
    // throwsClause left empty

    nlohmann::json j;
    to_json(j, fn);
    EXPECT_FALSE(j.contains("throwsClause"))
        << "empty throwsClause should be omitted from JSON";
    TranspileFunction back;
    from_json(j, back);
    EXPECT_TRUE(back.throwsClause.empty());
}

// =====================================================================
// JSON round-trip: templateParams (declaration-site generic type params)
// =====================================================================

TEST(TranspileJsonRoundTrip, FunctionTemplateParamsPreserved) {
    TranspileFunction fn;
    fn.qualifiedName = "util::identity";
    fn.returnType.nameParts = {"T"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    topo::TemplateParamDecl u;
    u.kind = topo::TemplateParamDecl::TypeParam;
    u.name = "U";
    fn.templateParams = {t, u};

    nlohmann::json j;
    to_json(j, fn);
    TranspileFunction back;
    from_json(j, back);

    ASSERT_EQ(back.templateParams.size(), 2u);
    EXPECT_EQ(back.templateParams[0].name, "T");
    EXPECT_EQ(back.templateParams[0].kind, topo::TemplateParamDecl::TypeParam);
    EXPECT_EQ(back.templateParams[1].name, "U");
}

TEST(TranspileJsonRoundTrip, TypeTemplateParamsPreserved) {
    TranspileType ty;
    ty.qualifiedName = "container::Box";
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    ty.templateParams = {t};

    nlohmann::json j;
    to_json(j, ty);
    TranspileType back;
    from_json(j, back);

    ASSERT_EQ(back.templateParams.size(), 1u);
    EXPECT_EQ(back.templateParams[0].name, "T");
}

TEST(TranspileJsonRoundTrip, TemplateParamBoundRoundTrip) {
    TranspileFunction fn;
    fn.qualifiedName = "util::compare";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    t.constraintType.nameParts = {"Clone"};
    fn.templateParams = {t};

    nlohmann::json j;
    to_json(j, fn);
    ASSERT_TRUE(j.contains("templateParams"));
    ASSERT_EQ(j["templateParams"].size(), 1u);
    EXPECT_TRUE(j["templateParams"][0].contains("bound"))
        << "type-param bound must be carried on the wire";

    TranspileFunction back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    EXPECT_EQ(back.templateParams[0].constraintType.nameParts,
              std::vector<std::string>({"Clone"}));
}

TEST(TranspileJsonRoundTrip, TemplateParamDefaultRoundTrip) {
    TranspileType ty;
    ty.qualifiedName = "container::Box";
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    topo::TypeNode def;
    def.nameParts = {"i32"};
    t.defaultType = def;
    ty.templateParams = {t};

    nlohmann::json j;
    to_json(j, ty);
    ASSERT_TRUE(j.contains("templateParams"));
    ASSERT_EQ(j["templateParams"].size(), 1u);
    EXPECT_TRUE(j["templateParams"][0].contains("default"))
        << "type-param default must be carried on the wire";

    TranspileType back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    ASSERT_TRUE(back.templateParams[0].defaultType.has_value());
    EXPECT_EQ(back.templateParams[0].defaultType->nameParts,
              std::vector<std::string>({"i32"}));
}

TEST(TranspileJsonRoundTrip, TemplateParamWithoutDefaultOmitsKey) {
    TranspileType ty;
    ty.qualifiedName = "container::Box";
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    ty.templateParams = {t};

    nlohmann::json j;
    to_json(j, ty);
    ASSERT_TRUE(j.contains("templateParams"));
    EXPECT_FALSE(j["templateParams"][0].contains("default"))
        << "absent default must be omitted (byte-identical to pre-default wire)";
}

TEST(TranspileJsonRoundTrip, NonTypeParamDefaultValueRoundTrip) {
    // NonTypeParam default literal expression (`<int N = 10>` /
    // `<const N: usize = 16>`). The wire key `defaultValue` is a string
    // (source-literal spelling) — separate from `default` (TypeNode for
    // TypeParam) so the two semantic axes never collide.
    TranspileType ty;
    ty.qualifiedName = "container::Buf";
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::NonTypeParam;
    t.name = "N";
    t.constraintType.nameParts = {"usize"};
    t.defaultValue = "16";
    ty.templateParams = {t};

    nlohmann::json j;
    to_json(j, ty);
    ASSERT_TRUE(j.contains("templateParams"));
    ASSERT_EQ(j["templateParams"].size(), 1u);
    ASSERT_TRUE(j["templateParams"][0].contains("defaultValue"))
        << "NonTypeParam default literal must be carried on the wire";
    EXPECT_EQ(j["templateParams"][0]["defaultValue"], "16");
    // `default` (TypeNode key) must NOT appear — they are independent
    // wire keys with different shapes.
    EXPECT_FALSE(j["templateParams"][0].contains("default"))
        << "`default` is TypeParam-only; NonTypeParam uses `defaultValue`";

    TranspileType back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    EXPECT_EQ(back.templateParams[0].kind,
              topo::TemplateParamDecl::NonTypeParam);
    ASSERT_TRUE(back.templateParams[0].defaultValue.has_value());
    EXPECT_EQ(*back.templateParams[0].defaultValue, "16");
    EXPECT_FALSE(back.templateParams[0].defaultType.has_value());
}

TEST(TranspileJsonRoundTrip, NonTypeParamWithoutDefaultValueOmitsKey) {
    // A NonTypeParam without a default emits no `defaultValue` key —
    // byte-identical to the wire payload for `<const N: usize>` before the
    // default-literal feature was added.
    TranspileType ty;
    ty.qualifiedName = "container::Buf";
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::NonTypeParam;
    t.name = "N";
    t.constraintType.nameParts = {"usize"};
    ty.templateParams = {t};

    nlohmann::json j;
    to_json(j, ty);
    ASSERT_TRUE(j.contains("templateParams"));
    EXPECT_FALSE(j["templateParams"][0].contains("defaultValue"))
        << "absent NonTypeParam default must be omitted "
           "(byte-identical to pre-feature wire)";
}

TEST(TranspileJsonRoundTrip, TemplateParamMultiBoundRoundTrip) {
    // Multi-bound: Rust `T: A + B`, Java/TS `T extends A & B`. The wire
    // graduates from `bound: TypeNode` to `bounds: [TypeNode]` only when
    // the parameter actually carries more than one bound — single-bound
    // payloads stay byte-identical (the legacy `bound` key is preserved).
    TranspileFunction fn;
    fn.qualifiedName = "util::sort";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    t.constraintType.nameParts = {"Clone"};
    topo::TypeNode eb;
    eb.nameParts = {"Debug"};
    t.extraBounds = {eb};
    fn.templateParams = {t};

    nlohmann::json j;
    to_json(j, fn);
    ASSERT_TRUE(j.contains("templateParams"));
    ASSERT_EQ(j["templateParams"].size(), 1u);
    EXPECT_FALSE(j["templateParams"][0].contains("bound"))
        << "multi-bound must drop the legacy `bound` key and use `bounds`";
    EXPECT_TRUE(j["templateParams"][0].contains("bounds"))
        << "multi-bound must surface on the wire as `bounds: [...]`";
    EXPECT_EQ(j["templateParams"][0]["bounds"].size(), 2u);

    TranspileFunction back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    EXPECT_EQ(back.templateParams[0].constraintType.nameParts,
              std::vector<std::string>({"Clone"}));
    ASSERT_EQ(back.templateParams[0].extraBounds.size(), 1u);
    EXPECT_EQ(back.templateParams[0].extraBounds[0].nameParts,
              std::vector<std::string>({"Debug"}));
}

TEST(TranspileJsonRoundTrip, SingleBoundStaysOnLegacyBoundKey) {
    // The byte-identical wire guarantee: a single-bound param keeps the
    // `bound` key and never grows a `bounds` key. (Multi-bound payloads
    // are the ones that opt into the new key.)
    TranspileFunction fn;
    fn.qualifiedName = "util::cmp";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    t.constraintType.nameParts = {"Clone"};
    fn.templateParams = {t};

    nlohmann::json j;
    to_json(j, fn);
    EXPECT_TRUE(j["templateParams"][0].contains("bound"));
    EXPECT_FALSE(j["templateParams"][0].contains("bounds"))
        << "single-bound must stay on `bound`, never escalating to `bounds`";
}

TEST(TranspileJsonRoundTrip, BoundsKeyTakesPrecedenceOverLegacyBound) {
    // Forward compatibility: if a payload mistakenly contains both
    // `bound` and `bounds`, the reader prefers `bounds` (multi-bound
    // is the authoritative form going forward).
    nlohmann::json tp = {
        {"kind", "type"}, {"name", "T"},
        {"bound", {{"nameParts", {"Legacy"}}}},
        {"bounds", nlohmann::json::array({
            nlohmann::json{{"nameParts", {"Clone"}}},
            nlohmann::json{{"nameParts", {"Debug"}}}})}
    };
    nlohmann::json j = {
        {"qualifiedName", "util::f"},
        {"returnType", {{"nameParts", {"void"}}}},
        {"params", nlohmann::json::array()},
        {"body", nlohmann::json::array()},
        {"unsupported", nlohmann::json::array()},
        {"fidelity", "source"},
        {"templateParams", nlohmann::json::array({tp})}
    };

    TranspileFunction back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    EXPECT_EQ(back.templateParams[0].constraintType.nameParts,
              std::vector<std::string>({"Clone"}))
        << "`bounds` must override the legacy `bound` key";
    ASSERT_EQ(back.templateParams[0].extraBounds.size(), 1u);
    EXPECT_EQ(back.templateParams[0].extraBounds[0].nameParts,
              std::vector<std::string>({"Debug"}));
}

TEST(TranspileJsonRoundTrip, TemplateParamWithoutBoundOmitsKey) {
    TranspileFunction fn;
    fn.qualifiedName = "util::identity";
    fn.returnType.nameParts = {"T"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    fn.templateParams = {t};

    nlohmann::json j;
    to_json(j, fn);
    ASSERT_TRUE(j.contains("templateParams"));
    EXPECT_FALSE(j["templateParams"][0].contains("bound"))
        << "absent bound must be omitted (byte-identical to pre-bounds wire)";
}

// ---------------------------------------------------------------------------
// JSON round-trip: Rust lifetime params + lifetime bounds
// ---------------------------------------------------------------------------

// `pub fn borrow<'a>(...)` — bare lifetime param with no outlives clause.
// kind="lifetime" carries the bare name ("a", apostrophe-free on the wire);
// the absence of an outlives target means the `bound` key is omitted so the
// payload stays byte-minimal for the common case.
TEST(TranspileJsonRoundTrip, TemplateParamLifetimeBareRoundTrip) {
    TranspileFunction fn;
    fn.qualifiedName = "util::borrow";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::LifetimeParam;
    t.name = "a";
    fn.templateParams = {t};

    nlohmann::json j;
    to_json(j, fn);
    ASSERT_TRUE(j.contains("templateParams"));
    ASSERT_EQ(j["templateParams"].size(), 1u);
    EXPECT_EQ(j["templateParams"][0]["kind"], "lifetime");
    EXPECT_EQ(j["templateParams"][0]["name"], "a");
    EXPECT_FALSE(j["templateParams"][0].contains("bound"))
        << "bare lifetime (no outlives) must omit the `bound` key";

    TranspileFunction back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    EXPECT_EQ(back.templateParams[0].kind,
              topo::TemplateParamDecl::LifetimeParam);
    EXPECT_EQ(back.templateParams[0].name, "a");
    EXPECT_TRUE(back.templateParams[0].constraintType.nameParts.empty());
}

// `<'a: 'b>` — lifetime-on-lifetime outlives. The outlives target rides
// the same `bound: TypeNode` wire key as type-param trait bounds; nameParts
// keeps the apostrophe on the outlives target (`["'b"]`) so the wire
// distinguishes lifetime references from plain type names at a glance.
TEST(TranspileJsonRoundTrip, TemplateParamLifetimeOutlivesRoundTrip) {
    TranspileFunction fn;
    fn.qualifiedName = "util::outlives";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl la;
    la.kind = topo::TemplateParamDecl::LifetimeParam;
    la.name = "a";
    la.constraintType.nameParts = {"'b"};
    topo::TemplateParamDecl lb;
    lb.kind = topo::TemplateParamDecl::LifetimeParam;
    lb.name = "b";
    fn.templateParams = {la, lb};

    nlohmann::json j;
    to_json(j, fn);
    ASSERT_TRUE(j["templateParams"][0].contains("bound"));
    EXPECT_EQ(j["templateParams"][0]["bound"]["nameParts"][0], "'b")
        << "outlives target keeps the apostrophe so the wire is unambiguous";

    TranspileFunction back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 2u);
    EXPECT_EQ(back.templateParams[0].kind,
              topo::TemplateParamDecl::LifetimeParam);
    EXPECT_EQ(back.templateParams[0].constraintType.nameParts,
              std::vector<std::string>({"'b"}));
    EXPECT_EQ(back.templateParams[1].kind,
              topo::TemplateParamDecl::LifetimeParam);
    EXPECT_TRUE(back.templateParams[1].constraintType.nameParts.empty());
}

// `<T: 'a>` — type param with a lifetime bound. The bound rides the
// type-param's `bound` (single) or `bounds` (multi-with-trait) list as a
// TypeNode whose nameParts[0] starts with `'`; non-Rust emitters drop
// these entries but the wire serializer carries them faithfully.
TEST(TranspileJsonRoundTrip, TemplateParamTypeWithLifetimeBoundRoundTrip) {
    TranspileType ty;
    ty.qualifiedName = "Holder";
    topo::TemplateParamDecl la;
    la.kind = topo::TemplateParamDecl::LifetimeParam;
    la.name = "a";
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    t.constraintType.nameParts = {"'a"};
    ty.templateParams = {la, t};

    nlohmann::json j;
    to_json(j, ty);
    ASSERT_EQ(j["templateParams"].size(), 2u);
    EXPECT_EQ(j["templateParams"][1]["bound"]["nameParts"][0], "'a")
        << "type-on-lifetime bound carries the apostrophe verbatim";

    TranspileType back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 2u);
    EXPECT_EQ(back.templateParams[0].kind,
              topo::TemplateParamDecl::LifetimeParam);
    EXPECT_EQ(back.templateParams[1].kind,
              topo::TemplateParamDecl::TypeParam);
    EXPECT_EQ(back.templateParams[1].constraintType.nameParts,
              std::vector<std::string>({"'a"}));
}

// Byte-identical contract: every TemplateParamDecl payload that has no
// lifetime data must serialize EXACTLY as it did before the lifetime
// feature was added. A bare type-param (`<T>`) and a single-trait-bound
// type-param (`<T: Clone>`) are the two highest-traffic shapes — both
// must round-trip without introducing a `kind: "lifetime"` slot or any
// other new key.
TEST(TranspileJsonRoundTrip, NoLifetimePayloadStaysByteIdentical) {
    TranspileFunction fn;
    fn.qualifiedName = "util::identity";
    fn.returnType.nameParts = {"T"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    fn.templateParams = {t};

    nlohmann::json j;
    to_json(j, fn);
    ASSERT_EQ(j["templateParams"].size(), 1u);
    EXPECT_EQ(j["templateParams"][0]["kind"], "type")
        << "non-lifetime payload must keep kind=type — the lifetime variant "
           "must never leak into legacy serialization";
    EXPECT_FALSE(j["templateParams"][0].contains("bound"));
    EXPECT_FALSE(j["templateParams"][0].contains("bounds"));
    EXPECT_FALSE(j["templateParams"][0].contains("default"));
}

// ---------------------------------------------------------------------------
// JSON round-trip: TypeNode.assocBindings (Rust `Iterator<Item = u8>`)
// ---------------------------------------------------------------------------

TEST(TranspileJsonRoundTrip, TypeNodeAssocBindingsEmptyOmitsKey) {
    // Defensive byte-identical guarantee: a bare TypeNode (no assoc
    // bindings) must NOT carry an `assocBindings` key on the wire. This
    // is the byte-identical contract for every pre-feature payload — a
    // single-bound trait without associated-type clauses, every parameter
    // type, every field type, etc.
    TranspileFunction fn;
    fn.qualifiedName = "util::compare";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    t.constraintType.nameParts = {"Clone"};
    fn.templateParams = {t};

    nlohmann::json j;
    to_json(j, fn);
    ASSERT_TRUE(j["templateParams"][0].contains("bound"));
    EXPECT_FALSE(j["templateParams"][0]["bound"].contains("assocBindings"))
        << "empty assocBindings must be omitted; pre-feature payloads stay byte-identical";
    EXPECT_FALSE(j["returnType"].contains("assocBindings"));
}

TEST(TranspileJsonRoundTrip, TypeNodeAssocBindingsSingleRoundTrip) {
    // `<T: Iterator<Item = u8>>` — the headline case. The bound TypeNode
    // carries `assocBindings: [{name: "Item", type: {nameParts: ["u8"]}}]`
    // and the JSON round-trip must preserve the binding's name and the
    // recursive TypeNode payload.
    TranspileFunction fn;
    fn.qualifiedName = "util::collect";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    t.constraintType.nameParts = {"Iterator"};
    topo::TypeNode::RecordField item;
    item.name = "Item";
    topo::TypeNode u8;
    u8.nameParts = {"u8"};
    item.typeBox.push_back(u8);
    t.constraintType.assocBindings = {item};
    fn.templateParams = {t};

    nlohmann::json j;
    to_json(j, fn);
    ASSERT_TRUE(j["templateParams"][0]["bound"].contains("assocBindings"));
    ASSERT_EQ(j["templateParams"][0]["bound"]["assocBindings"].size(), 1u);
    EXPECT_EQ(j["templateParams"][0]["bound"]["assocBindings"][0]["name"], "Item");
    EXPECT_EQ(j["templateParams"][0]["bound"]["assocBindings"][0]["type"]["nameParts"],
              nlohmann::json::array({"u8"}));

    TranspileFunction back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    ASSERT_EQ(back.templateParams[0].constraintType.assocBindings.size(), 1u);
    EXPECT_EQ(back.templateParams[0].constraintType.assocBindings[0].name, "Item");
    EXPECT_EQ(back.templateParams[0].constraintType.assocBindings[0].type().nameParts,
              std::vector<std::string>({"u8"}));
}

TEST(TranspileJsonRoundTrip, TypeNodeAssocBindingsRecursiveRoundTrip) {
    // The binding's TypeNode is itself a TypeNode and recurses naturally —
    // `Container<Item = optional<u8>, Key = i64>` round-trips both the
    // nested templateArgs inside the binding type AND the multiple-binding
    // ordering.
    topo::TypeNode bound;
    bound.nameParts = {"Container"};

    topo::TypeNode::RecordField itemBinding;
    itemBinding.name = "Item";
    topo::TypeNode optionalU8;
    optionalU8.nameParts = {"optional"};
    topo::TypeNode u8;
    u8.nameParts = {"u8"};
    optionalU8.templateArgs = {u8};
    itemBinding.typeBox.push_back(optionalU8);

    topo::TypeNode::RecordField keyBinding;
    keyBinding.name = "Key";
    topo::TypeNode i64;
    i64.nameParts = {"i64"};
    keyBinding.typeBox.push_back(i64);

    bound.assocBindings = {itemBinding, keyBinding};

    TranspileFunction fn;
    fn.qualifiedName = "util::pull";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    t.constraintType = bound;
    fn.templateParams = {t};

    nlohmann::json j;
    to_json(j, fn);
    const auto& abs = j["templateParams"][0]["bound"]["assocBindings"];
    ASSERT_EQ(abs.size(), 2u);
    EXPECT_EQ(abs[0]["name"], "Item");
    EXPECT_EQ(abs[1]["name"], "Key");
    EXPECT_EQ(abs[0]["type"]["nameParts"], nlohmann::json::array({"optional"}));
    EXPECT_EQ(abs[0]["type"]["templateArgs"][0]["nameParts"], nlohmann::json::array({"u8"}));

    TranspileFunction back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    const auto& bb = back.templateParams[0].constraintType.assocBindings;
    ASSERT_EQ(bb.size(), 2u);
    EXPECT_EQ(bb[0].name, "Item");
    EXPECT_EQ(bb[0].type().nameParts, std::vector<std::string>({"optional"}));
    ASSERT_EQ(bb[0].type().templateArgs.size(), 1u);
    EXPECT_EQ(bb[0].type().templateArgs[0].nameParts, std::vector<std::string>({"u8"}));
    EXPECT_EQ(bb[1].name, "Key");
    EXPECT_EQ(bb[1].type().nameParts, std::vector<std::string>({"i64"}));
}

TEST(TranspileJsonRoundTrip, TypeNodeAssocBindingsCoexistMultiBound) {
    // Multi-bound + assoc binding coexistence: `<T: Iterator<Item = u8> +
    // Clone>` — assoc binding rides only on Iterator (constraintType),
    // not on Clone (extraBounds[0]). Verifies the per-bound granularity
    // and the byte-identical Clone bound stays clean.
    TranspileFunction fn;
    fn.qualifiedName = "util::stream";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";

    topo::TypeNode iter;
    iter.nameParts = {"Iterator"};
    topo::TypeNode::RecordField item;
    item.name = "Item";
    topo::TypeNode u8;
    u8.nameParts = {"u8"};
    item.typeBox.push_back(u8);
    iter.assocBindings = {item};
    t.constraintType = iter;

    topo::TypeNode clone;
    clone.nameParts = {"Clone"};
    t.extraBounds = {clone};
    fn.templateParams = {t};

    nlohmann::json j;
    to_json(j, fn);
    // Multi-bound payload — `bounds: [Iterator, Clone]` (legacy `bound` key
    // suppressed). Iterator carries assocBindings; Clone does not.
    ASSERT_TRUE(j["templateParams"][0].contains("bounds"));
    ASSERT_EQ(j["templateParams"][0]["bounds"].size(), 2u);
    EXPECT_TRUE(j["templateParams"][0]["bounds"][0].contains("assocBindings"));
    EXPECT_FALSE(j["templateParams"][0]["bounds"][1].contains("assocBindings"))
        << "Clone bound must not silently inherit Iterator's assocBindings";

    TranspileFunction back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    ASSERT_EQ(back.templateParams[0].constraintType.assocBindings.size(), 1u);
    EXPECT_EQ(back.templateParams[0].constraintType.assocBindings[0].name, "Item");
    ASSERT_EQ(back.templateParams[0].extraBounds.size(), 1u);
    EXPECT_TRUE(back.templateParams[0].extraBounds[0].assocBindings.empty());
}

// ---------------------------------------------------------------------------
// JSON round-trip: TypeNode.hrtbLifetimes (Rust `for<'a, 'b> Fn(...)`)
// ---------------------------------------------------------------------------

TEST(TranspileJsonRoundTrip, TypeNodeHrtbLifetimesEmptyOmitsKey) {
    // Defensive byte-identical guarantee: a bound TypeNode without HRTB
    // lifetimes must NOT carry an `hrtbLifetimes` key on the wire. Every
    // pre-Phase-4 payload (single bound, multi bound, assoc bindings, plain
    // parameter / field / return type) must stay byte-identical.
    TranspileFunction fn;
    fn.qualifiedName = "util::compare";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    t.constraintType.nameParts = {"Clone"};
    fn.templateParams = {t};

    nlohmann::json j;
    to_json(j, fn);
    ASSERT_TRUE(j["templateParams"][0].contains("bound"));
    EXPECT_FALSE(j["templateParams"][0]["bound"].contains("hrtbLifetimes"))
        << "empty hrtbLifetimes must be omitted; pre-HRTB payloads stay byte-identical";
    EXPECT_FALSE(j["returnType"].contains("hrtbLifetimes"));
}

TEST(TranspileJsonRoundTrip, TypeNodeHrtbLifetimesSingleRoundTrip) {
    // `<F: for<'a> Fn(...)>` — single HRTB lifetime captured on the bound
    // TypeNode as `hrtbLifetimes: ["a"]` (no leading apostrophe; the `'` is
    // added at emit time). Round-trips through to_json/from_json and survives
    // the standard from_json tolerance for missing keys.
    TranspileFunction fn;
    fn.qualifiedName = "util::map";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "F";
    t.constraintType.nameParts = {"Fn"};
    t.constraintType.hrtbLifetimes = {"a"};
    fn.templateParams = {t};

    nlohmann::json j;
    to_json(j, fn);
    ASSERT_TRUE(j["templateParams"][0]["bound"].contains("hrtbLifetimes"));
    EXPECT_EQ(j["templateParams"][0]["bound"]["hrtbLifetimes"],
              nlohmann::json::array({"a"}));

    TranspileFunction back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    EXPECT_EQ(back.templateParams[0].constraintType.hrtbLifetimes,
              std::vector<std::string>({"a"}));
}

TEST(TranspileJsonRoundTrip, TypeNodeHrtbLifetimesMultiRoundTrip) {
    // `for<'a, 'b>` — multiple HRTB lifetimes preserve order through the
    // wire. The vector is stored verbatim (sans-apostrophe) and the emitter
    // re-adds the `'` for each entry, joining with `, `.
    topo::TypeNode bound;
    bound.nameParts = {"Trait"};
    bound.hrtbLifetimes = {"a", "b"};

    TranspileFunction fn;
    fn.qualifiedName = "util::run";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    t.constraintType = bound;
    fn.templateParams = {t};

    nlohmann::json j;
    to_json(j, fn);
    EXPECT_EQ(j["templateParams"][0]["bound"]["hrtbLifetimes"],
              nlohmann::json::array({"a", "b"}));

    TranspileFunction back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    EXPECT_EQ(back.templateParams[0].constraintType.hrtbLifetimes,
              std::vector<std::string>({"a", "b"}));
}

TEST(TranspileJsonRoundTrip, TypeNodeHrtbLifetimesRecursiveRoundTrip) {
    // HRTB on a parenthesised Fn-trait — the Rust extractor encodes the
    // inputs as positional `templateArgs` and the output as a synthesised
    // `Output` assocBinding. The HRTB lifetime label only ever sits on the
    // *outer* trait-bound TypeNode; recursing into inputs / output keeps
    // their own `hrtbLifetimes` empty.
    topo::TypeNode bound;
    bound.nameParts = {"Fn"};
    bound.hrtbLifetimes = {"a"};

    topo::TypeNode argTy;
    argTy.nameParts = {"&", "u8"};
    bound.templateArgs = {argTy};

    topo::TypeNode::RecordField outBinding;
    outBinding.name = "Output";
    topo::TypeNode outTy;
    outTy.nameParts = {"&", "u8"};
    outBinding.typeBox.push_back(outTy);
    bound.assocBindings = {outBinding};

    TranspileFunction fn;
    fn.qualifiedName = "util::run";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "F";
    t.constraintType = bound;
    fn.templateParams = {t};

    nlohmann::json j;
    to_json(j, fn);
    const auto& jb = j["templateParams"][0]["bound"];
    EXPECT_EQ(jb["hrtbLifetimes"], nlohmann::json::array({"a"}));
    EXPECT_EQ(jb["nameParts"], nlohmann::json::array({"Fn"}));
    ASSERT_EQ(jb["templateArgs"].size(), 1u);
    EXPECT_EQ(jb["templateArgs"][0]["nameParts"],
              nlohmann::json::array({"&", "u8"}));
    EXPECT_FALSE(jb["templateArgs"][0].contains("hrtbLifetimes"))
        << "inner TypeNodes inherit no HRTB from outer trait bound";
    ASSERT_EQ(jb["assocBindings"].size(), 1u);
    EXPECT_EQ(jb["assocBindings"][0]["name"], "Output");
    EXPECT_EQ(jb["assocBindings"][0]["type"]["nameParts"],
              nlohmann::json::array({"&", "u8"}));

    TranspileFunction back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    const auto& bb = back.templateParams[0].constraintType;
    EXPECT_EQ(bb.hrtbLifetimes, std::vector<std::string>({"a"}));
    EXPECT_EQ(bb.nameParts, std::vector<std::string>({"Fn"}));
    ASSERT_EQ(bb.templateArgs.size(), 1u);
    EXPECT_TRUE(bb.templateArgs[0].hrtbLifetimes.empty());
    ASSERT_EQ(bb.assocBindings.size(), 1u);
    EXPECT_EQ(bb.assocBindings[0].name, "Output");
    EXPECT_EQ(bb.assocBindings[0].type().nameParts,
              std::vector<std::string>({"&", "u8"}));
}

TEST(TranspileJsonRoundTrip, EmptyTemplateParamsOmittedAndRoundTrips) {
    TranspileFunction fn;
    fn.qualifiedName = "plain::add";
    fn.returnType.nameParts = {"int"};
    TranspileType ty;
    ty.qualifiedName = "plain::Pair";

    nlohmann::json jf, jt;
    to_json(jf, fn);
    to_json(jt, ty);
    EXPECT_FALSE(jf.contains("templateParams"))
        << "empty templateParams should be omitted (byte-identical to pre-generics)";
    EXPECT_FALSE(jt.contains("templateParams"));

    TranspileFunction fb;
    from_json(jf, fb);
    TranspileType tb;
    from_json(jt, tb);
    EXPECT_TRUE(fb.templateParams.empty());
    EXPECT_TRUE(tb.templateParams.empty());
}

// =====================================================================
// JSON round-trip: Fidelity preservation
// =====================================================================

TEST(TranspileJsonRoundTrip, FidelityPreservedOnExpr) {
    auto lit = std::make_unique<LiteralExpr>();
    lit->litKind = LiteralKind::Integer;
    lit->value = "99";
    lit->fidelity = Fidelity::Recovered;

    auto j = serializeExpr(*lit);
    auto back = deserializeExpr(j);

    EXPECT_EQ(back->fidelity, Fidelity::Recovered);
}

TEST(TranspileJsonRoundTrip, FidelityPreservedOnStmt) {
    auto vd = std::make_unique<VarDeclStmt>();
    vd->type.nameParts = {"int"};
    vd->name = "n";
    vd->fidelity = Fidelity::Inferred;

    auto j = serializeStmt(*vd);
    auto back = deserializeStmt(j);

    EXPECT_EQ(back->fidelity, Fidelity::Inferred);
}

// =====================================================================
// TypeBinder Java tests
// =====================================================================

TEST(TypeBinderJava, IntegerResolvesToInt) {
    auto binder = topo::TypeBinder::createDefault(topo::HostLanguage::Java);
    auto result = binder.resolve("integer");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "int");
}

TEST(TypeBinderJava, UnsignedResolvesToInt) {
    auto binder = topo::TypeBinder::createDefault(topo::HostLanguage::Java);
    auto result = binder.resolve("unsigned");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "int");
}

TEST(TypeBinderJava, FloatingResolvesToDouble) {
    auto binder = topo::TypeBinder::createDefault(topo::HostLanguage::Java);
    auto result = binder.resolve("floating");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "double");
}

TEST(TypeBinderJava, BooleanResolvesToBoolean) {
    auto binder = topo::TypeBinder::createDefault(topo::HostLanguage::Java);
    auto result = binder.resolve("boolean");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "boolean");
}

TEST(TypeBinderJava, TextResolvesToString) {
    auto binder = topo::TypeBinder::createDefault(topo::HostLanguage::Java);
    auto result = binder.resolve("text");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "String");
}

TEST(TypeBinderJava, UnknownNameReturnsNullopt) {
    auto binder = topo::TypeBinder::createDefault(topo::HostLanguage::Java);
    auto result = binder.resolve("foobar");
    EXPECT_FALSE(result.has_value());
}

// =====================================================================
// JSON round-trip: Lambda, Throw, TryCatch
// =====================================================================

TEST(TranspileJsonRoundTrip, LambdaExpr) {
    auto lambda = std::make_unique<LambdaExpr>();
    lambda->captures = {{.name = "x", .mode = CaptureMode::ByValue}, {.name = "y", .mode = CaptureMode::ByReference}};
    topo::Parameter p;
    p.type.nameParts = {"int"};
    p.name = "a";
    lambda->params = {p};
    lambda->returnType.nameParts = {"int"};
    auto ret = std::make_unique<ReturnStmt>();
    auto ref = std::make_unique<VarRefExpr>();
    ref->name = "a";
    ret->value = std::move(ref);
    lambda->body.push_back(std::move(ret));

    auto j = serializeExpr(*lambda);
    auto back = deserializeExpr(j);

    ASSERT_EQ(back->kind(), Expr::Kind::Lambda);
    auto* b = static_cast<LambdaExpr*>(back.get());
    ASSERT_EQ(b->captures.size(), 2u);
    EXPECT_EQ(b->captures[0].name, "x");
    EXPECT_EQ(b->captures[0].mode, CaptureMode::ByValue);
    EXPECT_EQ(b->captures[1].name, "y");
    EXPECT_EQ(b->captures[1].mode, CaptureMode::ByReference);
    ASSERT_EQ(b->params.size(), 1u);
    EXPECT_EQ(b->params[0].name, "a");
    EXPECT_EQ(b->returnType.nameParts, std::vector<std::string>{"int"});
    ASSERT_EQ(b->body.size(), 1u);
    EXPECT_EQ(b->body[0]->kind(), Stmt::Kind::Return);
}

TEST(TranspileJsonRoundTrip, ThrowExpr) {
    auto thrw = std::make_unique<ThrowExpr>();
    auto msg = std::make_unique<LiteralExpr>();
    msg->litKind = LiteralKind::String;
    msg->value = "something failed";
    thrw->operand = std::move(msg);

    auto j = serializeExpr(*thrw);
    auto back = deserializeExpr(j);

    ASSERT_EQ(back->kind(), Expr::Kind::Throw);
    auto* b = static_cast<ThrowExpr*>(back.get());
    ASSERT_EQ(b->operand->kind(), Expr::Kind::Literal);
    EXPECT_EQ(static_cast<LiteralExpr*>(b->operand.get())->value, "something failed");
}

TEST(TranspileJsonRoundTrip, CaptureModeAll) {
    for (auto m : {CaptureMode::ByValue, CaptureMode::ByReference}) {
        nlohmann::json j;
        to_json(j, m);
        CaptureMode back;
        from_json(j, back);
        EXPECT_EQ(back, m);
    }
}

TEST(TranspileJsonRoundTrip, TryCatchStmt) {
    auto tc = std::make_unique<TryCatchStmt>();

    // try body
    auto tryBody = std::make_unique<ExprStmt>();
    auto call = std::make_unique<CallExpr>();
    call->callee = "riskyOp";
    tryBody->expr = std::move(call);
    tc->tryBody.push_back(std::move(tryBody));

    // catch clause
    CatchClause clause;
    clause.exceptionType.nameParts = {"std", "exception"};
    clause.varName = "e";
    auto handler = std::make_unique<ExprStmt>();
    auto logCall = std::make_unique<CallExpr>();
    logCall->callee = "log";
    handler->expr = std::move(logCall);
    clause.body.push_back(std::move(handler));
    tc->catchClauses.push_back(std::move(clause));

    // finally body
    auto cleanup = std::make_unique<ExprStmt>();
    auto cleanupCall = std::make_unique<CallExpr>();
    cleanupCall->callee = "cleanup";
    cleanup->expr = std::move(cleanupCall);
    tc->finallyBody.push_back(std::move(cleanup));

    auto j = serializeStmt(*tc);
    auto back = deserializeStmt(j);

    ASSERT_EQ(back->kind(), Stmt::Kind::TryCatch);
    auto* b = static_cast<TryCatchStmt*>(back.get());
    ASSERT_EQ(b->tryBody.size(), 1u);
    EXPECT_EQ(b->tryBody[0]->kind(), Stmt::Kind::ExprStmt);
    ASSERT_EQ(b->catchClauses.size(), 1u);
    EXPECT_EQ(b->catchClauses[0].exceptionType.nameParts, (std::vector<std::string>{"std", "exception"}));
    EXPECT_EQ(b->catchClauses[0].varName, "e");
    ASSERT_EQ(b->catchClauses[0].body.size(), 1u);
    ASSERT_EQ(b->finallyBody.size(), 1u);
}

TEST(TranspileJsonRoundTrip, TryCatchStmtNoFinally) {
    auto tc = std::make_unique<TryCatchStmt>();

    auto tryBody = std::make_unique<ReturnStmt>();
    tc->tryBody.push_back(std::move(tryBody));

    CatchClause clause;
    clause.exceptionType.nameParts = {"Error"};
    clause.varName = "err";
    tc->catchClauses.push_back(std::move(clause));
    // finallyBody left empty

    auto j = serializeStmt(*tc);
    auto back = deserializeStmt(j);

    ASSERT_EQ(back->kind(), Stmt::Kind::TryCatch);
    auto* b = static_cast<TryCatchStmt*>(back.get());
    EXPECT_EQ(b->tryBody.size(), 1u);
    EXPECT_EQ(b->catchClauses.size(), 1u);
    EXPECT_TRUE(b->finallyBody.empty());
}

// =====================================================================
// JSON round-trip: TranspileType inheritance hierarchy (baseClasses)
// =====================================================================

TEST(TranspileJsonRoundTrip, TranspileTypeBaseClassesPreserved) {
    // class Dog extends Animal implements Comparable, Serializable
    TranspileType ty;
    ty.qualifiedName = "Dog";
    TranspileField fx;
    fx.type.nameParts = {"int"};
    fx.name = "age";
    ty.fields = {fx};

    topo::TypeNode base;
    base.nameParts = {"Animal"};
    topo::TypeNode i1;
    i1.nameParts = {"Comparable"};
    topo::TypeNode i2;
    i2.nameParts = {"Serializable"};
    ty.baseClasses = {base, i1, i2};

    nlohmann::json j;
    to_json(j, ty);
    TranspileType back;
    from_json(j, back);

    EXPECT_EQ(back.qualifiedName, "Dog");
    ASSERT_EQ(back.fields.size(), 1u);
    EXPECT_EQ(back.fields[0].name, "age");
    ASSERT_EQ(back.baseClasses.size(), 3u);
    EXPECT_EQ(back.baseClasses[0].nameParts, std::vector<std::string>{"Animal"});
    EXPECT_EQ(back.baseClasses[1].nameParts, std::vector<std::string>{"Comparable"});
    EXPECT_EQ(back.baseClasses[2].nameParts, std::vector<std::string>{"Serializable"});
}

TEST(TranspileJsonRoundTrip, TranspileTypeEmptyBaseClassesOmittedAndCompat) {
    // Empty baseClasses must be omitted from JSON (byte-compatible with
    // pre-inheritance payloads), and old JSON lacking the key must
    // deserialize to an empty baseClasses vector.
    TranspileType ty;
    ty.qualifiedName = "geom::Point";
    TranspileField fx;
    fx.type.nameParts = {"double"};
    fx.name = "x";
    ty.fields = {fx};

    nlohmann::json j;
    to_json(j, ty);
    EXPECT_FALSE(j.contains("baseClasses"))
        << "empty baseClasses must be omitted from JSON: " << j.dump();

    // Simulate an old payload (no baseClasses key at all).
    nlohmann::json old;
    old["qualifiedName"] = "geom::Point";
    old["fields"] = j["fields"];
    old["fidelity"] = j["fidelity"];
    TranspileType back;
    from_json(old, back);
    EXPECT_EQ(back.qualifiedName, "geom::Point");
    EXPECT_TRUE(back.baseClasses.empty());
}

TEST(TranspileJsonRoundTrip, TranspileTypeBaseClassKindsPreserved) {
    // class Dog extends Animal implements Comparable — discriminator must
    // round-trip parallel to baseClasses, as a "class"/"interface" array.
    TranspileType ty;
    ty.qualifiedName = "Dog";
    topo::TypeNode base;
    base.nameParts = {"Animal"};
    topo::TypeNode iface;
    iface.nameParts = {"Comparable"};
    ty.baseClasses = {base, iface};
    ty.baseClassKinds = {BaseClassKind::Class, BaseClassKind::Interface};

    nlohmann::json j;
    to_json(j, ty);
    ASSERT_TRUE(j.contains("baseClassKinds")) << j.dump();
    EXPECT_EQ(j["baseClassKinds"][0], "class");
    EXPECT_EQ(j["baseClassKinds"][1], "interface");

    TranspileType back;
    from_json(j, back);
    ASSERT_EQ(back.baseClassKinds.size(), 2u);
    EXPECT_EQ(back.baseClassKinds[0], BaseClassKind::Class);
    EXPECT_EQ(back.baseClassKinds[1], BaseClassKind::Interface);
}

TEST(TranspileJsonRoundTrip, TranspileTypeEmptyBaseClassKindsOmittedAndCompat) {
    // Empty baseClassKinds must be omitted (byte-compatible with
    // pre-discriminator payloads); old JSON lacking the key deserializes to
    // an empty vector so emitters fall back to the legacy heuristic.
    TranspileType ty;
    ty.qualifiedName = "Plain";
    nlohmann::json j;
    to_json(j, ty);
    EXPECT_FALSE(j.contains("baseClassKinds"))
        << "empty baseClassKinds must be omitted: " << j.dump();

    // Old payload with baseClasses but NO baseClassKinds key.
    nlohmann::json old;
    old["qualifiedName"] = "Plain";
    old["fields"] = nlohmann::json::array();
    old["baseClasses"] = nlohmann::json::array();
    old["baseClasses"].push_back(nlohmann::json{{"nameParts", {"Base"}}});
    old["fidelity"] = j["fidelity"];
    TranspileType back;
    from_json(old, back);
    EXPECT_EQ(back.baseClasses.size(), 1u);
    EXPECT_TRUE(back.baseClassKinds.empty());
}
