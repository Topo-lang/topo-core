#include "topo/Transpile/TranspileModel.h"
#include "topo/Transpile/TranspileModelJson.h"
#include "CppEmitter.h"
#include "RustEmitter.h"
#include "JavaEmitter.h"
#include "PythonEmitter.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>

using namespace topo::transpile;

// =====================================================================
// Helpers
// =====================================================================

static std::unique_ptr<LiteralExpr> intLit(const std::string& v) {
    auto e = std::make_unique<LiteralExpr>();
    e->litKind = LiteralKind::Integer;
    e->value = v;
    return e;
}

static std::unique_ptr<VarRefExpr> varRef(const std::string& name) {
    auto e = std::make_unique<VarRefExpr>();
    e->name = name;
    return e;
}

// =====================================================================
// JSON round-trip: BreakStmt / ContinueStmt
// =====================================================================

TEST(TranspileNewConstructsRoundTrip, BreakStmt) {
    auto brk = std::make_unique<BreakStmt>();
    brk->fidelity = Fidelity::Source;

    auto j = serializeStmt(*brk);
    auto back = deserializeStmt(j);

    ASSERT_EQ(back->kind(), Stmt::Kind::Break);
    EXPECT_EQ(back->fidelity, Fidelity::Source);
}

TEST(TranspileNewConstructsRoundTrip, BreakStmtRecoveredFidelity) {
    auto brk = std::make_unique<BreakStmt>();
    brk->fidelity = Fidelity::Recovered;

    auto j = serializeStmt(*brk);
    auto back = deserializeStmt(j);

    ASSERT_EQ(back->kind(), Stmt::Kind::Break);
    EXPECT_EQ(back->fidelity, Fidelity::Recovered);
}

TEST(TranspileNewConstructsRoundTrip, ContinueStmt) {
    auto cont = std::make_unique<ContinueStmt>();
    cont->fidelity = Fidelity::Source;

    auto j = serializeStmt(*cont);
    auto back = deserializeStmt(j);

    ASSERT_EQ(back->kind(), Stmt::Kind::Continue);
    EXPECT_EQ(back->fidelity, Fidelity::Source);
}

// =====================================================================
// JSON round-trip: SwitchStmt
// =====================================================================

TEST(TranspileNewConstructsRoundTrip, SwitchStmtWithCasesAndDefault) {
    auto sw = std::make_unique<SwitchStmt>();
    sw->subject = varRef("mode");

    // Case 1: value=1, body=return "one"
    SwitchCase c1;
    c1.value = intLit("1");
    auto r1 = std::make_unique<ReturnStmt>();
    auto s1 = std::make_unique<LiteralExpr>();
    s1->litKind = LiteralKind::String;
    s1->value = "one";
    r1->value = std::move(s1);
    c1.body.push_back(std::move(r1));

    // Case 2: value=2, body=return "two"
    SwitchCase c2;
    c2.value = intLit("2");
    auto r2 = std::make_unique<ReturnStmt>();
    auto s2 = std::make_unique<LiteralExpr>();
    s2->litKind = LiteralKind::String;
    s2->value = "two";
    r2->value = std::move(s2);
    c2.body.push_back(std::move(r2));

    // Case 3: value=3, body=break
    SwitchCase c3;
    c3.value = intLit("3");
    c3.body.push_back(std::make_unique<BreakStmt>());

    // Default: body=return "other"
    SwitchCase cdef;
    // value is nullptr for default
    auto rd = std::make_unique<ReturnStmt>();
    auto sd = std::make_unique<LiteralExpr>();
    sd->litKind = LiteralKind::String;
    sd->value = "other";
    rd->value = std::move(sd);
    cdef.body.push_back(std::move(rd));

    sw->cases.push_back(std::move(c1));
    sw->cases.push_back(std::move(c2));
    sw->cases.push_back(std::move(c3));
    sw->cases.push_back(std::move(cdef));

    auto j = serializeStmt(*sw);
    auto back = deserializeStmt(j);

    ASSERT_EQ(back->kind(), Stmt::Kind::Switch);
    auto* b = static_cast<SwitchStmt*>(back.get());
    ASSERT_EQ(b->subject->kind(), Expr::Kind::VarRef);
    EXPECT_EQ(static_cast<VarRefExpr*>(b->subject.get())->name, "mode");

    ASSERT_EQ(b->cases.size(), 4u);

    // Case 1
    ASSERT_NE(b->cases[0].value, nullptr);
    EXPECT_EQ(static_cast<LiteralExpr*>(b->cases[0].value.get())->value, "1");
    ASSERT_EQ(b->cases[0].body.size(), 1u);
    EXPECT_EQ(b->cases[0].body[0]->kind(), Stmt::Kind::Return);

    // Case 2
    ASSERT_NE(b->cases[1].value, nullptr);
    EXPECT_EQ(static_cast<LiteralExpr*>(b->cases[1].value.get())->value, "2");

    // Case 3
    ASSERT_NE(b->cases[2].value, nullptr);
    EXPECT_EQ(static_cast<LiteralExpr*>(b->cases[2].value.get())->value, "3");
    ASSERT_EQ(b->cases[2].body.size(), 1u);
    EXPECT_EQ(b->cases[2].body[0]->kind(), Stmt::Kind::Break);

    // Default case
    EXPECT_EQ(b->cases[3].value, nullptr);
    ASSERT_EQ(b->cases[3].body.size(), 1u);
    EXPECT_EQ(b->cases[3].body[0]->kind(), Stmt::Kind::Return);
}

// =====================================================================
// JSON round-trip: TernaryExpr
// =====================================================================

TEST(TranspileNewConstructsRoundTrip, TernaryExpr) {
    auto tern = std::make_unique<TernaryExpr>();
    tern->condition = varRef("flag");
    tern->trueExpr = intLit("1");
    tern->falseExpr = intLit("0");

    auto j = serializeExpr(*tern);
    auto back = deserializeExpr(j);

    ASSERT_EQ(back->kind(), Expr::Kind::Ternary);
    auto* b = static_cast<TernaryExpr*>(back.get());
    ASSERT_EQ(b->condition->kind(), Expr::Kind::VarRef);
    EXPECT_EQ(static_cast<VarRefExpr*>(b->condition.get())->name, "flag");
    ASSERT_EQ(b->trueExpr->kind(), Expr::Kind::Literal);
    EXPECT_EQ(static_cast<LiteralExpr*>(b->trueExpr.get())->value, "1");
    ASSERT_EQ(b->falseExpr->kind(), Expr::Kind::Literal);
    EXPECT_EQ(static_cast<LiteralExpr*>(b->falseExpr.get())->value, "0");
}

TEST(TranspileNewConstructsRoundTrip, TernaryExprFidelityPreserved) {
    auto tern = std::make_unique<TernaryExpr>();
    tern->fidelity = Fidelity::Inferred;
    tern->condition = varRef("x");
    tern->trueExpr = intLit("10");
    tern->falseExpr = intLit("20");

    auto j = serializeExpr(*tern);
    auto back = deserializeExpr(j);

    EXPECT_EQ(back->fidelity, Fidelity::Inferred);
}

// =====================================================================
// JSON round-trip: CompoundAssignExpr (all op variants)
// =====================================================================

TEST(TranspileNewConstructsRoundTrip, CompoundAssignAllOps) {
    BinaryOp ops[] = {BinaryOp::Add,
                      BinaryOp::Sub,
                      BinaryOp::Mul,
                      BinaryOp::Div,
                      BinaryOp::Mod,
                      BinaryOp::BitAnd,
                      BinaryOp::BitOr,
                      BinaryOp::BitXor,
                      BinaryOp::Shl,
                      BinaryOp::Shr};

    for (auto op : ops) {
        auto ca = std::make_unique<CompoundAssignExpr>();
        ca->op = op;
        ca->target = varRef("x");
        ca->value = intLit("1");

        auto j = serializeExpr(*ca);
        auto back = deserializeExpr(j);

        ASSERT_EQ(back->kind(), Expr::Kind::CompoundAssign) << "Failed for op variant";
        auto* b = static_cast<CompoundAssignExpr*>(back.get());
        EXPECT_EQ(b->op, op);
        ASSERT_EQ(b->target->kind(), Expr::Kind::VarRef);
        EXPECT_EQ(static_cast<VarRefExpr*>(b->target.get())->name, "x");
        ASSERT_EQ(b->value->kind(), Expr::Kind::Literal);
        EXPECT_EQ(static_cast<LiteralExpr*>(b->value.get())->value, "1");
    }
}

// =====================================================================
// JSON round-trip: BinaryOp BitAnd/BitOr/BitXor/Shl/Shr
// =====================================================================

TEST(TranspileNewConstructsRoundTrip, BitwiseBinaryOps) {
    BinaryOp ops[] = {BinaryOp::BitAnd, BinaryOp::BitOr, BinaryOp::BitXor, BinaryOp::Shl, BinaryOp::Shr};

    for (auto op : ops) {
        auto bin = std::make_unique<BinaryOpExpr>();
        bin->op = op;
        bin->lhs = varRef("a");
        bin->rhs = varRef("b");

        auto j = serializeExpr(*bin);
        auto back = deserializeExpr(j);

        ASSERT_EQ(back->kind(), Expr::Kind::BinaryOp);
        auto* b = static_cast<BinaryOpExpr*>(back.get());
        EXPECT_EQ(b->op, op);
        ASSERT_EQ(b->lhs->kind(), Expr::Kind::VarRef);
        EXPECT_EQ(static_cast<VarRefExpr*>(b->lhs.get())->name, "a");
        ASSERT_EQ(b->rhs->kind(), Expr::Kind::VarRef);
        EXPECT_EQ(static_cast<VarRefExpr*>(b->rhs.get())->name, "b");
    }
}

// =====================================================================
// JSON round-trip: UnaryOp BitNot
// =====================================================================

TEST(TranspileNewConstructsRoundTrip, UnaryBitNot) {
    auto un = std::make_unique<UnaryOpExpr>();
    un->op = UnaryOp::BitNot;
    un->operand = varRef("mask");

    auto j = serializeExpr(*un);
    auto back = deserializeExpr(j);

    ASSERT_EQ(back->kind(), Expr::Kind::UnaryOp);
    auto* b = static_cast<UnaryOpExpr*>(back.get());
    EXPECT_EQ(b->op, UnaryOp::BitNot);
    ASSERT_EQ(b->operand->kind(), Expr::Kind::VarRef);
    EXPECT_EQ(static_cast<VarRefExpr*>(b->operand.get())->name, "mask");
}

// =====================================================================
// JSON round-trip: Module containing new constructs
// =====================================================================

TEST(TranspileNewConstructsRoundTrip, ModuleWithNewConstructs) {
    TranspileModule mod;

    TranspileFunction fn;
    fn.qualifiedName = "test::dispatch";
    fn.returnType.nameParts = {"int"};

    topo::Parameter p;
    p.type.nameParts = {"int"};
    p.name = "cmd";
    fn.params = {p};

    // switch (cmd) { case 1: x += 1; break; default: continue; }
    auto sw = std::make_unique<SwitchStmt>();
    sw->subject = varRef("cmd");

    SwitchCase c1;
    c1.value = intLit("1");
    auto ca = std::make_unique<CompoundAssignExpr>();
    ca->op = BinaryOp::Add;
    ca->target = varRef("x");
    ca->value = intLit("1");
    auto es = std::make_unique<ExprStmt>();
    es->expr = std::move(ca);
    c1.body.push_back(std::move(es));
    c1.body.push_back(std::make_unique<BreakStmt>());

    SwitchCase cdef;
    cdef.body.push_back(std::make_unique<ContinueStmt>());

    sw->cases.push_back(std::move(c1));
    sw->cases.push_back(std::move(cdef));
    fn.body.push_back(std::move(sw));

    // return (flag ? 1 : 0);
    auto ret = std::make_unique<ReturnStmt>();
    auto tern = std::make_unique<TernaryExpr>();
    tern->condition = varRef("flag");
    tern->trueExpr = intLit("1");
    tern->falseExpr = intLit("0");
    ret->value = std::move(tern);
    fn.body.push_back(std::move(ret));

    mod.functions.push_back(std::move(fn));

    // Round-trip
    auto j = serializeModule(mod);
    auto back = deserializeModule(j);

    ASSERT_EQ(back.functions.size(), 1u);
    EXPECT_EQ(back.functions[0].qualifiedName, "test::dispatch");
    ASSERT_EQ(back.functions[0].body.size(), 2u);
    EXPECT_EQ(back.functions[0].body[0]->kind(), Stmt::Kind::Switch);
    EXPECT_EQ(back.functions[0].body[1]->kind(), Stmt::Kind::Return);

    // Verify switch deserialized correctly
    auto* swBack = static_cast<SwitchStmt*>(back.functions[0].body[0].get());
    ASSERT_EQ(swBack->cases.size(), 2u);
    ASSERT_EQ(swBack->cases[0].body.size(), 2u);
    EXPECT_EQ(swBack->cases[0].body[0]->kind(), Stmt::Kind::ExprStmt);
    EXPECT_EQ(swBack->cases[0].body[1]->kind(), Stmt::Kind::Break);
    ASSERT_EQ(swBack->cases[1].body.size(), 1u);
    EXPECT_EQ(swBack->cases[1].body[0]->kind(), Stmt::Kind::Continue);

    // Verify ternary in return
    auto* retBack = static_cast<ReturnStmt*>(back.functions[0].body[1].get());
    ASSERT_NE(retBack->value, nullptr);
    EXPECT_EQ(retBack->value->kind(), Expr::Kind::Ternary);
}

// =====================================================================
// Emitter output: SwitchStmt
// =====================================================================

static TranspileModule makeSwitchModule() {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "handle";
    fn.returnType.nameParts = {"void"};
    topo::Parameter p;
    p.type.nameParts = {"int"};
    p.name = "code";
    fn.params = {p};

    auto sw = std::make_unique<SwitchStmt>();
    sw->subject = varRef("code");

    SwitchCase c1;
    c1.value = intLit("1");
    c1.body.push_back(std::make_unique<BreakStmt>());

    SwitchCase c2;
    c2.value = intLit("2");
    c2.body.push_back(std::make_unique<ContinueStmt>());

    SwitchCase cdef;
    auto ret = std::make_unique<ReturnStmt>();
    cdef.body.push_back(std::move(ret));

    sw->cases.push_back(std::move(c1));
    sw->cases.push_back(std::move(c2));
    sw->cases.push_back(std::move(cdef));
    fn.body.push_back(std::move(sw));

    mod.functions.push_back(std::move(fn));
    return mod;
}

TEST(TranspileNewConstructsEmitter, CppSwitch) {
    CppEmitter emitter;
    auto output = emitter.emit(makeSwitchModule());

    EXPECT_NE(output.code.find("switch (code)"), std::string::npos);
    EXPECT_NE(output.code.find("case 1:"), std::string::npos);
    EXPECT_NE(output.code.find("case 2:"), std::string::npos);
    EXPECT_NE(output.code.find("default:"), std::string::npos);
    EXPECT_NE(output.code.find("break;"), std::string::npos);
    EXPECT_NE(output.code.find("continue;"), std::string::npos);
}

TEST(TranspileNewConstructsEmitter, RustSwitch) {
    RustEmitter emitter;
    auto output = emitter.emit(makeSwitchModule());

    EXPECT_NE(output.code.find("match code"), std::string::npos);
    EXPECT_NE(output.code.find("1 => {"), std::string::npos);
    EXPECT_NE(output.code.find("2 => {"), std::string::npos);
    EXPECT_NE(output.code.find("_ => {"), std::string::npos);
    EXPECT_NE(output.code.find("break;"), std::string::npos);
    EXPECT_NE(output.code.find("continue;"), std::string::npos);
}

TEST(TranspileNewConstructsEmitter, JavaSwitch) {
    JavaEmitter emitter;
    auto output = emitter.emit(makeSwitchModule());

    EXPECT_NE(output.code.find("switch (code)"), std::string::npos);
    EXPECT_NE(output.code.find("case 1:"), std::string::npos);
    EXPECT_NE(output.code.find("case 2:"), std::string::npos);
    EXPECT_NE(output.code.find("default:"), std::string::npos);
    EXPECT_NE(output.code.find("break;"), std::string::npos);
    EXPECT_NE(output.code.find("continue;"), std::string::npos);
}

// =====================================================================
// Emitter output: TernaryExpr
// =====================================================================

static TranspileModule makeTernaryModule() {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "pick";
    fn.returnType.nameParts = {"int"};
    topo::Parameter p;
    p.type.nameParts = {"boolean"};
    p.name = "flag";
    fn.params = {p};

    auto ret = std::make_unique<ReturnStmt>();
    auto tern = std::make_unique<TernaryExpr>();
    tern->condition = varRef("flag");
    tern->trueExpr = intLit("42");
    tern->falseExpr = intLit("0");
    ret->value = std::move(tern);
    fn.body.push_back(std::move(ret));

    mod.functions.push_back(std::move(fn));
    return mod;
}

TEST(TranspileNewConstructsEmitter, CppTernary) {
    CppEmitter emitter;
    auto output = emitter.emit(makeTernaryModule());

    EXPECT_NE(output.code.find("flag ? 42 : 0"), std::string::npos) << "C++ ternary syntax expected. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, RustTernary) {
    RustEmitter emitter;
    auto output = emitter.emit(makeTernaryModule());

    // Rust has no ternary operator; uses if/else expression
    EXPECT_NE(output.code.find("if flag { 42 } else { 0 }"), std::string::npos) << "Rust if/else expression expected. Got:\n"
                                                                           << output.code;
}

TEST(TranspileNewConstructsEmitter, JavaTernary) {
    JavaEmitter emitter;
    auto output = emitter.emit(makeTernaryModule());

    EXPECT_NE(output.code.find("flag ? 42 : 0"), std::string::npos) << "Java ternary syntax expected. Got:\n" << output.code;
}

// =====================================================================
// Emitter output: CompoundAssignExpr
// =====================================================================

static TranspileModule makeCompoundAssignModule(BinaryOp op) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "update";
    fn.returnType.nameParts = {"void"};
    topo::Parameter p;
    p.type.nameParts = {"int"};
    p.name = "x";
    fn.params = {p};

    auto ca = std::make_unique<CompoundAssignExpr>();
    ca->op = op;
    ca->target = varRef("x");
    ca->value = intLit("1");
    auto es = std::make_unique<ExprStmt>();
    es->expr = std::move(ca);
    fn.body.push_back(std::move(es));

    mod.functions.push_back(std::move(fn));
    return mod;
}

TEST(TranspileNewConstructsEmitter, CompoundAssignAddAllEmitters) {
    auto mod = makeCompoundAssignModule(BinaryOp::Add);

    CppEmitter cpp;
    EXPECT_NE(cpp.emit(mod).code.find("x += 1"), std::string::npos);

    // Re-create because emit consumed nothing (const ref), but module was moved
    mod = makeCompoundAssignModule(BinaryOp::Add);
    RustEmitter rust;
    EXPECT_NE(rust.emit(mod).code.find("x += 1"), std::string::npos);

    mod = makeCompoundAssignModule(BinaryOp::Add);
    JavaEmitter java;
    EXPECT_NE(java.emit(mod).code.find("x += 1"), std::string::npos);
}

TEST(TranspileNewConstructsEmitter, CompoundAssignBitwiseOps) {
    struct Case {
        BinaryOp op;
        std::string expected;
    };
    Case cases[] = {
        {BinaryOp::BitAnd, "x &= 1"},
        {BinaryOp::BitOr, "x |= 1"},
        {BinaryOp::BitXor, "x ^= 1"},
        {BinaryOp::Shl, "x <<= 1"},
        {BinaryOp::Shr, "x >>= 1"},
    };

    for (const auto& c : cases) {
        auto mod = makeCompoundAssignModule(c.op);
        CppEmitter cpp;
        EXPECT_NE(cpp.emit(mod).code.find(c.expected), std::string::npos) << "C++ expected '" << c.expected << "'";

        mod = makeCompoundAssignModule(c.op);
        RustEmitter rust;
        EXPECT_NE(rust.emit(mod).code.find(c.expected), std::string::npos) << "Rust expected '" << c.expected << "'";

        mod = makeCompoundAssignModule(c.op);
        JavaEmitter java;
        EXPECT_NE(java.emit(mod).code.find(c.expected), std::string::npos) << "Java expected '" << c.expected << "'";
    }
}

// =====================================================================
// Emitter output: Bitwise BinaryOp
// =====================================================================

static TranspileModule makeBitwiseBinaryModule(BinaryOp op) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "bitop";
    fn.returnType.nameParts = {"int"};
    topo::Parameter pa;
    pa.type.nameParts = {"int"};
    pa.name = "a";
    topo::Parameter pb;
    pb.type.nameParts = {"int"};
    pb.name = "b";
    fn.params = {pa, pb};

    auto ret = std::make_unique<ReturnStmt>();
    auto bin = std::make_unique<BinaryOpExpr>();
    bin->op = op;
    bin->lhs = varRef("a");
    bin->rhs = varRef("b");
    ret->value = std::move(bin);
    fn.body.push_back(std::move(ret));

    mod.functions.push_back(std::move(fn));
    return mod;
}

TEST(TranspileNewConstructsEmitter, BitwiseBinaryOpsAllEmitters) {
    struct Case {
        BinaryOp op;
        std::string expected;
    };
    Case cases[] = {
        {BinaryOp::BitAnd, "a & b"},
        {BinaryOp::BitOr, "a | b"},
        {BinaryOp::BitXor, "a ^ b"},
        {BinaryOp::Shl, "a << b"},
        {BinaryOp::Shr, "a >> b"},
    };

    for (const auto& c : cases) {
        auto mod = makeBitwiseBinaryModule(c.op);
        CppEmitter cpp;
        EXPECT_NE(cpp.emit(mod).code.find(c.expected), std::string::npos) << "C++ expected '" << c.expected << "'";

        mod = makeBitwiseBinaryModule(c.op);
        RustEmitter rust;
        EXPECT_NE(rust.emit(mod).code.find(c.expected), std::string::npos) << "Rust expected '" << c.expected << "'";

        mod = makeBitwiseBinaryModule(c.op);
        JavaEmitter java;
        EXPECT_NE(java.emit(mod).code.find(c.expected), std::string::npos) << "Java expected '" << c.expected << "'";
    }
}

// =====================================================================
// Emitter output: UnaryOp BitNot
// =====================================================================

static TranspileModule makeBitNotModule() {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "invert";
    fn.returnType.nameParts = {"int"};
    topo::Parameter p;
    p.type.nameParts = {"int"};
    p.name = "mask";
    fn.params = {p};

    auto ret = std::make_unique<ReturnStmt>();
    auto un = std::make_unique<UnaryOpExpr>();
    un->op = UnaryOp::BitNot;
    un->operand = varRef("mask");
    ret->value = std::move(un);
    fn.body.push_back(std::move(ret));

    mod.functions.push_back(std::move(fn));
    return mod;
}

TEST(TranspileNewConstructsEmitter, CppBitNot) {
    CppEmitter emitter;
    auto output = emitter.emit(makeBitNotModule());
    EXPECT_NE(output.code.find("~mask"), std::string::npos) << "C++ ~ operator expected. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, RustBitNot) {
    RustEmitter emitter;
    auto output = emitter.emit(makeBitNotModule());
    // Rust uses ! for both logical and bitwise NOT
    EXPECT_NE(output.code.find("!mask"), std::string::npos) << "Rust ! operator expected. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, JavaBitNot) {
    JavaEmitter emitter;
    auto output = emitter.emit(makeBitNotModule());
    EXPECT_NE(output.code.find("~mask"), std::string::npos) << "Java ~ operator expected. Got:\n" << output.code;
}

// =====================================================================
// JSON round-trip: Increment/Decrement UnaryOp variants
// =====================================================================

TEST(TranspileNewConstructsRoundTrip, IncrementDecrementOps) {
    UnaryOp ops[] = {UnaryOp::PreIncrement, UnaryOp::PostIncrement, UnaryOp::PreDecrement, UnaryOp::PostDecrement};
    for (auto op : ops) {
        auto un = std::make_unique<UnaryOpExpr>();
        un->op = op;
        un->operand = varRef("i");

        auto j = serializeExpr(*un);
        auto back = deserializeExpr(j);

        ASSERT_EQ(back->kind(), Expr::Kind::UnaryOp);
        auto* b = static_cast<UnaryOpExpr*>(back.get());
        EXPECT_EQ(b->op, op);
        ASSERT_EQ(b->operand->kind(), Expr::Kind::VarRef);
        EXPECT_EQ(static_cast<VarRefExpr*>(b->operand.get())->name, "i");
    }
}

// =====================================================================
// Emitter output: Increment/Decrement operators
// =====================================================================

static TranspileModule makeIncrDecrModule(UnaryOp op) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "incrdecr";
    fn.returnType.nameParts = {"void"};
    topo::Parameter p;
    p.type.nameParts = {"int"};
    p.name = "i";
    fn.params = {p};

    auto un = std::make_unique<UnaryOpExpr>();
    un->op = op;
    un->operand = varRef("i");
    auto es = std::make_unique<ExprStmt>();
    es->expr = std::move(un);
    fn.body.push_back(std::move(es));

    mod.functions.push_back(std::move(fn));
    return mod;
}

TEST(TranspileNewConstructsEmitter, CppPreIncrement) {
    CppEmitter emitter;
    auto output = emitter.emit(makeIncrDecrModule(UnaryOp::PreIncrement));
    EXPECT_NE(output.code.find("++i"), std::string::npos) << "C++ ++i expected. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, CppPostIncrement) {
    CppEmitter emitter;
    auto output = emitter.emit(makeIncrDecrModule(UnaryOp::PostIncrement));
    EXPECT_NE(output.code.find("i++"), std::string::npos) << "C++ i++ expected. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, CppPreDecrement) {
    CppEmitter emitter;
    auto output = emitter.emit(makeIncrDecrModule(UnaryOp::PreDecrement));
    EXPECT_NE(output.code.find("--i"), std::string::npos) << "C++ --i expected. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, CppPostDecrement) {
    CppEmitter emitter;
    auto output = emitter.emit(makeIncrDecrModule(UnaryOp::PostDecrement));
    EXPECT_NE(output.code.find("i--"), std::string::npos) << "C++ i-- expected. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, RustPreIncrement) {
    RustEmitter emitter;
    auto output = emitter.emit(makeIncrDecrModule(UnaryOp::PreIncrement));
    EXPECT_NE(output.code.find("i += 1"), std::string::npos) << "Rust += 1 expected. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, RustPostIncrement) {
    RustEmitter emitter;
    auto output = emitter.emit(makeIncrDecrModule(UnaryOp::PostIncrement));
    EXPECT_NE(output.code.find("let _prev = i"), std::string::npos) << "Rust post-increment expected. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, JavaPreIncrement) {
    JavaEmitter emitter;
    auto output = emitter.emit(makeIncrDecrModule(UnaryOp::PreIncrement));
    EXPECT_NE(output.code.find("++i"), std::string::npos) << "Java ++i expected. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, JavaPostDecrement) {
    JavaEmitter emitter;
    auto output = emitter.emit(makeIncrDecrModule(UnaryOp::PostDecrement));
    EXPECT_NE(output.code.find("i--"), std::string::npos) << "Java i-- expected. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, PythonPreIncrement) {
    PythonEmitter emitter;
    auto output = emitter.emit(makeIncrDecrModule(UnaryOp::PreIncrement));
    EXPECT_NE(output.code.find("i := i + 1"), std::string::npos) << "Python walrus increment expected. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, PythonPostDecrement) {
    PythonEmitter emitter;
    auto output = emitter.emit(makeIncrDecrModule(UnaryOp::PostDecrement));
    EXPECT_NE(output.code.find("i - 1"), std::string::npos) << "Python post-decrement expected. Got:\n" << output.code;
}

// =====================================================================
// Emitter output: Rust counted for-loop
// =====================================================================

static TranspileModule makeCountedForModule() {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "loop_test";
    fn.returnType.nameParts = {"void"};

    // for (let i = 0; i < 10; i += 1) { x = i; }
    auto forStmt = std::make_unique<ForStmt>();

    auto init = std::make_unique<VarDeclStmt>();
    init->name = "i";
    init->type.nameParts = {"int"};
    init->init = intLit("0");
    forStmt->init = std::move(init);

    auto cond = std::make_unique<BinaryOpExpr>();
    cond->op = BinaryOp::Less;
    cond->lhs = varRef("i");
    cond->rhs = intLit("10");
    forStmt->condition = std::move(cond);

    auto incr = std::make_unique<CompoundAssignExpr>();
    incr->op = BinaryOp::Add;
    incr->target = varRef("i");
    incr->value = intLit("1");
    forStmt->increment = std::move(incr);

    auto assign = std::make_unique<AssignStmt>();
    assign->target = varRef("x");
    assign->value = varRef("i");
    forStmt->body.push_back(std::move(assign));

    fn.body.push_back(std::move(forStmt));
    mod.functions.push_back(std::move(fn));
    return mod;
}

TEST(TranspileNewConstructsEmitter, RustCountedForLoop) {
    RustEmitter emitter;
    auto output = emitter.emit(makeCountedForModule());
    EXPECT_NE(output.code.find("for i in 0..10"), std::string::npos) << "Rust for-in range expected. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, RustCountedForLoopNoWhileFallback) {
    RustEmitter emitter;
    auto output = emitter.emit(makeCountedForModule());
    // When counted loop is detected, "while" should NOT appear
    EXPECT_EQ(output.code.find("while"), std::string::npos) << "Rust should use for-in, not while. Got:\n" << output.code;
}

// =====================================================================
// Emitter output: Java access modifiers
// =====================================================================

TEST(TranspileNewConstructsEmitter, JavaAccessModifierPublic) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "greet";
    fn.returnType.nameParts = {"void"};
    fn.accessModifier = "public";
    mod.functions.push_back(std::move(fn));

    JavaEmitter emitter;
    auto output = emitter.emit(mod);
    EXPECT_NE(output.code.find("public static void greet"), std::string::npos) << "Java public static modifier expected. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, JavaAccessModifierPrivate) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "helper";
    fn.returnType.nameParts = {"void"};
    fn.accessModifier = "private";
    mod.functions.push_back(std::move(fn));

    JavaEmitter emitter;
    auto output = emitter.emit(mod);
    EXPECT_NE(output.code.find("private static void helper"), std::string::npos) << "Java private static modifier expected. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, JavaAccessModifierEmpty) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "internal";
    fn.returnType.nameParts = {"void"};
    // accessModifier left empty (package-private)
    mod.functions.push_back(std::move(fn));

    JavaEmitter emitter;
    auto output = emitter.emit(mod);
    // Should have static but no access modifier prefix
    EXPECT_NE(output.code.find("static void internal"), std::string::npos) << "Java static-only expected. Got:\n" << output.code;
    EXPECT_EQ(output.code.find("public static void internal"), std::string::npos);
    EXPECT_EQ(output.code.find("private static void internal"), std::string::npos);
}

// =====================================================================
// JSON round-trip: accessModifier field
// =====================================================================

TEST(TranspileNewConstructsRoundTrip, AccessModifierRoundTrip) {
    TranspileFunction fn;
    fn.qualifiedName = "greet";
    fn.returnType.nameParts = {"void"};
    fn.accessModifier = "protected";

    nlohmann::json j;
    to_json(j, fn);
    EXPECT_EQ(j["accessModifier"], "protected");

    TranspileFunction back;
    from_json(j, back);
    EXPECT_EQ(back.accessModifier, "protected");
}

TEST(TranspileNewConstructsRoundTrip, AccessModifierEmptyNotSerialized) {
    TranspileFunction fn;
    fn.qualifiedName = "internal";
    fn.returnType.nameParts = {"void"};
    // accessModifier left empty

    nlohmann::json j;
    to_json(j, fn);
    EXPECT_FALSE(j.contains("accessModifier")) << "Empty accessModifier should not appear in JSON";

    TranspileFunction back;
    from_json(j, back);
    EXPECT_TRUE(back.accessModifier.empty());
}

// =====================================================================
// Emitter output: Rust if/else-if → match expression optimization
// =====================================================================

static std::unique_ptr<BinaryOpExpr> eqExpr(const std::string& var, const std::string& val) {
    auto e = std::make_unique<BinaryOpExpr>();
    e->op = BinaryOp::Eq;
    e->lhs = varRef(var);
    e->rhs = intLit(val);
    return e;
}

static TranspileModule makeIfElseChainMatchModule() {
    // if (x == 1) { a(); } else if (x == 2) { b(); } else if (x == 3) { c(); } else { d(); }
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "dispatch";
    fn.returnType.nameParts = {"void"};
    topo::Parameter p;
    p.type.nameParts = {"int"};
    p.name = "x";
    fn.params = {p};

    // Build innermost else-if: if (x == 3) { c(); } else { d(); }
    auto if3 = std::make_unique<IfStmt>();
    if3->condition = eqExpr("x", "3");
    auto callC = std::make_unique<CallExpr>();
    callC->callee = "c";
    auto esC = std::make_unique<ExprStmt>();
    esC->expr = std::move(callC);
    if3->thenBody.push_back(std::move(esC));
    auto callD = std::make_unique<CallExpr>();
    callD->callee = "d";
    auto esD = std::make_unique<ExprStmt>();
    esD->expr = std::move(callD);
    if3->elseBody.push_back(std::move(esD));

    // Middle else-if: if (x == 2) { b(); } else { <if3> }
    auto if2 = std::make_unique<IfStmt>();
    if2->condition = eqExpr("x", "2");
    auto callB = std::make_unique<CallExpr>();
    callB->callee = "b";
    auto esB = std::make_unique<ExprStmt>();
    esB->expr = std::move(callB);
    if2->thenBody.push_back(std::move(esB));
    if2->elseBody.push_back(std::move(if3));

    // Outermost: if (x == 1) { a(); } else { <if2> }
    auto if1 = std::make_unique<IfStmt>();
    if1->condition = eqExpr("x", "1");
    auto callA = std::make_unique<CallExpr>();
    callA->callee = "a";
    auto esA = std::make_unique<ExprStmt>();
    esA->expr = std::move(callA);
    if1->thenBody.push_back(std::move(esA));
    if1->elseBody.push_back(std::move(if2));

    fn.body.push_back(std::move(if1));
    mod.functions.push_back(std::move(fn));
    return mod;
}

TEST(TranspileNewConstructsEmitter, RustIfElseChainToMatch) {
    RustEmitter emitter;
    auto output = emitter.emit(makeIfElseChainMatchModule());

    EXPECT_NE(output.code.find("match x"), std::string::npos)
        << "Rust match expression expected. Got:\n" << output.code;
    EXPECT_NE(output.code.find("1 => {"), std::string::npos)
        << "Match arm for 1 expected. Got:\n" << output.code;
    EXPECT_NE(output.code.find("2 => {"), std::string::npos)
        << "Match arm for 2 expected. Got:\n" << output.code;
    EXPECT_NE(output.code.find("3 => {"), std::string::npos)
        << "Match arm for 3 expected. Got:\n" << output.code;
    EXPECT_NE(output.code.find("_ => {"), std::string::npos)
        << "Default match arm expected. Got:\n" << output.code;
    // Should NOT contain if/else
    EXPECT_EQ(output.code.find("if "), std::string::npos)
        << "Should use match, not if/else. Got:\n" << output.code;
}

static TranspileModule makeIfElseMixedVarsModule() {
    // if (x == 1) { a(); } else if (y == 2) { b(); } — different variables, not a match
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "mixed";
    fn.returnType.nameParts = {"void"};
    topo::Parameter px;
    px.type.nameParts = {"int"};
    px.name = "x";
    topo::Parameter py;
    py.type.nameParts = {"int"};
    py.name = "y";
    fn.params = {px, py};

    auto if2 = std::make_unique<IfStmt>();
    if2->condition = eqExpr("y", "2");
    auto callB = std::make_unique<CallExpr>();
    callB->callee = "b";
    auto esB = std::make_unique<ExprStmt>();
    esB->expr = std::move(callB);
    if2->thenBody.push_back(std::move(esB));

    auto if1 = std::make_unique<IfStmt>();
    if1->condition = eqExpr("x", "1");
    auto callA = std::make_unique<CallExpr>();
    callA->callee = "a";
    auto esA = std::make_unique<ExprStmt>();
    esA->expr = std::move(callA);
    if1->thenBody.push_back(std::move(esA));
    if1->elseBody.push_back(std::move(if2));

    fn.body.push_back(std::move(if1));
    mod.functions.push_back(std::move(fn));
    return mod;
}

TEST(TranspileNewConstructsEmitter, RustMixedVarsNoMatch) {
    RustEmitter emitter;
    auto output = emitter.emit(makeIfElseMixedVarsModule());

    // Should use regular if/else, not match
    EXPECT_EQ(output.code.find("match"), std::string::npos)
        << "Should NOT produce match for different variables. Got:\n" << output.code;
    EXPECT_NE(output.code.find("if "), std::string::npos)
        << "Should use if/else for mixed variables. Got:\n" << output.code;
}

// =====================================================================
// Emitter output: parameter mutability.
// Rust parameters are immutable by default. When a TranspileFunction
// reassigns a parameter in its body, the emitter must produce `mut name: T`
// to avoid E0384.
// =====================================================================

static TranspileModule makeParamReassignModule() {
    // fn reassign(a: i32, b: i32) { a = b; }
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "reassign";
    fn.returnType.nameParts = {"void"};
    topo::Parameter pa; pa.type.nameParts = {"int"}; pa.name = "a";
    topo::Parameter pb; pb.type.nameParts = {"int"}; pb.name = "b";
    fn.params = {pa, pb};

    auto assignA = std::make_unique<AssignStmt>();
    assignA->target = varRef("a");
    assignA->value = varRef("b");
    fn.body.push_back(std::move(assignA));

    mod.functions.push_back(std::move(fn));
    return mod;
}

TEST(TranspileNewConstructsEmitter, RustParamReassignmentEmitsMut) {
    RustEmitter emitter;
    auto output = emitter.emit(makeParamReassignModule());

    // Reassigned param `a` must be `mut`; untouched `b` must NOT be.
    EXPECT_NE(output.code.find("mut a: i32"), std::string::npos)
        << "Reassigned param `a` should be emitted with `mut`. Got:\n" << output.code;
    EXPECT_EQ(output.code.find("mut b: i32"), std::string::npos)
        << "Untouched param `b` should NOT be emitted with `mut`. Got:\n" << output.code;
}

static TranspileModule makeParamCompoundAssignModule() {
    // fn add_into(x: i32, y: i32) { x += y; }
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "add_into";
    fn.returnType.nameParts = {"void"};
    topo::Parameter px; px.type.nameParts = {"int"}; px.name = "x";
    topo::Parameter py; py.type.nameParts = {"int"}; py.name = "y";
    fn.params = {px, py};

    auto compound = std::make_unique<CompoundAssignExpr>();
    compound->op = BinaryOp::Add;
    compound->target = varRef("x");
    compound->value = varRef("y");
    auto es = std::make_unique<ExprStmt>();
    es->expr = std::move(compound);
    fn.body.push_back(std::move(es));

    mod.functions.push_back(std::move(fn));
    return mod;
}

TEST(TranspileNewConstructsEmitter, RustParamCompoundAssignEmitsMut) {
    RustEmitter emitter;
    auto output = emitter.emit(makeParamCompoundAssignModule());

    // Compound-assign counts as reassignment.
    EXPECT_NE(output.code.find("mut x: i32"), std::string::npos)
        << "Param `x` with `+=` should be `mut`. Got:\n" << output.code;
    EXPECT_EQ(output.code.find("mut y: i32"), std::string::npos)
        << "Param `y` (RHS only) should NOT be `mut`. Got:\n" << output.code;
}

static TranspileModule makeParamReassignNestedModule() {
    // fn loop_decr(n: i32) { while (n > 0) { n = n - 1; } }
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "loop_decr";
    fn.returnType.nameParts = {"void"};
    topo::Parameter pn; pn.type.nameParts = {"int"}; pn.name = "n";
    fn.params = {pn};

    auto whileStmt = std::make_unique<WhileStmt>();
    auto cond = std::make_unique<BinaryOpExpr>();
    cond->op = BinaryOp::Greater;
    cond->lhs = varRef("n");
    cond->rhs = intLit("0");
    whileStmt->condition = std::move(cond);

    auto assignN = std::make_unique<AssignStmt>();
    assignN->target = varRef("n");
    auto sub = std::make_unique<BinaryOpExpr>();
    sub->op = BinaryOp::Sub;
    sub->lhs = varRef("n");
    sub->rhs = intLit("1");
    assignN->value = std::move(sub);
    whileStmt->body.push_back(std::move(assignN));

    fn.body.push_back(std::move(whileStmt));
    mod.functions.push_back(std::move(fn));
    return mod;
}

TEST(TranspileNewConstructsEmitter, RustParamReassignInNestedScopeEmitsMut) {
    RustEmitter emitter;
    auto output = emitter.emit(makeParamReassignNestedModule());

    // Reassignment inside a while body still requires the signature to mark `mut`.
    EXPECT_NE(output.code.find("mut n: i32"), std::string::npos)
        << "Param `n` reassigned in nested scope should be `mut`. Got:\n" << output.code;
}

// =====================================================================
// Emitter output: stdlib bridging types `bytes` and `array<T, N>`
// (Rust host mapping)
// =====================================================================

// `TypeNode` and the `stdlib` catalog live in namespace `topo` (this TU's
// `using namespace topo::transpile;` does not bring them in).
using topo::TypeNode;
namespace stdlib = topo::stdlib;

// Build a TypeNode for a scalar stdlib type (e.g. i64), mirroring what the
// parser populates: stdlibId set + canonical keyword in nameParts[0].
static TypeNode stdlibScalar(stdlib::TypeId id, const std::string& kw) {
    TypeNode t;
    t.stdlibId = id;
    t.nameParts = {kw};
    return t;
}

// One-parameter function module; the parameter's type is `paramType`.
static TranspileModule makeSingleParamModule(const std::string& fnName,
                                             TypeNode paramType) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "test::" + fnName;
    fn.returnType.nameParts = {"void"};

    topo::Parameter p;
    p.name = "p";
    p.type = std::move(paramType);
    fn.params = {p};

    mod.functions.push_back(std::move(fn));
    return mod;
}

TEST(TranspileNewConstructsEmitter, RustBytesMapsToU8Slice) {
    // `bytes` is slice<u8>-isomorphic; the Rust mapping must be exactly the
    // slice<u8> form (`&[u8]`) — no new invented Rust type.
    TypeNode bytesT;
    bytesT.stdlibId = stdlib::TypeId::Bytes;
    bytesT.nameParts = {"bytes"};

    RustEmitter emitter;
    auto output = emitter.emit(makeSingleParamModule("take_bytes", bytesT));

    EXPECT_NE(output.code.find("p: &[u8]"), std::string::npos)
        << "`bytes` must map to the slice<u8> form `&[u8]`. Got:\n"
        << output.code;
}

TEST(TranspileNewConstructsEmitter, RustBytesEqualsExplicitSliceOfU8) {
    // Equivalence guard: emitting `bytes` and emitting `slice<u8>` must
    // produce the identical Rust type token.
    TypeNode bytesT;
    bytesT.stdlibId = stdlib::TypeId::Bytes;
    bytesT.nameParts = {"bytes"};

    TypeNode sliceOfU8;
    sliceOfU8.stdlibId = stdlib::TypeId::Slice;
    sliceOfU8.nameParts = {"slice"};
    sliceOfU8.templateArgs.push_back(stdlibScalar(stdlib::TypeId::U8, "u8"));

    RustEmitter e1, e2;
    auto outBytes = e1.emit(makeSingleParamModule("b", bytesT));
    auto outSlice = e2.emit(makeSingleParamModule("b", sliceOfU8));

    EXPECT_NE(outBytes.code.find("p: &[u8]"), std::string::npos);
    EXPECT_NE(outSlice.code.find("p: &[u8]"), std::string::npos)
        << "Sanity: slice<u8> itself should emit `&[u8]`. Got:\n"
        << outSlice.code;
}

TEST(TranspileNewConstructsEmitter, RustArrayMapsToFixedLengthArray) {
    // array<i64, 4> -> idiomatic Rust `[i64; 4]`.
    TypeNode arr;
    arr.stdlibId = stdlib::TypeId::Array;
    arr.nameParts = {"array"};
    arr.templateArgs.push_back(stdlibScalar(stdlib::TypeId::I64, "i64"));
    TypeNode count;       // second template arg carries N via nonTypeValue
    count.nonTypeValue = 4;
    arr.templateArgs.push_back(count);

    RustEmitter emitter;
    auto output = emitter.emit(makeSingleParamModule("take_arr", arr));

    EXPECT_NE(output.code.find("p: [i64; 4]"), std::string::npos)
        << "`array<i64, 4>` must map to `[i64; 4]`. Got:\n" << output.code;
}

TEST(TranspileNewConstructsEmitter, RustNestedArrayOfArrayRecurses) {
    // array<array<i64, 4>, 2> -> `[[i64; 4]; 2]` (element recursion, same
    // mechanism as slice element recursion).
    TypeNode inner;
    inner.stdlibId = stdlib::TypeId::Array;
    inner.nameParts = {"array"};
    inner.templateArgs.push_back(stdlibScalar(stdlib::TypeId::I64, "i64"));
    TypeNode innerN;
    innerN.nonTypeValue = 4;
    inner.templateArgs.push_back(innerN);

    TypeNode outer;
    outer.stdlibId = stdlib::TypeId::Array;
    outer.nameParts = {"array"};
    outer.templateArgs.push_back(inner);
    TypeNode outerN;
    outerN.nonTypeValue = 2;
    outer.templateArgs.push_back(outerN);

    RustEmitter emitter;
    auto output = emitter.emit(makeSingleParamModule("take_nested", outer));

    EXPECT_NE(output.code.find("p: [[i64; 4]; 2]"), std::string::npos)
        << "`array<array<i64,4>,2>` must recurse into `[[i64; 4]; 2]`. Got:\n"
        << output.code;
}

TEST(TranspileNewConstructsEmitter, RustArrayOfRecordRecursesIntoElement) {
    // array<record<a: i64>, 2>: the element type is a record composite. The
    // Array case must recurse into the element via emitType and wrap it as
    // `[<elem>; 2]`. Record-composite -> Rust mapping is a separate concern
    // (no Record case in the Rust stdlib switch yet); this test pins the
    // Array layer's recursion + count, not the record element rendering.
    TypeNode rec;
    rec.stdlibId = stdlib::TypeId::Record;
    rec.nameParts = {"record"};
    TypeNode::RecordField f;
    f.name = "a";
    f.typeBox.push_back(stdlibScalar(stdlib::TypeId::I64, "i64"));
    rec.recordFields.push_back(std::move(f));

    TypeNode arr;
    arr.stdlibId = stdlib::TypeId::Array;
    arr.nameParts = {"array"};
    arr.templateArgs.push_back(rec);
    TypeNode count;
    count.nonTypeValue = 2;
    arr.templateArgs.push_back(count);

    RustEmitter emitter;
    auto output = emitter.emit(makeSingleParamModule("take_rec_arr", arr));

    // The Array wrapper and count must be present and recurse into the
    // element (whatever the record element renders as).
    auto pos = output.code.find("p: [");
    ASSERT_NE(pos, std::string::npos)
        << "Array wrapper `[` missing. Got:\n" << output.code;
    EXPECT_NE(output.code.find("; 2]"), std::string::npos)
        << "`array<record<...>, 2>` must carry count `; 2]`. Got:\n"
        << output.code;
}

// --- Rust named-lifetime emission ---
//
// Rust lifetime elision fails (E0106) for a free function returning a
// reference with >=2 reference inputs, and a struct holding a reference
// field needs a lifetime parameter. The emitter must introduce a single
// shared `'a` ONLY in those cases and stay elided everywhere elision
// already works (no fixture regression).

// A `&T` reference parameter/return: explicit Ref modifier over a named
// type the binder will not resolve, so it emits as `&T`.
static TypeNode refTo(const std::string& name) {
    TypeNode t;
    t.nameParts = {name};
    t.modifier = TypeNode::Ref;
    return t;
}

// Free function: name, return type, ordered parameter types.
static TranspileModule makeFnModule(const std::string& fnName,
                                    TypeNode retType,
                                    std::vector<TypeNode> paramTypes) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "test::" + fnName;
    fn.returnType = std::move(retType);
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        topo::Parameter p;
        p.name = "p" + std::to_string(i);
        p.type = std::move(paramTypes[i]);
        fn.params.push_back(std::move(p));
    }
    mod.functions.push_back(std::move(fn));
    return mod;
}

TEST(TranspileRustLifetime, TwoRefParamsReturningRefGetsNamedLifetime) {
    // fn pick(p0: &T, p1: &T) -> &T  — elision is ambiguous (E0106).
    // Expect a single shared `'a` on the fn and on every borrow.
    RustEmitter emitter;
    auto out = emitter.emit(
        makeFnModule("pick", refTo("T"), {refTo("T"), refTo("T")}));

    EXPECT_NE(out.code.find("fn pick<'a>("), std::string::npos)
        << "Function should carry a named lifetime `<'a>`. Got:\n"
        << out.code;
    // Both params and the return annotated `&'a`.
    EXPECT_NE(out.code.find("p0: &'a T"), std::string::npos)
        << "First ref param should be `&'a T`. Got:\n" << out.code;
    EXPECT_NE(out.code.find("p1: &'a T"), std::string::npos)
        << "Second ref param should be `&'a T`. Got:\n" << out.code;
    EXPECT_NE(out.code.find("-> &'a T"), std::string::npos)
        << "Return should be `&'a T`. Got:\n" << out.code;
}

TEST(TranspileRustLifetime, SingleRefParamStaysElided) {
    // fn first(p0: &T) -> &T  — elision covers this; NO `'a`.
    RustEmitter emitter;
    auto out = emitter.emit(makeFnModule("first", refTo("T"), {refTo("T")}));

    EXPECT_EQ(out.code.find("'a"), std::string::npos)
        << "Single-ref-param fn must stay elided (no `'a`). Got:\n"
        << out.code;
    EXPECT_NE(out.code.find("fn first(p0: &T) -> &T"), std::string::npos)
        << "Elided signature expected. Got:\n" << out.code;
}

TEST(TranspileRustLifetime, TwoRefParamsNonRefReturnStaysElided) {
    // Return is a value type -> elision irrelevant, no `'a` needed.
    RustEmitter emitter;
    TypeNode i32ret;
    i32ret.nameParts = {"i32"};
    auto out = emitter.emit(
        makeFnModule("cmp", i32ret, {refTo("T"), refTo("T")}));

    EXPECT_EQ(out.code.find("'a"), std::string::npos)
        << "Non-reference return must stay elided. Got:\n" << out.code;
}

TEST(TranspileRustLifetime, StructWithRefFieldGetsNamedLifetime) {
    // struct Holder { r: &T }  — needs `struct Holder<'a>` + `&'a T`.
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "test::Holder";
    TranspileField f;
    f.name = "r";
    f.type = refTo("T");
    ty.fields = {f};
    mod.types.push_back(std::move(ty));

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("struct Holder<'a>"), std::string::npos)
        << "Struct with a ref field needs `<'a>`. Got:\n" << out.code;
    EXPECT_NE(out.code.find("r: &'a T"), std::string::npos)
        << "Ref field should be annotated `&'a T`. Got:\n" << out.code;
}

// Declaration-level type parameter.
static topo::TemplateParamDecl typeParam(const std::string& name) {
    topo::TemplateParamDecl tp;
    tp.kind = topo::TemplateParamDecl::TypeParam;
    tp.name = name;
    return tp;
}

// Declaration-level non-type template parameter (C++ `template <int N>`,
// Rust `const N: usize`). constraintType carries the value type; the host
// emitter renders it as the canonical "value-type before name" idiom.
static topo::TemplateParamDecl nonTypeParam(const std::string& name,
                                             const std::string& valueType) {
    topo::TemplateParamDecl tp;
    tp.kind = topo::TemplateParamDecl::NonTypeParam;
    tp.name = name;
    tp.constraintType.nameParts.push_back(valueType);
    return tp;
}

TEST(TranspileRustGenerics, GenericStructEmitsAngleBracketName) {
    // struct Box<T> { value: i32 }  (value field is plain so no lifetime).
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "test::Box";
    ty.templateParams = {typeParam("T")};
    TranspileField f;
    f.name = "value";
    f.type.nameParts = {"i32"};
    ty.fields = {f};
    mod.types.push_back(std::move(ty));

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("struct Box<T> {"), std::string::npos)
        << "Generic struct should emit `struct Box<T> {`. Got:\n" << out.code;
    EXPECT_EQ(out.code.find("'a"), std::string::npos)
        << "No reference field ⇒ no lifetime. Got:\n" << out.code;
}

TEST(TranspileRustGenerics, NonGenericStructByteIdentical) {
    // No templateParams, no ref field ⇒ exactly the pre-generics output.
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "test::Plain";
    TranspileField f;
    f.name = "n";
    f.type.nameParts = {"i32"};
    ty.fields = {f};
    mod.types.push_back(std::move(ty));

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("struct Plain {"), std::string::npos)
        << "Non-generic struct must be unchanged. Got:\n" << out.code;
    EXPECT_EQ(out.code.find("<"), std::string::npos)
        << "Non-generic struct must carry no angle brackets. Got:\n"
        << out.code;
}

TEST(TranspileRustGenerics, GenericFunctionEmitsAngleBracketName) {
    // fn id<T>(p0: T) -> T
    TypeNode tret;
    tret.nameParts = {"T"};
    TypeNode tp;
    tp.nameParts = {"T"};
    auto mod = makeFnModule("id", tret, {tp});
    mod.functions[0].templateParams = {typeParam("T")};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn id<T>("), std::string::npos)
        << "Generic fn should emit `fn id<T>(`. Got:\n" << out.code;
}

TEST(TranspileRustGenerics, MultipleTypeParamsCommaSeparated) {
    TypeNode tret;
    tret.nameParts = {"T"};
    TypeNode tp;
    tp.nameParts = {"U"};
    auto mod = makeFnModule("conv", tret, {tp});
    mod.functions[0].templateParams = {typeParam("T"), typeParam("U")};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn conv<T, U>("), std::string::npos)
        << "Two type params expected as `<T, U>`. Got:\n" << out.code;
}

TEST(TranspileRustGenerics, LifetimeAndTypeParamMergeIntoOneList) {
    // A struct with BOTH a reference field (forces `'a`) AND a type param.
    // Rust requires a single ordered list with the lifetime FIRST:
    // `struct S<'a, T>` — never two separate `<...>` groups.
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "test::S";
    ty.templateParams = {typeParam("T")};
    TranspileField rf;
    rf.name = "r";
    rf.type = refTo("T");
    ty.fields = {rf};
    mod.types.push_back(std::move(ty));

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("struct S<'a, T> {"), std::string::npos)
        << "Combined clause must be `<'a, T>` (lifetime first, single "
           "group). Got:\n"
        << out.code;
    // Guard against a regression that emits two groups like `S<'a><T>`.
    EXPECT_EQ(out.code.find("><"), std::string::npos)
        << "Must not emit two separate angle-bracket groups. Got:\n"
        << out.code;
    EXPECT_NE(out.code.find("r: &'a T"), std::string::npos)
        << "Ref field still annotated `&'a T`. Got:\n" << out.code;
}

// --- Trait-bound MVP ---
// `<T: Trait>` survives end-to-end: the wire carries `bound` on TemplateParam,
// RustEmitter renders `<T: Trait>` instead of bare `<T>`. Higher-ranked /
// multi-bound forms are absent from the wire (the extractor downgrades
// fidelity), so they are not exercised here.

TEST(TranspileRustGenerics, TypeParamWithSingleBoundRenders) {
    TypeNode tret;
    tret.nameParts = {"void"};
    auto mod = makeFnModule("dump", tret, {});
    auto tp = typeParam("T");
    tp.constraintType.nameParts = {"Clone"};
    mod.functions[0].templateParams = {tp};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn dump<T: Clone>("), std::string::npos)
        << "Bound type param must emit `<T: Clone>`. Got:\n" << out.code;
}

TEST(TranspileRustGenerics, MultiSegmentBoundRendersWithPathSeparator) {
    // A bound carried as a multi-segment path (e.g. `std::fmt::Debug`) must
    // render with Rust's `::` segment separator.
    TypeNode tret;
    tret.nameParts = {"void"};
    auto mod = makeFnModule("show", tret, {});
    auto tp = typeParam("T");
    tp.constraintType.nameParts = {"std", "fmt", "Debug"};
    mod.functions[0].templateParams = {tp};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn show<T: std::fmt::Debug>("), std::string::npos)
        << "Multi-segment bound must emit with `::`. Got:\n" << out.code;
}

// --- Multi-bound MVP: `<T: A + B>` ---
// Multi-bound emit joins each bound with ` + ` (Rust syntax) and uses `::`
// for multi-segment paths. The wire shape graduates from `bound: TypeNode`
// to `bounds: [TypeNode]` only when there's >1 bound; single-bound stays
// on the legacy `bound` key (byte-identical wire output).

TEST(TranspileRustGenerics, TypeParamWithMultiBoundRenders) {
    TypeNode tret;
    tret.nameParts = {"void"};
    auto mod = makeFnModule("sort", tret, {});
    auto tp = typeParam("T");
    tp.constraintType.nameParts = {"Clone"};
    TypeNode eb;
    eb.nameParts = {"Debug"};
    tp.extraBounds = {eb};
    mod.functions[0].templateParams = {tp};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn sort<T: Clone + Debug>("), std::string::npos)
        << "Multi-bound must emit `<T: A + B>`. Got:\n" << out.code;
}

TEST(TranspileRustGenerics, MultiBoundWithMultiSegmentPathsJoinWithDoubleColon) {
    // Each bound retains its own `::` segments; the `+` separator goes
    // between bounds only.
    TypeNode tret;
    tret.nameParts = {"void"};
    auto mod = makeFnModule("show", tret, {});
    auto tp = typeParam("T");
    tp.constraintType.nameParts = {"std", "fmt", "Debug"};
    TypeNode eb;
    eb.nameParts = {"std", "fmt", "Display"};
    tp.extraBounds = {eb};
    mod.functions[0].templateParams = {tp};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn show<T: std::fmt::Debug + std::fmt::Display>("),
              std::string::npos)
        << "Multi-segment paths must use `::` within each bound and ` + ` "
           "between bounds. Got:\n" << out.code;
}

TEST(TranspileRustGenerics, SingleBoundEmitWithExtraBoundsEmptyByteIdentical) {
    // Defensive regression: a single bound (extraBounds empty) must emit
    // byte-identical to pre-multi-bound output — no trailing ` + ` artefact.
    TypeNode tret;
    tret.nameParts = {"void"};
    auto mod = makeFnModule("dump", tret, {});
    auto tp = typeParam("T");
    tp.constraintType.nameParts = {"Clone"};
    mod.functions[0].templateParams = {tp};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn dump<T: Clone>("), std::string::npos)
        << "Single-bound stays bare `<T: Clone>`. Got:\n" << out.code;
    EXPECT_EQ(out.code.find("Clone + "), std::string::npos)
        << "no stray ` + ` after single bound. Got:\n" << out.code;
}

// --- Positional union<...> bound downgrade (Python TypeVar constraint-tuple) ---
// A Python `TypeVar('T', int, str)` constraint-tuple lowers to a type-param
// whose bound is a positional `union<...>` TypeNode (nameParts == ["union"]).
// C++/Rust/Java have no anonymous untagged-union type usable as a generic
// bound, so each emitter drops the bound, renders the bare type param, and
// appends a `TOPO-TRANSPILE:` downgrade note — never the literal `union`
// token, which would be uncompilable host code.

static topo::TemplateParamDecl unionBoundTypeParam(const std::string& name) {
    auto tp = typeParam(name);
    tp.constraintType.nameParts = {"union"};
    TypeNode a; a.nameParts = {"i64"};
    TypeNode b; b.nameParts = {"string"};
    tp.constraintType.templateArgs = {a, b};
    return tp;
}

TEST(TranspileUnionBoundDowngrade, CppDropsUnionBoundWithNote) {
    TypeNode tret; tret.nameParts = {"void"};
    auto mod = makeFnModule("pick", tret, {});
    mod.functions[0].templateParams = {unionBoundTypeParam("T")};

    CppEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_EQ(out.code.find("union T"), std::string::npos)
        << "the literal `union` token must never reach a C++ bound. Got:\n"
        << out.code;
    EXPECT_NE(out.code.find("TOPO-TRANSPILE: union<...> bound on T dropped"),
              std::string::npos)
        << "a downgrade note must mark the dropped union bound. Got:\n"
        << out.code;
}

TEST(TranspileUnionBoundDowngrade, RustDropsUnionBoundWithNote) {
    TypeNode tret; tret.nameParts = {"void"};
    auto mod = makeFnModule("pick", tret, {});
    mod.functions[0].templateParams = {unionBoundTypeParam("T")};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    // `T: union` is the malformed rendered-bound form; the drop note also
    // contains the word "union" (`TOPO-TRANSPILE: union<...>`) so the check
    // must target the bound position specifically, not the bare word.
    EXPECT_EQ(out.code.find("T: union"), std::string::npos)
        << "the literal `union` token must never reach a Rust bound. Got:\n"
        << out.code;
    EXPECT_NE(out.code.find("TOPO-TRANSPILE: union<...> bound on T dropped"),
              std::string::npos)
        << "a downgrade note must mark the dropped union bound. Got:\n"
        << out.code;
}

TEST(TranspileUnionBoundDowngrade, JavaDropsUnionBoundWithNote) {
    TypeNode tret; tret.nameParts = {"void"};
    auto mod = makeFnModule("pick", tret, {});
    mod.functions[0].templateParams = {unionBoundTypeParam("T")};

    JavaEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_EQ(out.code.find("extends union"), std::string::npos)
        << "the literal `union` token must never reach a Java bound. Got:\n"
        << out.code;
    EXPECT_NE(out.code.find("TOPO-TRANSPILE: union<...> bound on T dropped"),
              std::string::npos)
        << "a downgrade note must mark the dropped union bound. Got:\n"
        << out.code;
}

// --- Associated-type bindings (`<T: Iterator<Item = u8>>`) ---
// The bound TypeNode carries `assocBindings` and the emitter's `renderPath`
// helper rewrites the bound's surface form to `Iterator<Item = u8>`. Plays
// alongside positional `templateArgs` (positional first, then named
// bindings) and coexists per-bound with multi-bound (assoc binding rides
// only on the bound that carries them, never widening unrelated bounds).

TEST(TranspileRustGenerics, AssocTypeBindingSingleEmits) {
    TypeNode tret;
    tret.nameParts = {"void"};
    auto mod = makeFnModule("collect", tret, {});
    auto tp = typeParam("T");
    tp.constraintType.nameParts = {"Iterator"};
    TypeNode::RecordField item;
    item.name = "Item";
    TypeNode u8;
    u8.nameParts = {"u8"};
    item.typeBox.push_back(u8);
    tp.constraintType.assocBindings = {item};
    mod.functions[0].templateParams = {tp};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn collect<T: Iterator<Item = u8>>("),
              std::string::npos)
        << "Assoc-type binding must render as `Iterator<Item = u8>`. Got:\n"
        << out.code;
}

TEST(TranspileRustGenerics, AssocTypeBindingMultipleEmitsCommaSeparated) {
    // `Container<Item = u8, Key = i64>` — multiple bindings comma-separated
    // in declaration order.
    TypeNode tret;
    tret.nameParts = {"void"};
    auto mod = makeFnModule("pull", tret, {});
    auto tp = typeParam("T");
    tp.constraintType.nameParts = {"Container"};
    TypeNode::RecordField item;
    item.name = "Item";
    TypeNode u8;
    u8.nameParts = {"u8"};
    item.typeBox.push_back(u8);
    TypeNode::RecordField key;
    key.name = "Key";
    TypeNode i64;
    i64.nameParts = {"i64"};
    key.typeBox.push_back(i64);
    tp.constraintType.assocBindings = {item, key};
    mod.functions[0].templateParams = {tp};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn pull<T: Container<Item = u8, Key = i64>>("),
              std::string::npos)
        << "Multi-binding must comma-separate in source order. Got:\n"
        << out.code;
}

TEST(TranspileRustGenerics, AssocTypeBindingCoexistsWithPositionalTemplateArgs) {
    // `Container<U, Item = u8>` — positional templateArgs first, then
    // named bindings. Matches Rust's lexical ordering.
    TypeNode tret;
    tret.nameParts = {"void"};
    auto mod = makeFnModule("fan", tret, {});
    auto tp = typeParam("T");
    tp.constraintType.nameParts = {"Container"};
    TypeNode positional;
    positional.nameParts = {"U"};
    tp.constraintType.templateArgs = {positional};
    TypeNode::RecordField item;
    item.name = "Item";
    TypeNode u8;
    u8.nameParts = {"u8"};
    item.typeBox.push_back(u8);
    tp.constraintType.assocBindings = {item};
    mod.functions[0].templateParams = {tp};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn fan<T: Container<U, Item = u8>>("),
              std::string::npos)
        << "Positional args precede named bindings. Got:\n" << out.code;
}

TEST(TranspileRustGenerics, AssocTypeBindingPerBoundGranularity) {
    // `<T: Iterator<Item = u8> + Clone>` — assoc binding rides only on
    // Iterator (constraintType); Clone (extraBounds[0]) stays bare and the
    // human-facing diff carries no spurious `Clone<...>`.
    TypeNode tret;
    tret.nameParts = {"void"};
    auto mod = makeFnModule("stream", tret, {});
    auto tp = typeParam("T");
    tp.constraintType.nameParts = {"Iterator"};
    TypeNode::RecordField item;
    item.name = "Item";
    TypeNode u8;
    u8.nameParts = {"u8"};
    item.typeBox.push_back(u8);
    tp.constraintType.assocBindings = {item};
    TypeNode clone;
    clone.nameParts = {"Clone"};
    tp.extraBounds = {clone};
    mod.functions[0].templateParams = {tp};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn stream<T: Iterator<Item = u8> + Clone>("),
              std::string::npos)
        << "Assoc binding rides only on Iterator; Clone stays bare. Got:\n"
        << out.code;
    EXPECT_EQ(out.code.find("Clone<"), std::string::npos)
        << "Clone must not silently inherit Iterator's assoc binding. Got:\n"
        << out.code;
}

TEST(TranspileRustGenerics, AssocTypeBindingByteIdenticalWhenEmpty) {
    // Defensive byte-identical regression: an empty assocBindings vector
    // must produce exactly the same output as no assocBindings field at
    // all — no `Iterator<>` artefact.
    TypeNode tret;
    tret.nameParts = {"void"};
    auto mod = makeFnModule("collect", tret, {});
    auto tp = typeParam("T");
    tp.constraintType.nameParts = {"Iterator"};
    // No assocBindings populated.
    mod.functions[0].templateParams = {tp};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn collect<T: Iterator>("), std::string::npos)
        << "Empty assocBindings must produce bare `<T: Iterator>`. Got:\n"
        << out.code;
    EXPECT_EQ(out.code.find("Iterator<"), std::string::npos)
        << "Empty assocBindings must NOT produce empty `<>` brackets. Got:\n"
        << out.code;
}

// --- Default type-param MVP ---
// `<T = Default>` is rendered only on type-level decls (struct/enum/trait);
// Rust forbids defaults on free functions (E0091) so the emitter drops the
// default and surfaces a TOPO-TRANSPILE comment at the function call-site.

TEST(TranspileRustGenerics, StructTypeParamWithDefaultRenders) {
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "test::Container";
    auto tp = typeParam("T");
    tp.defaultType = TypeNode{};
    tp.defaultType->nameParts = {"i32"};
    ty.templateParams = {tp};
    mod.types.push_back(std::move(ty));

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("struct Container<T = i32>"), std::string::npos)
        << "Struct must emit `<T = i32>`. Got:\n" << out.code;
}

TEST(TranspileRustGenerics, MultiSegmentDefaultRendersWithPathSeparator) {
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "test::Vec";
    auto tp = typeParam("T");
    tp.defaultType = TypeNode{};
    tp.defaultType->nameParts = {"std", "vec", "Vec"};
    ty.templateParams = {tp};
    mod.types.push_back(std::move(ty));

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("struct Vec<T = std::vec::Vec>"), std::string::npos)
        << "Multi-segment default must use `::`. Got:\n" << out.code;
}

TEST(TranspileRustGenerics, FunctionTypeParamWithDefaultDownregradesAndDrops) {
    // Rust forbids defaults on free functions; the emitter must drop the
    // default from the clause AND surface a TOPO-TRANSPILE downgrade comment.
    TypeNode tret;
    tret.nameParts = {"T"};
    TypeNode pt;
    pt.nameParts = {"T"};
    auto mod = makeFnModule("identity", tret, {pt});
    auto tp = typeParam("T");
    tp.defaultType = TypeNode{};
    tp.defaultType->nameParts = {"i32"};
    mod.functions[0].templateParams = {tp};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn identity<T>("), std::string::npos)
        << "Function clause must be bare `<T>` (default dropped). Got:\n"
        << out.code;
    EXPECT_EQ(out.code.find("<T = i32>"), std::string::npos)
        << "Function must not emit `<T = i32>` (E0091). Got:\n" << out.code;
    EXPECT_NE(out.code.find("default on type parameter `T` dropped"),
              std::string::npos)
        << "Function must surface the dropped-default downgrade comment. "
           "Got:\n" << out.code;
}

TEST(TranspileRustGenerics, UnboundedTypeParamByteIdenticalToPreBoundsOutput) {
    // Absence of a bound must produce the same wire/emit shape as before
    // bounds existed — the byte-identical invariant the foundation contract
    // demands for empty optional fields.
    TypeNode tret;
    tret.nameParts = {"T"};
    TypeNode pt;
    pt.nameParts = {"T"};
    auto mod = makeFnModule("identity", tret, {pt});
    mod.functions[0].templateParams = {typeParam("T")};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn identity<T>("), std::string::npos)
        << "Unbounded type param must remain bare `<T>`. Got:\n" << out.code;
    EXPECT_EQ(out.code.find(": "), out.code.find(": T")) // first `: ` is the param type
        << "No stray `:` from absent bound. Got:\n" << out.code;
}

// --- Non-type template parameter (`<const N: usize>`) ---
// Rust const generics: extractor writes `kind="nontype"` + value type in
// `constraintType`; emitter renders `const N: Type`.

TEST(TranspileRustGenerics, NonTypeParamRendersConstClause) {
    TypeNode tret;
    tret.nameParts = {"void"};
    auto mod = makeFnModule("with_size", tret, {});
    mod.functions[0].templateParams = {nonTypeParam("N", "usize")};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn with_size<const N: usize>("), std::string::npos)
        << "Non-type param must emit `<const N: usize>`. Got:\n" << out.code;
}

TEST(TranspileRustGenerics, NonTypeAndTypeParamsOrderPreserved) {
    // `<T, const N: usize>`: type and non-type params share the clause in
    // source order. The emitter must keep the wire ordering unchanged.
    TypeNode tret;
    tret.nameParts = {"void"};
    auto mod = makeFnModule("buffer", tret, {});
    mod.functions[0].templateParams = {typeParam("T"), nonTypeParam("N", "usize")};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn buffer<T, const N: usize>("),
              std::string::npos)
        << "Generated:\n" << out.code;
}

TEST(TranspileRustGenerics, LifetimeAndTypeParamMergeInFunction) {
    // fn pick<'a, T>(p0: &T, p1: &T) -> &T — two ref params + ref return
    // forces `'a`; the type param T must join the same ordered list.
    auto mod = makeFnModule("pick", refTo("T"), {refTo("T"), refTo("T")});
    mod.functions[0].templateParams = {typeParam("T")};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn pick<'a, T>("), std::string::npos)
        << "Combined fn clause must be `<'a, T>`. Got:\n" << out.code;
    EXPECT_EQ(out.code.find("><"), std::string::npos)
        << "Must not emit two separate angle-bracket groups. Got:\n"
        << out.code;
}

TEST(TranspileRustLifetime, StructWithoutRefFieldStaysUnparameterised) {
    // struct Plain { n: i32 }  — no lifetime parameter.
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "test::Plain";
    TranspileField f;
    f.name = "n";
    f.type.nameParts = {"i32"};
    ty.fields = {f};
    mod.types.push_back(std::move(ty));

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_EQ(out.code.find("'a"), std::string::npos)
        << "Value-only struct must stay unparameterised. Got:\n"
        << out.code;
    EXPECT_NE(out.code.find("struct Plain {"), std::string::npos)
        << "Plain struct signature expected. Got:\n" << out.code;
}

// --- Rust lifetime params + type-on-lifetime bound (`T: 'a`) ---
//
// These tests cover the new wire-level lifetime model:
//   - `kind=Lifetime` entries surface in the generics clause as `'a`.
//   - Lifetime-on-lifetime outlives `'a: 'b` renders verbatim.
//   - Type-on-lifetime bound `T: 'a` joins existing trait bounds with ` + `.
//   - Cross-host emitters (C++/Java/Python/TS) silently drop everything.
//   - The pre-existing elision-rule logic still produces single shared `'a`
//     when the wire carries NO lifetime entries (i.e. zero regression).

// Build a lifetime-kind TemplateParamDecl (name WITHOUT the leading
// apostrophe, per the wire contract).
static topo::TemplateParamDecl lifetimeParam(const std::string& name) {
    topo::TemplateParamDecl lp;
    lp.kind = topo::TemplateParamDecl::LifetimeParam;
    lp.name = name;
    return lp;
}

TEST(TranspileRustGenericsPhase3, LifetimeParamRendersWithApostrophe) {
    // `pub fn borrow<'a>(p0: T)` — bare lifetime in the generics clause.
    // No borrow type → the emitter must NOT inject `'a` per the existing
    // elision rule, but the wire-declared lifetime is still rendered.
    TypeNode tret;
    tret.nameParts = {"void"};
    TypeNode tp;
    tp.nameParts = {"T"};
    auto mod = makeFnModule("borrow", tret, {tp});
    mod.functions[0].templateParams = {lifetimeParam("a")};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn borrow<'a>("), std::string::npos)
        << "Wire lifetime param must render as `'a`. Got:\n" << out.code;
}

TEST(TranspileRustGenericsPhase3, LifetimeWithOutlivesRendersColon) {
    // `<'a: 'b>` — lifetime-on-lifetime outlives. Each lifetime is its own
    // template-param entry; the outlives target lives on the first entry's
    // `constraintType` (apostrophe kept).
    TypeNode tret;
    tret.nameParts = {"void"};
    auto mod = makeFnModule("nested", tret, {});
    auto la = lifetimeParam("a");
    la.constraintType.nameParts = {"'b"};
    auto lb = lifetimeParam("b");
    mod.functions[0].templateParams = {la, lb};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn nested<'a: 'b, 'b>("), std::string::npos)
        << "Lifetime outlives must render as `'a: 'b`. Got:\n" << out.code;
}

TEST(TranspileRustGenericsPhase3, TypeParamWithLifetimeBoundRenders) {
    // `struct Holder<'a, T: 'a> { value: &'a T }` — the canonical
    // lifetime-bound example. Wire model: lifetime "a" + type param T whose single bound
    // is the lifetime `'a` (TypeNode with nameParts={"'a"}). Note: a
    // top-level struct (qualifiedName has no `::` namespace) renders
    // without `pub` — the visibility prefix is namespace-driven, matching
    // the existing fn emit behaviour. The lifetime-clause shape is what
    // this test verifies.
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "Holder";
    auto la = lifetimeParam("a");
    auto t = typeParam("T");
    t.constraintType.nameParts = {"'a"};
    ty.templateParams = {la, t};
    TranspileField rf;
    rf.name = "value";
    rf.type = refTo("T");
    ty.fields = {rf};
    mod.types.push_back(std::move(ty));

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("struct Holder<'a, T: 'a> {"), std::string::npos)
        << "Lifetime-bound type param must emit `<'a, T: 'a>`. Got:\n"
        << out.code;
    EXPECT_NE(out.code.find("value: &'a T"), std::string::npos)
        << "Ref field must reuse wire lifetime `'a`. Got:\n" << out.code;
}

TEST(TranspileRustGenericsPhase3, TypeParamWithTraitPlusLifetimeBoundRenders) {
    // `<T: Clone + 'a>` — trait bound coexisting with a lifetime bound.
    // extraBounds carries a TypeNode whose nameParts starts with `'`.
    TypeNode tret;
    tret.nameParts = {"void"};
    auto mod = makeFnModule("walk", tret, {});
    auto la = lifetimeParam("a");
    auto t = typeParam("T");
    t.constraintType.nameParts = {"Clone"};
    TypeNode lt;
    lt.nameParts = {"'a"};
    t.extraBounds = {lt};
    mod.functions[0].templateParams = {la, t};

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn walk<'a, T: Clone + 'a>("), std::string::npos)
        << "Trait + lifetime bound must render `T: Clone + 'a`. Got:\n"
        << out.code;
}

TEST(TranspileRustGenericsPhase3,
     WireLifetimeSuppressesElisionInjection) {
    // When the wire ALREADY declares `'a`, the elision-introduced `'a`
    // path must NOT add a SECOND `'a` to the clause. The borrow-annotation
    // pass uses the wire name.
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "test::Holder";
    ty.templateParams = {lifetimeParam("a")};
    TranspileField rf;
    rf.name = "r";
    rf.type = refTo("T");
    ty.fields = {rf};
    mod.types.push_back(std::move(ty));

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("struct Holder<'a>"), std::string::npos)
        << "Wire lifetime should appear exactly once. Got:\n" << out.code;
    EXPECT_EQ(out.code.find("'a, 'a"), std::string::npos)
        << "Must not double-inject `'a` (one wire, one injection). Got:\n"
        << out.code;
    EXPECT_NE(out.code.find("r: &'a T"), std::string::npos)
        << "Ref field reuses wire lifetime. Got:\n" << out.code;
}

TEST(TranspileRustGenericsPhase3,
     ElisionStillInjectsWhenNoWireLifetime) {
    // Regression guard: without any wire `kind=Lifetime` entry, the
    // existing single-shared-`'a` elision rule must still fire for a
    // fn returning a ref with >=2 ref params.
    auto mod = makeFnModule("pick", refTo("T"), {refTo("T"), refTo("T")});
    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("fn pick<'a>("), std::string::npos)
        << "Elision-failed fn still gets injected `<'a>`. Got:\n"
        << out.code;
    EXPECT_NE(out.code.find("p0: &'a T"), std::string::npos);
    EXPECT_NE(out.code.find("p1: &'a T"), std::string::npos);
    EXPECT_NE(out.code.find("-> &'a T"), std::string::npos);
}

// --- Cross-host lifetime drop behaviour ---
// C++ / Java / Python / TypeScript have no Rust-style lifetime concept; the
// emitters silently drop kind=Lifetime entries and any `'`-prefixed bound
// TypeNode without leaving a comment (lifetime annotations are noise for
// non-Rust hosts).

namespace {

// Build a generic struct module carrying `<'a, T: 'a>` — the canonical
// cross-host drop test case. Each non-Rust emitter must produce the same
// output as a plain `<T>` struct (no lifetime artifacts).
TranspileModule makeLifetimeStructModule() {
    TranspileModule mod;
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
    TranspileField f;
    f.name = "value";
    f.type.nameParts = {"T"};
    ty.fields = {f};
    mod.types.push_back(std::move(ty));
    return mod;
}

} // namespace

TEST(TranspileCrossHostLifetime, CppEmitterDropsLifetimePartsSilently) {
    auto mod = makeLifetimeStructModule();
    CppEmitter emitter;
    auto out = emitter.emit(mod);
    EXPECT_NE(out.code.find("template <typename T>"), std::string::npos)
        << "C++ must emit `<typename T>` with no `'` in the clause. Got:\n"
        << out.code;
    EXPECT_EQ(out.code.find("'"), std::string::npos)
        << "C++ output must contain no apostrophes (no lifetime spillage)."
           " Got:\n" << out.code;
    EXPECT_EQ(out.code.find("TOPO-TRANSPILE: associated"), std::string::npos)
        << "Lifetime drop must be silent (no drop comment). Got:\n"
        << out.code;
}

TEST(TranspileCrossHostLifetime, JavaEmitterDropsLifetimePartsSilently) {
    auto mod = makeLifetimeStructModule();
    JavaEmitter emitter;
    auto out = emitter.emit(mod);
    EXPECT_NE(out.code.find("<T>"), std::string::npos)
        << "Java must emit `<T>` with no lifetime in the clause. Got:\n"
        << out.code;
    EXPECT_EQ(out.code.find("'a"), std::string::npos)
        << "Java output must not carry a Rust lifetime. Got:\n" << out.code;
}

TEST(TranspileCrossHostLifetime, PythonEmitterDropsLifetimePartsSilently) {
    auto mod = makeLifetimeStructModule();
    PythonEmitter emitter;
    auto out = emitter.emit(mod);
    EXPECT_NE(out.code.find("[T]"), std::string::npos)
        << "Python PEP 695 must emit `[T]` with no lifetime. Got:\n"
        << out.code;
    EXPECT_EQ(out.code.find("'a"), std::string::npos)
        << "Python output must not carry a Rust lifetime. Got:\n"
        << out.code;
}

// --- HRTB (Rust `for<'a> Fn(&'a T) -> &'a T`) ---

// Build the canonical HRTB function module: `pub fn map<F>(_f: F)
// where F: for<'a> Fn(&'a u8) -> &'a u8 {}`. The F type param carries a
// single bound TypeNode whose `hrtbLifetimes=["a"]` introduces the HRTB
// lifetime, with parenthesised Fn-trait inputs encoded as `templateArgs`
// and output as a synthesised `Output` assocBinding. The RustEmitter
// detects the Fn/FnMut/FnOnce + Output shape and re-renders parenthesised;
// other-host emitters render angle-bracketed and silently drop the HRTB
// prefix.
namespace {
TranspileModule makeHrtbFnModule() {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "test::map";
    fn.returnType.nameParts = {"void"};
    topo::Parameter p;
    p.name = "_f";
    p.type.nameParts = {"F"};
    fn.params.push_back(p);

    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "F";

    topo::TypeNode bound;
    bound.nameParts = {"Fn"};
    bound.hrtbLifetimes = {"a"};
    // Input arg `&u8` — the Rust extractor encodes the reference as a
    // leading nameParts token. The shared HRTB lifetime label is added by
    // the emitter (not stored on each inner TypeNode).
    topo::TypeNode argTy;
    argTy.nameParts = {"&", "u8"};
    bound.templateArgs = {argTy};
    // Output binding `Output = &u8` — synthesised from the parenthesised
    // Fn-trait shape.
    topo::TypeNode::RecordField outBinding;
    outBinding.name = "Output";
    topo::TypeNode outTy;
    outTy.nameParts = {"&", "u8"};
    outBinding.typeBox.push_back(outTy);
    bound.assocBindings = {outBinding};

    t.constraintType = bound;
    fn.templateParams = {t};

    mod.functions.push_back(std::move(fn));
    return mod;
}
} // namespace

TEST(TranspileRustHRTB, FnTraitBoundEmitsForAllPrefixParenthesisedAndArrow) {
    // Target shape: the emitted Rust generics clause carries
    // `for<'a> Fn(&'a u8) -> &'a u8` on the F type parameter.
    auto mod = makeHrtbFnModule();
    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("F: for<'a> Fn(&'a u8) -> &'a u8"),
              std::string::npos)
        << "HRTB Fn-trait bound on F must render with `for<'a>` prefix and "
           "parenthesised Fn syntax. Got:\n"
        << out.code;
}

TEST(TranspileRustHRTB, MultipleHrtbLifetimesJoinWithCommaAndApostrophe) {
    // Synthetic two-lifetime HRTB on an angle-bracketed Trait — confirms the
    // generic `for<'a, 'b>` rendering (independent of the parenthesised
    // Fn-trait special case).
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "test::run";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    t.constraintType.nameParts = {"Trait"};
    t.constraintType.hrtbLifetimes = {"a", "b"};
    fn.templateParams = {t};
    mod.functions.push_back(std::move(fn));

    RustEmitter emitter;
    auto out = emitter.emit(mod);
    EXPECT_NE(out.code.find("for<'a, 'b> Trait"), std::string::npos)
        << "Multi-HRTB must render `for<'a, 'b>` and re-add apostrophes."
        << " Got:\n" << out.code;
}

TEST(TranspileRustHRTB, EmptyHrtbLifetimesIsByteIdenticalToPrePhase4) {
    // Byte-identical contract: a TypeNode with empty `hrtbLifetimes` must
    // produce output indistinguishable from the pre-Phase-4 emitter. We
    // verify by comparing against a hand-built control case where the same
    // TypeNode has its `hrtbLifetimes` cleared.
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "test::run";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl t;
    t.kind = topo::TemplateParamDecl::TypeParam;
    t.name = "T";
    t.constraintType.nameParts = {"Clone"};
    fn.templateParams = {t};
    mod.functions.push_back(std::move(fn));

    RustEmitter emitter;
    auto out = emitter.emit(mod);
    EXPECT_NE(out.code.find("T: Clone"), std::string::npos)
        << "Non-HRTB bound must render exactly as pre-Phase-4. Got:\n"
        << out.code;
    EXPECT_EQ(out.code.find("for<"), std::string::npos)
        << "Non-HRTB output must contain no `for<` prefix. Got:\n"
        << out.code;
}

TEST(TranspileCrossHostHRTB, CppEmitterDropsHrtbPrefixSilently) {
    // Cross-host: C++ silently drops the `for<'a>` prefix and the
    // parenthesised Fn-trait body falls back to angle-bracketed via the
    // existing assocBinding-drop machinery. No `for<` or `'a` must leak
    // into C++ output, and the drop is silent (no `// for<...>` comment).
    auto mod = makeHrtbFnModule();
    CppEmitter emitter;
    auto out = emitter.emit(mod);
    EXPECT_EQ(out.code.find("for<"), std::string::npos)
        << "C++ output must contain no `for<` HRTB prefix. Got:\n"
        << out.code;
    EXPECT_EQ(out.code.find("'a"), std::string::npos)
        << "C++ output must contain no lifetime annotation. Got:\n"
        << out.code;
}

TEST(TranspileCrossHostHRTB, JavaEmitterDropsHrtbPrefixSilently) {
    auto mod = makeHrtbFnModule();
    JavaEmitter emitter;
    auto out = emitter.emit(mod);
    EXPECT_EQ(out.code.find("for<"), std::string::npos)
        << "Java output must contain no `for<` HRTB prefix. Got:\n"
        << out.code;
    EXPECT_EQ(out.code.find("'a"), std::string::npos)
        << "Java output must contain no Rust lifetime. Got:\n" << out.code;
}

TEST(TranspileCrossHostHRTB, PythonEmitterDropsHrtbPrefixSilently) {
    auto mod = makeHrtbFnModule();
    PythonEmitter emitter;
    auto out = emitter.emit(mod);
    EXPECT_EQ(out.code.find("for<"), std::string::npos)
        << "Python output must contain no `for<` HRTB prefix. Got:\n"
        << out.code;
    EXPECT_EQ(out.code.find("'a"), std::string::npos)
        << "Python output must contain no Rust lifetime. Got:\n"
        << out.code;
}

// --- Rust trait/impl emission for base classes / interfaces ---

// A base TypeNode is identified purely by its (possibly qualified) name.
static TypeNode baseNamed(const std::string& name) {
    TypeNode t;
    t.nameParts = {name};
    return t;
}

// Count non-overlapping occurrences of `needle` in `hay`.
static int countOccurrences(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (size_t pos = hay.find(needle); pos != std::string::npos;
         pos = hay.find(needle, pos + needle.size()))
        ++n;
    return n;
}

TEST(TranspileRustInheritance, StructWithTwoBasesGetsMarkerTraitsAndImpls) {
    // struct Dog : Animal, Loggable  ->  marker trait per base + impl per base,
    // struct body itself unchanged.
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "test::Dog";
    TranspileField f;
    f.name = "age";
    f.type.nameParts = {"i32"};
    ty.fields = {f};
    ty.baseClasses = {baseNamed("Animal"), baseNamed("Loggable")};
    mod.types.push_back(std::move(ty));

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    // Struct body is unaffected by the inheritance machinery.
    EXPECT_NE(out.code.find("struct Dog {"), std::string::npos)
        << "Struct definition must be unchanged. Got:\n" << out.code;
    EXPECT_NE(out.code.find("age: i32,"), std::string::npos)
        << "Field must be unchanged. Got:\n" << out.code;

    // One marker trait per distinct base, each declared exactly once.
    EXPECT_EQ(countOccurrences(out.code, "trait AnimalTrait {}"), 1)
        << "Got:\n" << out.code;
    EXPECT_EQ(countOccurrences(out.code, "trait LoggableTrait {}"), 1)
        << "Got:\n" << out.code;

    // One impl per base, no lifetime (struct has no reference field).
    EXPECT_NE(out.code.find("impl AnimalTrait for Dog {}"), std::string::npos)
        << "Got:\n" << out.code;
    EXPECT_NE(out.code.find("impl LoggableTrait for Dog {}"), std::string::npos)
        << "Got:\n" << out.code;
}

TEST(TranspileRustInheritance, EmptyBaseClassesIsByteIdenticalToPreChange) {
    // No base classes -> output must be exactly the pre-inheritance form.
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "Plain";
    TranspileField f;
    f.name = "n";
    f.type.nameParts = {"i32"};
    ty.fields = {f};
    mod.types.push_back(std::move(ty));

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_EQ(out.code, "struct Plain {\n    n: i32,\n}\n\n")
        << "Empty baseClasses must emit byte-identically. Got:\n" << out.code;
}

TEST(TranspileRustInheritance, SharedBaseTraitDeclaredExactlyOnce) {
    // Two structs derive from Animal -> `trait AnimalTrait {}` appears once,
    // but each struct still gets its own impl.
    TranspileModule mod;
    for (const char* nm : {"Dog", "Cat"}) {
        TranspileType ty;
        ty.qualifiedName = std::string("test::") + nm;
        TranspileField f;
        f.name = "id";
        f.type.nameParts = {"i32"};
        ty.fields = {f};
        ty.baseClasses = {baseNamed("Animal")};
        mod.types.push_back(std::move(ty));
    }

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_EQ(countOccurrences(out.code, "trait AnimalTrait {}"), 1)
        << "Shared base trait must be declared exactly once. Got:\n"
        << out.code;
    EXPECT_NE(out.code.find("impl AnimalTrait for Dog {}"), std::string::npos)
        << "Got:\n" << out.code;
    EXPECT_NE(out.code.find("impl AnimalTrait for Cat {}"), std::string::npos)
        << "Got:\n" << out.code;
}

TEST(TranspileRustInheritance, RefFieldStructImplCarriesLifetime) {
    // struct Holder { r: &T } : Tracked  ->  impl must repeat `<'a>` and the
    // struct's lifetime parameter so the impl compiles.
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "test::Holder";
    TranspileField f;
    f.name = "r";
    f.type = refTo("T");
    ty.fields = {f};
    ty.baseClasses = {baseNamed("Tracked")};
    mod.types.push_back(std::move(ty));

    RustEmitter emitter;
    auto out = emitter.emit(mod);

    EXPECT_NE(out.code.find("struct Holder<'a>"), std::string::npos)
        << "Ref-field struct still needs `<'a>`. Got:\n" << out.code;
    EXPECT_EQ(countOccurrences(out.code, "trait TrackedTrait {}"), 1)
        << "Got:\n" << out.code;
    EXPECT_NE(out.code.find("impl<'a> TrackedTrait for Holder<'a> {}"),
              std::string::npos)
        << "Impl over a lifetime-parameterised struct must repeat `<'a>`. "
           "Got:\n"
        << out.code;
}

// =====================================================================
// RustEmitter: NonTypeParam default literal — acceptance shape.
// `<const N: usize = 16>` on a struct/enum/trait renders the default
// verbatim; the function-level emit path drops it with a downgrade note
// (Rust E0091 forbids defaults on free functions).
// =====================================================================

TEST(TranspileNewConstructsEmitter, RustNonTypeParamDefaultOnTypeAppendsAssign) {
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "Buf";
    topo::TemplateParamDecl tp;
    tp.kind = topo::TemplateParamDecl::NonTypeParam;
    tp.name = "N";
    tp.constraintType.nameParts = {"usize"};
    tp.defaultValue = "16";
    ty.templateParams = {tp};
    mod.types.push_back(std::move(ty));

    RustEmitter emitter;
    auto out = emitter.emit(mod);
    EXPECT_NE(out.code.find("struct Buf<const N: usize = 16>"),
              std::string::npos)
        << "Generated:\n" << out.code;
}

TEST(TranspileNewConstructsEmitter, RustNonTypeParamWithoutDefaultUnchanged) {
    // Byte-identical contract: an existing `<const N: usize>` model
    // renders without any trailing `= …`.
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "Buf";
    topo::TemplateParamDecl tp;
    tp.kind = topo::TemplateParamDecl::NonTypeParam;
    tp.name = "N";
    tp.constraintType.nameParts = {"usize"};
    ty.templateParams = {tp};
    mod.types.push_back(std::move(ty));

    RustEmitter emitter;
    auto out = emitter.emit(mod);
    EXPECT_NE(out.code.find("struct Buf<const N: usize>"), std::string::npos)
        << "Generated:\n" << out.code;
    EXPECT_EQ(out.code.find("= "), std::string::npos)
        << "Absent defaultValue must not introduce an `= …` clause; got:\n"
        << out.code;
}

TEST(TranspileNewConstructsEmitter, RustNonTypeParamDefaultOnFnIsDropped) {
    // Rust E0091: free functions cannot carry defaults on type or const
    // generic parameters. The emitter strips the default and surfaces a
    // downgrade note.
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "scan";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl tp;
    tp.kind = topo::TemplateParamDecl::NonTypeParam;
    tp.name = "N";
    tp.constraintType.nameParts = {"usize"};
    tp.defaultValue = "16";
    fn.templateParams = {tp};
    mod.functions.push_back(std::move(fn));

    RustEmitter emitter;
    auto out = emitter.emit(mod);
    EXPECT_NE(out.code.find("fn scan<const N: usize>"), std::string::npos)
        << "Generated:\n" << out.code;
    EXPECT_EQ(out.code.find("= 16"), std::string::npos)
        << "Function-level default must be dropped; got:\n" << out.code;
    EXPECT_NE(out.code.find("// TOPO-TRANSPILE: default on const parameter `N`"),
              std::string::npos)
        << "Expected downgrade note; got:\n" << out.code;
}

// =====================================================================
// JSON round-trip: variadic pack + template-template params
// =====================================================================

// A TypeParam flagged `isVariadic` round-trips through the `isVariadic`
// wire key (kind stays "type" — the pack flag is orthogonal).
TEST(TranspileTemplateParamRoundTrip, VariadicTypeParamRoundTrips) {
    TranspileType ty;
    ty.qualifiedName = "Tuple";
    topo::TemplateParamDecl tp;
    tp.kind = topo::TemplateParamDecl::TypeParam;
    tp.name = "Ts";
    tp.isVariadic = true;
    ty.templateParams = {tp};

    nlohmann::json j;
    to_json(j, ty);
    ASSERT_EQ(j["templateParams"].size(), 1u);
    EXPECT_EQ(j["templateParams"][0]["kind"], "type");
    EXPECT_EQ(j["templateParams"][0]["isVariadic"], true);

    TranspileType back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    EXPECT_EQ(back.templateParams[0].kind, topo::TemplateParamDecl::TypeParam);
    EXPECT_TRUE(back.templateParams[0].isVariadic);
    EXPECT_EQ(back.templateParams[0].name, "Ts");
}

// A non-variadic TypeParam omits the `isVariadic` key entirely — the wire
// stays byte-identical to pre-Phase-6 output.
TEST(TranspileTemplateParamRoundTrip, NonVariadicOmitsIsVariadicKey) {
    TranspileType ty;
    ty.qualifiedName = "Box";
    topo::TemplateParamDecl tp;
    tp.kind = topo::TemplateParamDecl::TypeParam;
    tp.name = "T";
    ty.templateParams = {tp};

    nlohmann::json j;
    to_json(j, ty);
    ASSERT_EQ(j["templateParams"].size(), 1u);
    EXPECT_FALSE(j["templateParams"][0].contains("isVariadic"))
        << "non-variadic param must omit the isVariadic key: "
        << j["templateParams"][0].dump();
    EXPECT_FALSE(j["templateParams"][0].contains("innerParams"))
        << "a plain type param must omit innerParams: "
        << j["templateParams"][0].dump();
}

// A TemplateTemplateParam round-trips kind="template" plus its recursive
// `innerParams` list.
TEST(TranspileTemplateParamRoundTrip, TemplateTemplateParamRoundTrips) {
    TranspileType ty;
    ty.qualifiedName = "Holder";
    topo::TemplateParamDecl tp;
    tp.kind = topo::TemplateParamDecl::TemplateTemplateParam;
    tp.name = "C";
    topo::TemplateParamDecl inner;
    inner.kind = topo::TemplateParamDecl::TypeParam;
    inner.name = "Inner";
    tp.innerParams = {inner};
    ty.templateParams = {tp};

    nlohmann::json j;
    to_json(j, ty);
    ASSERT_EQ(j["templateParams"].size(), 1u);
    EXPECT_EQ(j["templateParams"][0]["kind"], "template");
    ASSERT_TRUE(j["templateParams"][0].contains("innerParams"));
    ASSERT_EQ(j["templateParams"][0]["innerParams"].size(), 1u);
    EXPECT_EQ(j["templateParams"][0]["innerParams"][0]["kind"], "type");
    EXPECT_EQ(j["templateParams"][0]["innerParams"][0]["name"], "Inner");

    TranspileType back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    EXPECT_EQ(back.templateParams[0].kind,
              topo::TemplateParamDecl::TemplateTemplateParam);
    EXPECT_EQ(back.templateParams[0].name, "C");
    ASSERT_EQ(back.templateParams[0].innerParams.size(), 1u);
    EXPECT_EQ(back.templateParams[0].innerParams[0].kind,
              topo::TemplateParamDecl::TypeParam);
    EXPECT_EQ(back.templateParams[0].innerParams[0].name, "Inner");
}

// A variadic inner param inside a TemplateTemplateParam survives the
// recursive round-trip (`template <typename...> class C`).
TEST(TranspileTemplateParamRoundTrip, TemplateTemplateParamVariadicInnerRoundTrips) {
    TranspileFunction fn;
    fn.qualifiedName = "wrap";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl tp;
    tp.kind = topo::TemplateParamDecl::TemplateTemplateParam;
    tp.name = "C";
    topo::TemplateParamDecl inner;
    inner.kind = topo::TemplateParamDecl::TypeParam;
    inner.isVariadic = true;
    tp.innerParams = {inner};
    fn.templateParams = {tp};

    nlohmann::json j;
    to_json(j, fn);
    TranspileFunction back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    ASSERT_EQ(back.templateParams[0].innerParams.size(), 1u);
    EXPECT_TRUE(back.templateParams[0].innerParams[0].isVariadic);
}

// =====================================================================
// Python TypeVar constraint-tuple as a union bound.
// `T = TypeVar('T', int, str)` lowers (in the Python extractor) to a
// templateParam whose `bound` is a union TypeNode: nameParts == ["union"]
// with the variant types carried positionally in `templateArgs`. This is
// the untagged member-choice sense (`T` is exactly one of the listed
// types) — distinct from the stdlib *tagged* `union<tag: …, v1: …>` whose
// discriminant + named fields ride `recordFields`.
// =====================================================================

namespace {
// A type param `T` whose `bound` is a positional union of `members`.
topo::TemplateParamDecl unionBoundTypeParam(const std::string& name,
                                            const std::vector<std::string>& members) {
    topo::TemplateParamDecl tp;
    tp.kind = topo::TemplateParamDecl::TypeParam;
    tp.name = name;
    tp.constraintType.nameParts = {"union"};
    for (const auto& m : members) {
        topo::TypeNode arg;
        arg.nameParts = {m};
        tp.constraintType.templateArgs.push_back(std::move(arg));
    }
    return tp;
}
} // namespace

// The union bound round-trips through the wire: `bound` is a TypeNode and
// its positional `templateArgs` carry the variant types. (`recordFields`
// is intentionally NOT used — it has no transpile-wire serialization.)
TEST(TranspileTemplateParamRoundTrip, UnionConstraintBoundRoundTrips) {
    TranspileFunction fn;
    fn.qualifiedName = "pick";
    fn.returnType.nameParts = {"void"};
    fn.templateParams = {unionBoundTypeParam("T", {"i64", "string"})};

    nlohmann::json j;
    to_json(j, fn);
    ASSERT_EQ(j["templateParams"].size(), 1u);
    ASSERT_TRUE(j["templateParams"][0].contains("bound"));
    EXPECT_EQ(j["templateParams"][0]["bound"]["nameParts"][0], "union");
    ASSERT_EQ(j["templateParams"][0]["bound"]["templateArgs"].size(), 2u);

    TranspileFunction back;
    from_json(j, back);
    ASSERT_EQ(back.templateParams.size(), 1u);
    const auto& bound = back.templateParams[0].constraintType;
    ASSERT_EQ(bound.nameParts.size(), 1u);
    EXPECT_EQ(bound.nameParts[0], "union");
    ASSERT_EQ(bound.templateArgs.size(), 2u);
    EXPECT_EQ(bound.templateArgs[0].nameParts[0], "i64");
    EXPECT_EQ(bound.templateArgs[1].nameParts[0], "string");
}

// PythonEmitter renders a union-bound type param as PEP 604 `[T: int | str]`.
TEST(TranspileNewConstructsEmitter, PythonUnionConstraintBoundEmitsPep604) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "pick";
    fn.returnType.nameParts = {"void"};
    fn.templateParams = {unionBoundTypeParam("T", {"i64", "string"})};
    mod.functions.push_back(std::move(fn));

    PythonEmitter emitter;
    auto out = emitter.emit(mod);
    EXPECT_NE(out.code.find("[T: int | str]"), std::string::npos)
        << "Union-bounded TypeVar must render PEP 604 `[T: int | str]`. Got:\n"
        << out.code;
}

// A three-member union keeps order and chains the `|`.
TEST(TranspileNewConstructsEmitter, PythonUnionConstraintBoundThreeMembers) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "norm";
    fn.returnType.nameParts = {"void"};
    fn.templateParams = {unionBoundTypeParam("N", {"i64", "f64", "bool"})};
    mod.functions.push_back(std::move(fn));

    PythonEmitter emitter;
    auto out = emitter.emit(mod);
    EXPECT_NE(out.code.find("[N: int | float | bool]"), std::string::npos)
        << "Three-member union must render `int | float | bool`. Got:\n"
        << out.code;
}

// Byte-identical contract: a plain (non-union) single `bound` TypeVar must
// emit exactly as before — the union path must not perturb the legacy
// scalar-bound rendering.
TEST(TranspileNewConstructsEmitter, PythonScalarBoundByteIdenticalAfterUnionPath) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "pick";
    fn.returnType.nameParts = {"void"};
    topo::TemplateParamDecl tp;
    tp.kind = topo::TemplateParamDecl::TypeParam;
    tp.name = "T";
    tp.constraintType.nameParts = {"i64"};
    fn.templateParams = {tp};
    mod.functions.push_back(std::move(fn));

    PythonEmitter emitter;
    auto out = emitter.emit(mod);
    EXPECT_NE(out.code.find("[T: int]"), std::string::npos)
        << "Scalar `bound` must still render `[T: int]`. Got:\n" << out.code;
    EXPECT_EQ(out.code.find("|"), std::string::npos)
        << "Scalar bound must carry no union `|`. Got:\n" << out.code;
}
