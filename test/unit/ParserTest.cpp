#include "topo/AST/ASTNode.h"
#include "topo/AST/ASTPrinter.h"
#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>

using namespace topo;

struct ParseResult {
    std::unique_ptr<TopoFile> ast;
    DiagnosticEngine diag;
};

static ParseResult parse(const std::string& source) {
    ParseResult result;
    Lexer lexer(source, "<test>", result.diag);
    Parser parser(lexer, result.diag);
    result.ast = parser.parseTopoFile();
    return result;
}

static std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// --- Empty file ---

TEST(Parser, EmptyFile) {
    auto [ast, diag] = parse("");
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_NE(ast, nullptr);
    EXPECT_TRUE(ast->declarations.empty());
}

// --- Import ---

TEST(Parser, FileImport) {
    auto [ast, diag] = parse("import other;");
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(ast->declarations.size(), 1u);
    EXPECT_EQ(ast->declarations[0]->kind, ASTKind::FileImport);
    auto& imp = static_cast<FileImport&>(*ast->declarations[0]);
    EXPECT_EQ(imp.path, "other");
    EXPECT_TRUE(imp.selectedSymbols.empty());
}

TEST(Parser, ImportSlashPath) {
    auto [ast, diag] = parse("import core/types;");
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(ast->declarations.size(), 1u);
    auto& imp = static_cast<Import&>(*ast->declarations[0]);
    EXPECT_EQ(imp.path, "core/types");
    EXPECT_TRUE(imp.selectedSymbols.empty());
}

TEST(Parser, ImportDeepPath) {
    auto [ast, diag] = parse("import a/b/c;");
    ASSERT_FALSE(diag.hasErrors());
    auto& imp = static_cast<Import&>(*ast->declarations[0]);
    EXPECT_EQ(imp.path, "a/b/c");
}

TEST(Parser, ImportWithSymbolSelection) {
    auto [ast, diag] = parse("import types { Foo, Bar };");
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(ast->declarations.size(), 1u);
    auto& imp = static_cast<Import&>(*ast->declarations[0]);
    EXPECT_EQ(imp.path, "types");
    ASSERT_EQ(imp.selectedSymbols.size(), 2u);
    EXPECT_EQ(imp.selectedSymbols[0], "Foo");
    EXPECT_EQ(imp.selectedSymbols[1], "Bar");
}

TEST(Parser, ImportPathWithSelection) {
    auto [ast, diag] = parse("import core/types { ResourceId };");
    ASSERT_FALSE(diag.hasErrors());
    auto& imp = static_cast<Import&>(*ast->declarations[0]);
    EXPECT_EQ(imp.path, "core/types");
    ASSERT_EQ(imp.selectedSymbols.size(), 1u);
    EXPECT_EQ(imp.selectedSymbols[0], "ResourceId");
}

TEST(Parser, ImportSingleSymbolSelection) {
    auto [ast, diag] = parse("import math { Vector3 };");
    ASSERT_FALSE(diag.hasErrors());
    auto& imp = static_cast<Import&>(*ast->declarations[0]);
    EXPECT_EQ(imp.path, "math");
    ASSERT_EQ(imp.selectedSymbols.size(), 1u);
    EXPECT_EQ(imp.selectedSymbols[0], "Vector3");
}

// --- Using declaration ---

TEST(Parser, UsingDeclaration) {
    auto [ast, diag] = parse("using IntPtr = std::cpp17::intptr_t;");
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(ast->declarations.size(), 1u);
    EXPECT_EQ(ast->declarations[0]->kind, ASTKind::TypeAliasDecl);
    auto& alias = static_cast<TypeAliasDecl&>(*ast->declarations[0]);
    EXPECT_EQ(alias.name, "IntPtr");
    ASSERT_EQ(alias.type.nameParts.size(), 3u);
    EXPECT_EQ(alias.type.nameParts[0], "std");
    EXPECT_EQ(alias.type.nameParts[1], "cpp17");
    EXPECT_EQ(alias.type.nameParts[2], "intptr_t");
}

// --- Namespace path ---

TEST(Parser, NamespacePath) {
    auto [ast, diag] = parse("namespace a::b {\n  public:\n    void foo();\n}");
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(ast->declarations.size(), 1u);
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    ASSERT_EQ(ns.path.size(), 2u);
    EXPECT_EQ(ns.path[0], "a");
    EXPECT_EQ(ns.path[1], "b");
}

// --- Visibility sections ---

TEST(Parser, ThreeVisibilitySections) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n    void a();\n"
        "  protected:\n    void b();\n"
        "  private:\n    void c();\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    ASSERT_EQ(ns.sections.size(), 3u);

    auto& pub = static_cast<VisibilitySection&>(*ns.sections[0]);
    EXPECT_EQ(pub.visibility, Visibility::Public);
    EXPECT_EQ(pub.members.size(), 1u);

    auto& prot = static_cast<VisibilitySection&>(*ns.sections[1]);
    EXPECT_EQ(prot.visibility, Visibility::Protected);
    EXPECT_EQ(prot.members.size(), 1u);

    auto& priv = static_cast<VisibilitySection&>(*ns.sections[2]);
    EXPECT_EQ(priv.visibility, Visibility::Private);
    EXPECT_EQ(priv.members.size(), 1u);
}

// --- C++ style function declaration ---

TEST(Parser, CppStyleFunction) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    void run(const int& count, bool flag);\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    ASSERT_EQ(sec.members.size(), 1u);
    auto& fn = static_cast<FunctionDecl&>(*sec.members[0]);

    EXPECT_EQ(fn.name, "run");
    EXPECT_EQ(fn.returnType.nameParts.size(), 1u);
    EXPECT_EQ(fn.returnType.nameParts[0], "void");
    EXPECT_FALSE(fn.isRustStyle);
    EXPECT_FALSE(fn.isConst);
    ASSERT_EQ(fn.params.size(), 2u);
    EXPECT_TRUE(fn.params[0].type.isConst);
    EXPECT_EQ(fn.params[0].type.modifier, TypeNode::Ref);
    EXPECT_EQ(fn.params[0].name, "count");
    EXPECT_EQ(fn.params[1].type.nameParts[0], "bool");
    EXPECT_EQ(fn.params[1].name, "flag");
}

// --- Rust style function declaration ---

TEST(Parser, RustStyleFunction) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    isReady() -> bool;\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FunctionDecl&>(*sec.members[0]);

    EXPECT_EQ(fn.name, "isReady");
    EXPECT_TRUE(fn.isRustStyle);
    EXPECT_EQ(fn.returnType.nameParts[0], "bool");
}

// --- fn block with stages ---

TEST(Parser, FnBlockWithStages) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    fn run {\n"
        "      stage<1> init();\n"
        "      stage<2> process();\n"
        "    }\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);

    // Find the FunctionLogicBlock
    ASSERT_GE(sec.members.size(), 1u);
    auto& block = static_cast<FunctionLogicBlock&>(*sec.members[0]);
    EXPECT_EQ(block.name, "run");
    ASSERT_EQ(block.operations.size(), 2u);

    auto& op1 = static_cast<OperationDecl&>(*block.operations[0]);
    EXPECT_EQ(op1.stage, 1);
    EXPECT_EQ(op1.funcName, "init");

    auto& op2 = static_cast<OperationDecl&>(*block.operations[1]);
    EXPECT_EQ(op2.stage, 2);
    EXPECT_EQ(op2.funcName, "process");
}

// --- Auto stage (no explicit stage) ---

TEST(Parser, AutoStage) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    fn run {\n"
        "      doSomething();\n"
        "    }\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& block = static_cast<FunctionLogicBlock&>(*sec.members[0]);
    ASSERT_EQ(block.operations.size(), 1u);
    auto& op = static_cast<OperationDecl&>(*block.operations[0]);
    EXPECT_EQ(op.stage, -1); // auto stage
}

// --- Dotted function name ---

TEST(Parser, DottedFuncName) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    fn telemetry.init {\n"
        "      stage<1> start();\n"
        "    }\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& block = static_cast<FunctionLogicBlock&>(*sec.members[0]);
    EXPECT_EQ(block.name, "telemetry.init");
}

// --- const function ---

TEST(Parser, ConstFunction) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    void draw() const;\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FunctionDecl&>(*sec.members[0]);
    EXPECT_TRUE(fn.isConst);
}

// --- Pointer and reference modifiers ---

TEST(Parser, PointerAndRefModifiers) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    void foo(int* p, int& r);\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FunctionDecl&>(*sec.members[0]);
    ASSERT_EQ(fn.params.size(), 2u);
    EXPECT_EQ(fn.params[0].type.modifier, TypeNode::Ptr);
    EXPECT_EQ(fn.params[1].type.modifier, TypeNode::Ref);
}

// --- Error recovery ---

TEST(Parser, ErrorRecovery) {
    auto [ast, diag] = parse(
        "namespace test {\n"
        "  public:\n"
        "    void foo(;\n"
        "}");
    // Should produce error but still return an AST
    EXPECT_TRUE(diag.hasErrors());
    ASSERT_NE(ast, nullptr);
}

// --- Fixture: demo.topo end-to-end ---

TEST(Parser, DemoFixtureEndToEnd) {
    std::string source = readFile(TOPO_TEST_FIXTURES_DIR "/demo.topo");
    ASSERT_FALSE(source.empty()) << "Could not read demo.topo fixture";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());

    // demo.topo: namespace app, public run() + fn block, protected init/process
    ASSERT_EQ(ast->declarations.size(), 1u);
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    EXPECT_EQ(ns.path.size(), 1u);
    EXPECT_EQ(ns.path[0], "app");
    // Has visibility sections
    EXPECT_GE(ns.sections.size(), 2u);
}

// --- Assignment in fn block ---

TEST(Parser, AssignmentOperation) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      result = 42;\n"
        "    }\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    // Find the FunctionLogicBlock (second member)
    auto& block = static_cast<FunctionLogicBlock&>(*sec.members[1]);
    ASSERT_EQ(block.operations.size(), 1u);
    auto& op = static_cast<OperationDecl&>(*block.operations[0]);
    EXPECT_TRUE(op.isAssignment());
    EXPECT_EQ(op.varName, "result");
    ASSERT_NE(op.expr, nullptr);
    EXPECT_EQ(op.expr->kind, ASTKind::LiteralExpr);
}

// --- Binary expression ---

TEST(Parser, BinaryExpression) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      val = a + b * c;\n"
        "    }\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& block = static_cast<FunctionLogicBlock&>(*sec.members[1]);
    ASSERT_EQ(block.operations.size(), 1u);
    auto& op = static_cast<OperationDecl&>(*block.operations[0]);
    EXPECT_TRUE(op.isAssignment());
    // val = a + (b * c) — binary expr at top level
    ASSERT_NE(op.expr, nullptr);
    EXPECT_EQ(op.expr->kind, ASTKind::BinaryExpr);
    auto& binExpr = static_cast<BinaryExpr&>(*op.expr);
    EXPECT_EQ(binExpr.op, TokenKind::Plus);
    EXPECT_EQ(binExpr.lhs->kind, ASTKind::IdentifierExpr);
    EXPECT_EQ(binExpr.rhs->kind, ASTKind::BinaryExpr);
}

// --- Unary expression ---

TEST(Parser, UnaryExpression) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      val = !flag;\n"
        "    }\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& block = static_cast<FunctionLogicBlock&>(*sec.members[1]);
    auto& op = static_cast<OperationDecl&>(*block.operations[0]);
    ASSERT_NE(op.expr, nullptr);
    EXPECT_EQ(op.expr->kind, ASTKind::UnaryExpr);
    auto& unary = static_cast<UnaryExpr&>(*op.expr);
    EXPECT_EQ(unary.op, TokenKind::Bang);
}

// --- Call expression in assignment ---

TEST(Parser, CallExprInAssignment) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      val = getCount();\n"
        "    }\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& block = static_cast<FunctionLogicBlock&>(*sec.members[1]);
    auto& op = static_cast<OperationDecl&>(*block.operations[0]);
    ASSERT_NE(op.expr, nullptr);
    EXPECT_EQ(op.expr->kind, ASTKind::CallExpr);
    auto& call = static_cast<CallExpr&>(*op.expr);
    EXPECT_EQ(call.funcName, "getCount");
}

// --- Assignment with stage ---

TEST(Parser, AssignmentWithStage) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      stage<1> result = compute();\n"
        "    }\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& block = static_cast<FunctionLogicBlock&>(*sec.members[1]);
    auto& op = static_cast<OperationDecl&>(*block.operations[0]);
    EXPECT_TRUE(op.isAssignment());
    EXPECT_EQ(op.stage, 1);
    EXPECT_EQ(op.varName, "result");
}

// --- std::import ---

TEST(Parser, StdImportWithType) {
    auto [ast, diag] = parse(
        "std::import(\"cstdint\", uint64_t);\n"
        "namespace x {\n"
        "  public:\n"
        "    void foo();\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_GE(ast->declarations.size(), 2u);
    EXPECT_EQ(ast->declarations[0]->kind, ASTKind::DataDecl);
    auto& imp = static_cast<DataDecl&>(*ast->declarations[0]);
    EXPECT_EQ(imp.importPath, "cstdint");
    EXPECT_EQ(imp.name, "uint64_t");
}

TEST(Parser, StdImportWithoutType) {
    auto [ast, diag] = parse(
        "std::import(\"iostream\");\n"
        "namespace x {\n"
        "  public:\n"
        "    void foo();\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_GE(ast->declarations.size(), 2u);
    auto& imp = static_cast<DataDecl&>(*ast->declarations[0]);
    EXPECT_EQ(imp.importPath, "iostream");
    EXPECT_TRUE(imp.name.empty());
}

// --- Expression fixture ---

TEST(Parser, ExpressionFixture) {
    std::string source = readFile(TOPO_TEST_FIXTURES_DIR "/expression_test.topo");
    ASSERT_FALSE(source.empty()) << "Could not read expression_test.topo fixture";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_GE(ast->declarations.size(), 1u);
}

// --- Import fixture ---

TEST(Parser, ImportFixture) {
    std::string source = readFile(TOPO_TEST_FIXTURES_DIR "/import_test.topo");
    ASSERT_FALSE(source.empty()) << "Could not read import_test.topo fixture";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
    // Should have 2 std::import + 1 namespace
    ASSERT_GE(ast->declarations.size(), 3u);
    EXPECT_EQ(ast->declarations[0]->kind, ASTKind::StdImportDecl);
    EXPECT_EQ(ast->declarations[1]->kind, ASTKind::StdImportDecl);
}

// --- Fixture: all_features.topo end-to-end ---

TEST(Parser, AllFeaturesFixture) {
    std::string source = readFile(TOPO_TEST_FIXTURES_DIR "/all_features.topo");
    ASSERT_FALSE(source.empty()) << "Could not read all_features.topo fixture";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());

    // Should have: import, using x3, namespace
    ASSERT_GE(ast->declarations.size(), 5u);
    EXPECT_EQ(ast->declarations[0]->kind, ASTKind::FileImport);
    EXPECT_EQ(ast->declarations[1]->kind, ASTKind::TypeAliasDecl);
    EXPECT_EQ(ast->declarations[2]->kind, ASTKind::TypeAliasDecl);
    EXPECT_EQ(ast->declarations[3]->kind, ASTKind::TypeAliasDecl);
    EXPECT_EQ(ast->declarations[4]->kind, ASTKind::NamespaceDecl);
}

// =============================================================
// Multi-return tests
// =============================================================

TEST(Parser, MultiReturnDeclaration) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    compute(int a, int b) -> (int sum, int product);\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FunctionDecl&>(*sec.members[0]);

    EXPECT_EQ(fn.name, "compute");
    EXPECT_TRUE(fn.isRustStyle);
    EXPECT_TRUE(fn.isMultiReturn);
    ASSERT_EQ(fn.returnParams.size(), 2u);
    EXPECT_EQ(fn.returnParams[0].type.nameParts[0], "int");
    EXPECT_EQ(fn.returnParams[0].name, "sum");
    EXPECT_EQ(fn.returnParams[1].type.nameParts[0], "int");
    EXPECT_EQ(fn.returnParams[1].name, "product");
    ASSERT_EQ(fn.params.size(), 2u);
    EXPECT_EQ(fn.params[0].name, "a");
    EXPECT_EQ(fn.params[1].name, "b");
}

TEST(Parser, MultiReturnSignature) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    compute(int a) -> (int x, bool y);\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FunctionDecl&>(*sec.members[0]);

    std::string sig = fn.signature();
    EXPECT_NE(sig.find("-> (int x, bool y)"), std::string::npos);
}

// --- `with returns(...)` selective-return clause ---

TEST(Parser, WithReturnsBasic) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    compute(int a) -> (int x, int y, int z) with returns(x, _, _);\n"
        "}");
    ASSERT_FALSE(diag.hasErrors()) << "parse should accept `with returns(x, _, _)`";
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FunctionDecl&>(*sec.members[0]);

    EXPECT_TRUE(fn.isMultiReturn);
    EXPECT_TRUE(fn.hasUsedReturnsClause);
    ASSERT_EQ(fn.declaredUsedReturns.size(), 1u);
    EXPECT_EQ(fn.declaredUsedReturns[0], "x");
}

TEST(Parser, WithReturnsAllNamed) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    divmod(int a, int b) -> (int q, int r) with returns(q, r);\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FunctionDecl&>(*sec.members[0]);

    EXPECT_TRUE(fn.hasUsedReturnsClause);
    ASSERT_EQ(fn.declaredUsedReturns.size(), 2u);
    EXPECT_EQ(fn.declaredUsedReturns[0], "q");
    EXPECT_EQ(fn.declaredUsedReturns[1], "r");
}

TEST(Parser, WithReturnsAllUnderscore) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    probe(int a) -> (int a, int b, int c) with returns(_, _, _);\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FunctionDecl&>(*sec.members[0]);

    EXPECT_TRUE(fn.hasUsedReturnsClause);
    EXPECT_TRUE(fn.declaredUsedReturns.empty()) << "all-underscore clause yields no named fields";
}

TEST(Parser, WithReturnsEmptyIsError) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    compute(int a) -> (int x, int y) with returns();\n"
        "}");
    EXPECT_TRUE(diag.hasErrors()) << "empty `with returns()` must be rejected";
}

TEST(Parser, WithReturnsDuplicateIsError) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    compute(int a) -> (int x, int y) with returns(x, x);\n"
        "}");
    EXPECT_TRUE(diag.hasErrors());
}

TEST(Parser, WithReturnsTooManyIsError) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    compute(int a) -> (int x, int y) with returns(x, _, _, _);\n"
        "}");
    EXPECT_TRUE(diag.hasErrors());
}

TEST(Parser, WithReturnsOnSingleReturnIsError) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    compute(int a) -> int with returns(x);\n"
        "}");
    EXPECT_TRUE(diag.hasErrors()) << "clause not allowed on single-return fn";
}

TEST(Parser, ReturnBindingSingleValue) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      compute() -> result;\n"
        "    }\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& block = static_cast<FunctionLogicBlock&>(*sec.members[1]);
    ASSERT_EQ(block.operations.size(), 1u);
    auto& op = static_cast<OperationDecl&>(*block.operations[0]);
    EXPECT_EQ(op.funcName, "compute");
    ASSERT_TRUE(op.returnBinding.has_value());
    EXPECT_TRUE(op.returnBinding->isSingleValue);
    EXPECT_EQ(op.returnBinding->singleName, "result");
}

TEST(Parser, ReturnBindingDestructuring) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      compute() -> (a, _, c);\n"
        "    }\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& block = static_cast<FunctionLogicBlock&>(*sec.members[1]);
    auto& op = static_cast<OperationDecl&>(*block.operations[0]);
    ASSERT_TRUE(op.returnBinding.has_value());
    EXPECT_FALSE(op.returnBinding->isSingleValue);
    ASSERT_EQ(op.returnBinding->targets.size(), 3u);
    EXPECT_EQ(op.returnBinding->targets[0].name, "a");
    EXPECT_FALSE(op.returnBinding->targets[0].isDiscard);
    EXPECT_EQ(op.returnBinding->targets[1].name, "_");
    EXPECT_TRUE(op.returnBinding->targets[1].isDiscard);
    EXPECT_EQ(op.returnBinding->targets[2].name, "c");
    EXPECT_FALSE(op.returnBinding->targets[2].isDiscard);
}

TEST(Parser, ReturnBindingWithStage) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      stage<1> compute() -> val;\n"
        "    }\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& block = static_cast<FunctionLogicBlock&>(*sec.members[1]);
    auto& op = static_cast<OperationDecl&>(*block.operations[0]);
    EXPECT_EQ(op.stage, 1);
    ASSERT_TRUE(op.returnBinding.has_value());
    EXPECT_TRUE(op.returnBinding->isSingleValue);
    EXPECT_EQ(op.returnBinding->singleName, "val");
}

// =============================================================
// Pipeline tests
// =============================================================

TEST(Parser, PipelineEdges) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      a -> b;\n"
        "      b -> c;\n"
        "      c -> void;\n"
        "    }\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& block = static_cast<FunctionLogicBlock&>(*sec.members[1]);

    EXPECT_TRUE(block.isPipeline());
    ASSERT_EQ(block.pipelineEdges.size(), 3u);

    EXPECT_EQ(block.pipelineEdges[0].source, "a");
    EXPECT_EQ(block.pipelineEdges[0].target, "b");
    EXPECT_FALSE(block.pipelineEdges[0].isTerminal);

    EXPECT_EQ(block.pipelineEdges[1].source, "b");
    EXPECT_EQ(block.pipelineEdges[1].target, "c");
    EXPECT_FALSE(block.pipelineEdges[1].isTerminal);

    EXPECT_EQ(block.pipelineEdges[2].source, "c");
    EXPECT_TRUE(block.pipelineEdges[2].isTerminal);
    EXPECT_EQ(block.pipelineEdges[2].terminalType, "void");
}

TEST(Parser, PipelineTerminalInt) {
    // After the type abstraction refactoring, 'int' is an Identifier.
    // In pipeline context, it becomes a function target (not a terminal type).
    // Terminal types are 'void' (keyword) or qualified type names.
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      a -> int;\n"
        "    }\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& block = static_cast<FunctionLogicBlock&>(*sec.members[1]);
    ASSERT_EQ(block.pipelineEdges.size(), 1u);
    // 'int' is now an identifier → treated as function target, not terminal
    EXPECT_FALSE(block.pipelineEdges[0].isTerminal);
    EXPECT_EQ(block.pipelineEdges[0].target, "int");
}

TEST(Parser, PipelineNotConfusedWithFnBlock) {
    // Normal fn block: first thing is a function call (Identifier + LParen)
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      doWork();\n"
        "    }\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& block = static_cast<FunctionLogicBlock&>(*sec.members[1]);
    EXPECT_FALSE(block.isPipeline());
    EXPECT_EQ(block.operations.size(), 1u);
}

TEST(Parser, MultiReturnFixture) {
    std::string source = readFile(TOPO_TEST_FIXTURES_DIR "/multi_return.topo");
    ASSERT_FALSE(source.empty()) << "Could not read multi_return.topo fixture";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
}

TEST(Parser, PipelineFixture) {
    std::string source = readFile(TOPO_TEST_FIXTURES_DIR "/pipeline.topo");
    ASSERT_FALSE(source.empty()) << "Could not read pipeline.topo fixture";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
}

// =============================================================
// Internal visibility tests
// =============================================================

TEST(Parser, InternalVisibilitySection) {
    auto [ast, diag] = parse(
        "namespace x {\n"
        "  internal:\n"
        "    void secret();\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    ASSERT_EQ(ns.sections.size(), 1u);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    EXPECT_EQ(sec.visibility, Visibility::Internal);
    EXPECT_EQ(sec.members.size(), 1u);
}

TEST(Parser, InternalNamespace) {
    auto [ast, diag] = parse(
        "internal namespace detail {\n"
        "  public:\n"
        "    void helper();\n"
        "}");
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(ast->declarations.size(), 1u);
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    EXPECT_TRUE(ns.isInternal);
    EXPECT_EQ(ns.path.size(), 1u);
    EXPECT_EQ(ns.path[0], "detail");
}

TEST(Parser, InternalBasicFixture) {
    std::string source = readFile(TOPO_TEST_FIXTURES_DIR "/internal_basic.topo");
    ASSERT_FALSE(source.empty()) << "Could not read internal_basic.topo fixture";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());

    // Should have one namespace
    ASSERT_EQ(ast->declarations.size(), 1u);
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    // Two sections: public + internal
    ASSERT_EQ(ns.sections.size(), 2u);

    auto& pubSec = static_cast<VisibilitySection&>(*ns.sections[0]);
    EXPECT_EQ(pubSec.visibility, Visibility::Public);

    auto& intSec = static_cast<VisibilitySection&>(*ns.sections[1]);
    EXPECT_EQ(intSec.visibility, Visibility::Internal);
    EXPECT_EQ(intSec.members.size(), 2u);
}

TEST(Parser, InternalNamespaceFixture) {
    std::string source = readFile(TOPO_TEST_FIXTURES_DIR "/internal_namespace.topo");
    ASSERT_FALSE(source.empty()) << "Could not read internal_namespace.topo fixture";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());

    // Should have two namespaces: internal detail + api
    ASSERT_EQ(ast->declarations.size(), 2u);

    auto& detailNs = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    EXPECT_TRUE(detailNs.isInternal);
    EXPECT_EQ(detailNs.path[0], "detail");

    auto& apiNs = static_cast<NamespaceDecl&>(*ast->declarations[1]);
    EXPECT_FALSE(apiNs.isInternal);
    EXPECT_EQ(apiNs.path[0], "api");
}

// --- Class declarations ---

TEST(Parser, BasicClassDecl) {
    auto [ast, diag] = parse(R"(
namespace math {
    public:
        class Vector {
            public:
                void push_back(int value);
                int size() const;
            private:
                int data;
                int count;
        }
}
)");
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(ast->declarations.size(), 1u);

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    ASSERT_EQ(sec.members.size(), 1u);
    EXPECT_EQ(sec.members[0]->kind, ASTKind::ClassDecl);

    auto& cls = static_cast<ClassDecl&>(*sec.members[0]);
    EXPECT_EQ(cls.name, "Vector");
    EXPECT_FALSE(cls.baseClass.has_value());
    ASSERT_EQ(cls.sections.size(), 2u);

    // Public section: 2 member functions
    auto& pubSec = static_cast<VisibilitySection&>(*cls.sections[0]);
    EXPECT_EQ(pubSec.visibility, Visibility::Public);
    ASSERT_EQ(pubSec.members.size(), 2u);
    EXPECT_EQ(pubSec.members[0]->kind, ASTKind::FunctionDecl);
    EXPECT_EQ(pubSec.members[1]->kind, ASTKind::FunctionDecl);

    auto& pushBack = static_cast<FunctionDecl&>(*pubSec.members[0]);
    EXPECT_EQ(pushBack.name, "push_back");

    auto& sizeFunc = static_cast<FunctionDecl&>(*pubSec.members[1]);
    EXPECT_EQ(sizeFunc.name, "size");
    EXPECT_TRUE(sizeFunc.isConst);

    // Private section: 2 member variables
    auto& privSec = static_cast<VisibilitySection&>(*cls.sections[1]);
    EXPECT_EQ(privSec.visibility, Visibility::Private);
    ASSERT_EQ(privSec.members.size(), 2u);
    EXPECT_EQ(privSec.members[0]->kind, ASTKind::MemberVarDecl);
    EXPECT_EQ(privSec.members[1]->kind, ASTKind::MemberVarDecl);

    auto& dataVar = static_cast<MemberVarDecl&>(*privSec.members[0]);
    EXPECT_EQ(dataVar.name, "data");
}

TEST(Parser, ClassInheritance) {
    auto [ast, diag] = parse(R"(
namespace shapes {
    public:
        class Shape {
            public:
                int area() const;
        }

        class Circle : public Shape {
            public:
                int radius() const;
        }
}
)");
    ASSERT_FALSE(diag.hasErrors());

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    ASSERT_EQ(sec.members.size(), 2u);

    auto& shape = static_cast<ClassDecl&>(*sec.members[0]);
    EXPECT_EQ(shape.name, "Shape");
    EXPECT_FALSE(shape.baseClass.has_value());

    auto& circle = static_cast<ClassDecl&>(*sec.members[1]);
    EXPECT_EQ(circle.name, "Circle");
    ASSERT_TRUE(circle.baseClass.has_value());
    EXPECT_EQ(circle.baseClass->nameParts.size(), 1u);
    EXPECT_EQ(circle.baseClass->nameParts[0], "Shape");
}

TEST(Parser, ClassConstructorDestructor) {
    auto [ast, diag] = parse(R"(
namespace math {
    public:
        class Matrix {
            public:
                Matrix(int rows, int cols);
                explicit Matrix(int size);
                ~Matrix();
                int get(int r, int c) const;
        }
}
)");
    ASSERT_FALSE(diag.hasErrors());

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& cls = static_cast<ClassDecl&>(*sec.members[0]);
    EXPECT_EQ(cls.name, "Matrix");

    auto& pubSec = static_cast<VisibilitySection&>(*cls.sections[0]);
    ASSERT_EQ(pubSec.members.size(), 4u);

    // Non-explicit constructor
    EXPECT_EQ(pubSec.members[0]->kind, ASTKind::ConstructorDecl);
    auto& ctor1 = static_cast<ConstructorDecl&>(*pubSec.members[0]);
    EXPECT_FALSE(ctor1.isExplicit);
    EXPECT_EQ(ctor1.params.size(), 2u);

    // Explicit constructor
    EXPECT_EQ(pubSec.members[1]->kind, ASTKind::ConstructorDecl);
    auto& ctor2 = static_cast<ConstructorDecl&>(*pubSec.members[1]);
    EXPECT_TRUE(ctor2.isExplicit);
    EXPECT_EQ(ctor2.params.size(), 1u);

    // Destructor
    EXPECT_EQ(pubSec.members[2]->kind, ASTKind::DestructorDecl);
    auto& dtor = static_cast<DestructorDecl&>(*pubSec.members[2]);
    EXPECT_EQ(dtor.className, "Matrix");

    // Regular member function
    EXPECT_EQ(pubSec.members[3]->kind, ASTKind::FunctionDecl);
}

TEST(Parser, ClassStaticMembers) {
    auto [ast, diag] = parse(R"(
namespace utils {
    public:
        class Singleton {
            public:
                static Singleton instance();
            private:
                static int count;
        }
}
)");
    ASSERT_FALSE(diag.hasErrors());

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& cls = static_cast<ClassDecl&>(*sec.members[0]);

    // Public section: static method
    auto& pubSec = static_cast<VisibilitySection&>(*cls.sections[0]);
    ASSERT_EQ(pubSec.members.size(), 1u);
    auto& staticMethod = static_cast<FunctionDecl&>(*pubSec.members[0]);
    EXPECT_TRUE(staticMethod.isStatic);
    EXPECT_EQ(staticMethod.name, "instance");

    // Private section: static member variable
    auto& privSec = static_cast<VisibilitySection&>(*cls.sections[1]);
    ASSERT_EQ(privSec.members.size(), 1u);
    auto& staticVar = static_cast<DataDecl&>(*privSec.members[0]);
    EXPECT_TRUE(staticVar.isStatic());
    EXPECT_EQ(staticVar.name, "count");
}

TEST(Parser, ClassRejectVirtual) {
    auto [ast, diag] = parse(R"(
namespace test {
    public:
        class Foo {
            public:
                virtual void bar();
        }
}
)");
    EXPECT_TRUE(diag.hasErrors());
}

TEST(Parser, ClassRejectFunctionBody) {
    auto [ast, diag] = parse(R"(
namespace test {
    public:
        class Foo {
            public:
                int bar() { return 42; }
        }
}
)");
    EXPECT_TRUE(diag.hasErrors());
}

TEST(Parser, ClassRejectMemberInitializer) {
    auto [ast, diag] = parse(R"(
namespace test {
    public:
        class Foo {
            private:
                int value = 42;
        }
}
)");
    EXPECT_TRUE(diag.hasErrors());
}

// --- Template declarations ---

TEST(Parser, FunctionTemplateTypeParam) {
    auto [ast, diag] = parse(R"(
namespace math {
    public:
        template<typename T>
        T normalize(T x, T y);
}
)");
    ASSERT_FALSE(diag.hasErrors());

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    ASSERT_EQ(sec.members.size(), 1u);
    EXPECT_EQ(sec.members[0]->kind, ASTKind::FunctionDecl);

    auto& fn = static_cast<FunctionDecl&>(*sec.members[0]);
    EXPECT_EQ(fn.name, "normalize");
    ASSERT_EQ(fn.templateParams.size(), 1u);
    EXPECT_EQ(fn.templateParams[0].kind, TemplateParamDecl::TypeParam);
    EXPECT_EQ(fn.templateParams[0].name, "T");
    EXPECT_EQ(fn.params.size(), 2u);
}

TEST(Parser, FunctionTemplateMultiParams) {
    auto [ast, diag] = parse(R"(
namespace math {
    public:
        template<typename T, typename U>
        void convert(T input, U output);
}
)");
    ASSERT_FALSE(diag.hasErrors());

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FunctionDecl&>(*sec.members[0]);
    ASSERT_EQ(fn.templateParams.size(), 2u);
    EXPECT_EQ(fn.templateParams[0].name, "T");
    EXPECT_EQ(fn.templateParams[1].name, "U");
}

TEST(Parser, FunctionTemplateNonTypeParam) {
    auto [ast, diag] = parse(R"(
namespace math {
    public:
        template<typename T, int N>
        void fill(T value);
}
)");
    ASSERT_FALSE(diag.hasErrors());

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FunctionDecl&>(*sec.members[0]);
    ASSERT_EQ(fn.templateParams.size(), 2u);
    EXPECT_EQ(fn.templateParams[0].kind, TemplateParamDecl::TypeParam);
    EXPECT_EQ(fn.templateParams[0].name, "T");
    EXPECT_EQ(fn.templateParams[1].kind, TemplateParamDecl::NonTypeParam);
    EXPECT_EQ(fn.templateParams[1].name, "N");
}

TEST(Parser, TemplateFnBlock) {
    auto [ast, diag] = parse(R"(
namespace math {
    public:
        template<typename T>
        T normalize(T x, T y);

        template<typename T>
        fn normalize {
            stage<1> compute();
        }
}
)");
    ASSERT_FALSE(diag.hasErrors());

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    ASSERT_EQ(sec.members.size(), 2u);

    auto& fnBlock = static_cast<FunctionLogicBlock&>(*sec.members[1]);
    ASSERT_EQ(fnBlock.templateParams.size(), 1u);
    EXPECT_EQ(fnBlock.templateParams[0].name, "T");
}

TEST(Parser, TypeNodeTemplateArgs) {
    auto [ast, diag] = parse(R"(
namespace container {
    public:
        template<typename T>
        void process(Vector<T> items);
}
)");
    ASSERT_FALSE(diag.hasErrors());

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FunctionDecl&>(*sec.members[0]);
    ASSERT_EQ(fn.params.size(), 1u);

    auto& paramType = fn.params[0].type;
    EXPECT_EQ(paramType.nameParts.size(), 1u);
    EXPECT_EQ(paramType.nameParts[0], "Vector");
    ASSERT_EQ(paramType.templateArgs.size(), 1u);
    EXPECT_EQ(paramType.templateArgs[0].nameParts[0], "T");
}

TEST(Parser, ClassTemplateDecl) {
    auto [ast, diag] = parse(R"(
namespace container {
    public:
        template<typename T>
        class Vector {
            public:
                void push_back(const T& value);
                T get(int index) const;
        }
}
)");
    ASSERT_FALSE(diag.hasErrors());

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    ASSERT_EQ(sec.members.size(), 1u);
    EXPECT_EQ(sec.members[0]->kind, ASTKind::ClassDecl);

    auto& cls = static_cast<ClassDecl&>(*sec.members[0]);
    EXPECT_EQ(cls.name, "Vector");
    EXPECT_TRUE(cls.isTemplate());
    ASSERT_EQ(cls.templateParams.size(), 1u);
    EXPECT_EQ(cls.templateParams[0].name, "T");
}

// --- Class template + CRTP ---

TEST(Parser, ClassTemplateMultiParam) {
    auto [ast, diag] = parse(R"(
namespace container {
    public:
        template<typename K, typename V>
        class HashMap {
            public:
                void insert(const K& key, const V& value);
                V get(const K& key) const;
        }
}
)");
    ASSERT_FALSE(diag.hasErrors());

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& cls = static_cast<ClassDecl&>(*sec.members[0]);
    EXPECT_EQ(cls.name, "HashMap");
    ASSERT_EQ(cls.templateParams.size(), 2u);
    EXPECT_EQ(cls.templateParams[0].name, "K");
    EXPECT_EQ(cls.templateParams[1].name, "V");
}

TEST(Parser, ClassTemplateNonType) {
    auto [ast, diag] = parse(R"(
namespace container {
    public:
        template<typename T, int N>
        class Array {
            public:
                T get(int index) const;
        }
}
)");
    ASSERT_FALSE(diag.hasErrors());

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& cls = static_cast<ClassDecl&>(*sec.members[0]);
    ASSERT_EQ(cls.templateParams.size(), 2u);
    EXPECT_EQ(cls.templateParams[0].kind, TemplateParamDecl::TypeParam);
    EXPECT_EQ(cls.templateParams[1].kind, TemplateParamDecl::NonTypeParam);
    EXPECT_EQ(cls.templateParams[1].name, "N");
}

TEST(Parser, CRTPPattern) {
    auto [ast, diag] = parse(R"(
namespace pattern {
    public:
        template<typename Derived>
        class Base {
            public:
                void interface();
        }
        class Impl : public Base<Impl> {
            public:
                void implementation();
        }
}
)");
    ASSERT_FALSE(diag.hasErrors());

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    ASSERT_EQ(sec.members.size(), 2u);

    // Base<Derived> is a class template
    auto& base = static_cast<ClassDecl&>(*sec.members[0]);
    EXPECT_EQ(base.name, "Base");
    EXPECT_TRUE(base.isTemplate());

    // Impl inherits from Base<Impl>
    auto& impl = static_cast<ClassDecl&>(*sec.members[1]);
    EXPECT_EQ(impl.name, "Impl");
    ASSERT_TRUE(impl.baseClass.has_value());
    EXPECT_EQ(impl.baseClass->nameParts[0], "Base");
    ASSERT_EQ(impl.baseClass->templateArgs.size(), 1u);
    EXPECT_EQ(impl.baseClass->templateArgs[0].nameParts[0], "Impl");
}

// --- Constraint, Adapt, Instantiate, Specialization ---

TEST(Parser, ConstraintDecl) {
    std::string source = R"(
namespace math {
    public:
        constraint Numeric {
            T add(T a, T b);
            T zero;
        }
}
)";
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    ASSERT_EQ(sec.members.size(), 1u);

    auto& constraint = static_cast<TypeDecl&>(*sec.members[0]);
    EXPECT_TRUE(constraint.isConstraint());
    EXPECT_EQ(constraint.name, "Numeric");
    EXPECT_FALSE(constraint.parentConstraint.has_value());
    ASSERT_EQ(constraint.constraintMembers.size(), 2u);

    // First member: function T add(T a, T b)
    EXPECT_TRUE(constraint.constraintMembers[0].isFunction);
    EXPECT_EQ(constraint.constraintMembers[0].name, "add");
    EXPECT_EQ(constraint.constraintMembers[0].params.size(), 2u);

    // Second member: variable T zero
    EXPECT_FALSE(constraint.constraintMembers[1].isFunction);
    EXPECT_EQ(constraint.constraintMembers[1].name, "zero");
}

TEST(Parser, ConstraintWithInheritance) {
    std::string source = R"(
namespace math {
    public:
        constraint Ordered : Comparable {
            Bool less_than(T a, T b);
        }
}
)";
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& constraint = static_cast<TypeDecl&>(*sec.members[0]);
    EXPECT_TRUE(constraint.isConstraint());
    EXPECT_EQ(constraint.name, "Ordered");
    ASSERT_TRUE(constraint.parentConstraint.has_value());
    EXPECT_EQ(*constraint.parentConstraint, "Comparable");
}

TEST(Parser, AdaptDecl) {
    std::string source = R"(
namespace math {
    public:
        adapt Numeric for Double {
            add = double_add;
            zero = double_zero;
        }
    using Double = std::cpp17::double;
}
)";
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& adapt = static_cast<TypeDecl&>(*sec.members[0]);
    EXPECT_TRUE(adapt.isAdapt());
    EXPECT_EQ(adapt.adaptConstraintName, "Numeric");
    EXPECT_EQ(adapt.adaptTargetType.nameParts[0], "Double");
    ASSERT_EQ(adapt.adaptMappings.size(), 2u);
    EXPECT_EQ(adapt.adaptMappings[0].memberName, "add");
    EXPECT_EQ(adapt.adaptMappings[0].targetName, "double_add");
    EXPECT_EQ(adapt.adaptMappings[1].memberName, "zero");
    EXPECT_EQ(adapt.adaptMappings[1].targetName, "double_zero");
}

TEST(Parser, InstantiateDecl) {
    std::string source = R"(
namespace container {
    public:
        instantiate Vector<Int>;
    using Int = std::cpp17::int32_t;
}
)";
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& inst = static_cast<InstantiateDecl&>(*sec.members[0]);
    EXPECT_EQ(inst.type.nameParts[0], "Vector");
    ASSERT_EQ(inst.type.templateArgs.size(), 1u);
    EXPECT_EQ(inst.type.templateArgs[0].nameParts[0], "Int");
}

TEST(Parser, FullSpecialization) {
    std::string source = R"(
namespace traits {
    public:
        template<>
        class Traits<Int> {
            public:
                Bool is_integral;
        }
}
)";
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& cls = static_cast<ClassDecl&>(*sec.members[0]);
    EXPECT_EQ(cls.name, "Traits");
    EXPECT_TRUE(cls.isSpecialization);
    EXPECT_FALSE(cls.isPartialSpecialization);
    ASSERT_EQ(cls.specializationArgs.size(), 1u);
    EXPECT_EQ(cls.specializationArgs[0].nameParts[0], "Int");
    EXPECT_TRUE(cls.templateParams.empty());
}

TEST(Parser, PartialSpecialization) {
    std::string source = R"(
namespace container {
    public:
        template<typename T>
        class Vector<T*> {
            public:
                void push_back(T value);
        }
}
)";
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& cls = static_cast<ClassDecl&>(*sec.members[0]);
    EXPECT_EQ(cls.name, "Vector");
    EXPECT_FALSE(cls.isSpecialization);
    EXPECT_TRUE(cls.isPartialSpecialization);
    ASSERT_EQ(cls.specializationArgs.size(), 1u);
    ASSERT_EQ(cls.templateParams.size(), 1u);
    EXPECT_EQ(cls.templateParams[0].name, "T");
}

TEST(Parser, ConstrainedTemplateParam) {
    std::string source = R"(
namespace math {
    public:
        template<typename T : Numeric>
        T normalize(T x, T y);
}
)";
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& func = static_cast<FunctionDecl&>(*sec.members[0]);
    ASSERT_EQ(func.templateParams.size(), 1u);
    EXPECT_EQ(func.templateParams[0].name, "T");
    EXPECT_EQ(func.templateParams[0].constraintType.nameParts[0], "Numeric");
}

// --- ComptimeIf, ComptimeFn, TypeFn, Template Template Params ---

TEST(Parser, ComptimeIf) {
    std::string source = R"(
namespace meta {
    public:
        comptime if (N <= 4) {
            void small_impl();
        } else {
            void large_impl();
        }
}
)";
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    ASSERT_EQ(sec.members.size(), 1u);

    auto& ci = static_cast<ComptimeIf&>(*sec.members[0]);
    EXPECT_EQ(ci.thenBody.size(), 1u);
    EXPECT_EQ(ci.elseBody.size(), 1u);
}

TEST(Parser, ComptimeFn) {
    std::string source = R"(
namespace meta {
    public:
        comptime fn factorial(Int N) -> Int { N }

    using Int = std::cpp17::int32_t;
}
)";
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    ASSERT_GE(sec.members.size(), 1u);

    auto& cf = static_cast<FnDecl&>(*sec.members[0]);
    EXPECT_EQ(cf.name, "factorial");
    EXPECT_EQ(cf.params.size(), 1u);
    EXPECT_NE(cf.comptimeBody, nullptr);
}

TEST(Parser, TypeFnDecl) {
    std::string source = R"(
namespace meta {
    public:
        typefn Wider(typename T) -> typename {
            match T {
                Float => Double,
                Int => Long,
            }
        }
}
)";
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    ASSERT_EQ(sec.members.size(), 1u);

    auto& tf = static_cast<TypeDecl&>(*sec.members[0]);
    EXPECT_TRUE(tf.isTypeFn());
    EXPECT_EQ(tf.name, "Wider");
    EXPECT_EQ(tf.matchTarget, "T");
    ASSERT_EQ(tf.templateParams.size(), 1u);
    EXPECT_EQ(tf.templateParams[0].name, "T");
    ASSERT_EQ(tf.matchArms.size(), 2u);
    EXPECT_EQ(tf.matchArms[0].pattern.nameParts[0], "Float");
    EXPECT_EQ(tf.matchArms[0].result.nameParts[0], "Double");
}

TEST(Parser, TemplateTemplateParam) {
    std::string source = R"(
namespace container {
    public:
        template<typename T, template<typename> class Container>
        void process(Container<T> data);
}
)";
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& func = static_cast<FunctionDecl&>(*sec.members[0]);
    ASSERT_EQ(func.templateParams.size(), 2u);

    EXPECT_EQ(func.templateParams[0].kind, TemplateParamDecl::TypeParam);
    EXPECT_EQ(func.templateParams[0].name, "T");

    EXPECT_EQ(func.templateParams[1].kind, TemplateParamDecl::TemplateTemplateParam);
    EXPECT_EQ(func.templateParams[1].name, "Container");
    EXPECT_EQ(func.templateParams[1].innerParams.size(), 1u);
}

// --- Variadic Templates ---

TEST(Parser, VariadicTemplateParam) {
    std::string source = R"(
namespace util {
    public:
        template<typename... Ts>
        void print_all();
}
)";
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& func = static_cast<FunctionDecl&>(*sec.members[0]);
    ASSERT_EQ(func.templateParams.size(), 1u);
    EXPECT_EQ(func.templateParams[0].name, "Ts");
    EXPECT_TRUE(func.templateParams[0].isVariadic);
}

TEST(Parser, VariadicPackExpansion) {
    // Type... in parameter list signals pack expansion
    std::string source = R"(
namespace util {
    public:
        template<typename... Ts>
        void apply(Ts... args);
}
)";
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    // This exercises pack expansion type parsing (Type...)
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& func = static_cast<FunctionDecl&>(*sec.members[0]);
    ASSERT_EQ(func.params.size(), 1u);
    EXPECT_EQ(func.params[0].type.nameParts[0], "Ts");
    EXPECT_TRUE(func.params[0].type.isVariadic);
}

// --- Priority sections ---

TEST(Parser, PrioritySectionBasic) {
    std::string source = R"(
namespace app {
    public:
        priority(critical):
            void init();
            void start();

        priority(low):
            void cleanup();

        void normal_func();
}
)";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    ASSERT_EQ(ns.sections.size(), 1u);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    ASSERT_EQ(sec.members.size(), 4u);

    // init: critical
    auto& init = static_cast<FunctionDecl&>(*sec.members[0]);
    EXPECT_EQ(init.name, "init");
    EXPECT_EQ(init.priority, PriorityLevel::Critical);

    // start: critical
    auto& start = static_cast<FunctionDecl&>(*sec.members[1]);
    EXPECT_EQ(start.name, "start");
    EXPECT_EQ(start.priority, PriorityLevel::Critical);

    // cleanup: low
    auto& cleanup = static_cast<FunctionDecl&>(*sec.members[2]);
    EXPECT_EQ(cleanup.name, "cleanup");
    EXPECT_EQ(cleanup.priority, PriorityLevel::Low);

    // normal_func: inherits Low from preceding priority(low): section
    auto& normal = static_cast<FunctionDecl&>(*sec.members[3]);
    EXPECT_EQ(normal.name, "normal_func");
    EXPECT_EQ(normal.priority, PriorityLevel::Low);
}

TEST(Parser, PrioritySectionAllLevels) {
    std::string source = R"(
namespace app {
    public:
        priority(critical):
            void a();
        priority(high):
            void b();
        priority(normal):
            void c();
        priority(low):
            void d();
        priority(background):
            void e();
}
)";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());

    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& sec = static_cast<VisibilitySection&>(*ns.sections[0]);
    ASSERT_EQ(sec.members.size(), 5u);

    EXPECT_EQ(static_cast<FunctionDecl&>(*sec.members[0]).priority, PriorityLevel::Critical);
    EXPECT_EQ(static_cast<FunctionDecl&>(*sec.members[1]).priority, PriorityLevel::High);
    EXPECT_EQ(static_cast<FunctionDecl&>(*sec.members[2]).priority, PriorityLevel::Normal);
    EXPECT_EQ(static_cast<FunctionDecl&>(*sec.members[3]).priority, PriorityLevel::Low);
    EXPECT_EQ(static_cast<FunctionDecl&>(*sec.members[4]).priority, PriorityLevel::Background);
}

TEST(Parser, PriorityInvalidLevel) {
    std::string source = R"(
namespace app {
    public:
        priority(urgent):
            void foo();
}
)";
    auto [ast, diag] = parse(source);
    EXPECT_TRUE(diag.hasErrors());
}

// --- Data-aware optimization hints ---

TEST(Parser, HintsRustStyleCardinalityRange) {
    std::string source = R"(
namespace app {
    public:
        process(Array items) -> void, cardinality(1k..100k);
}
)";
    auto [ast, diag] = parse(source);
    EXPECT_FALSE(diag.hasErrors());
    ASSERT_EQ(ast->declarations.size(), 1u);
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FnDecl&>(*section.members[0]);
    ASSERT_TRUE(fn.cardinality.has_value());
    EXPECT_EQ(fn.cardinality->min, 1000);
    EXPECT_EQ(fn.cardinality->max, 100000);
}

TEST(Parser, HintsRustStyleAccessPattern) {
    std::string source = R"(
namespace app {
    public:
        lookup(Map table) -> Result, access(random);
}
)";
    auto [ast, diag] = parse(source);
    EXPECT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FnDecl&>(*section.members[0]);
    EXPECT_EQ(fn.accessPattern, AccessPattern::Random);
}

TEST(Parser, HintsRustStyleVoidWithHints) {
    std::string source = R"(
namespace app {
    public:
        transform(Array items) -> cardinality(100k);
}
)";
    auto [ast, diag] = parse(source);
    EXPECT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FnDecl&>(*section.members[0]);
    ASSERT_TRUE(fn.cardinality.has_value());
    EXPECT_EQ(fn.cardinality->min, 100000);
    EXPECT_EQ(fn.cardinality->max, 100000);
    // void return
    EXPECT_TRUE(fn.returnType.nameParts.empty());
}

TEST(Parser, HintsCppStyleCardinality) {
    std::string source = R"(
namespace app {
    public:
        void compute(Array data) -> cardinality(1k..10k);
}
)";
    auto [ast, diag] = parse(source);
    EXPECT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FnDecl&>(*section.members[0]);
    ASSERT_TRUE(fn.cardinality.has_value());
    EXPECT_EQ(fn.cardinality->min, 1000);
    EXPECT_EQ(fn.cardinality->max, 10000);
}

TEST(Parser, HintsTiledAccessWithSize) {
    std::string source = R"(
namespace app {
    public:
        blocked(Matrix m) -> void, access(tiled, 64);
}
)";
    auto [ast, diag] = parse(source);
    EXPECT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FnDecl&>(*section.members[0]);
    EXPECT_EQ(fn.accessPattern, AccessPattern::Tiled);
    EXPECT_EQ(fn.tiledSize, 64);
}

TEST(Parser, HintsOpenEndedCardinality) {
    std::string source = R"(
namespace app {
    public:
        large_scale(Array items) -> void, cardinality(10k..);
}
)";
    auto [ast, diag] = parse(source);
    EXPECT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FnDecl&>(*section.members[0]);
    ASSERT_TRUE(fn.cardinality.has_value());
    EXPECT_EQ(fn.cardinality->min, 10000);
    EXPECT_EQ(fn.cardinality->max, -1); // open-ended
}

TEST(Parser, HintsBothCardinalityAndAccess) {
    std::string source = R"(
namespace app {
    public:
        void pipeline_op(Array data) -> cardinality(10k..1M), access(streaming);
}
)";
    auto [ast, diag] = parse(source);
    EXPECT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FnDecl&>(*section.members[0]);
    ASSERT_TRUE(fn.cardinality.has_value());
    EXPECT_EQ(fn.cardinality->min, 10000);
    EXPECT_EQ(fn.cardinality->max, 1000000);
    EXPECT_EQ(fn.accessPattern, AccessPattern::Streaming);
}

TEST(Parser, HintsUnknownAccessPatternError) {
    std::string source = R"(
namespace app {
    public:
        process(Array items) -> void, access(sequential);
}
)";
    auto [ast, diag] = parse(source);
    EXPECT_TRUE(diag.hasErrors());
}

TEST(Parser, HintsGatherScatterAccess) {
    std::string source = R"(
namespace app {
    public:
        void scatter(Array data) -> access(gather_scatter);
}
)";
    auto [ast, diag] = parse(source);
    EXPECT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FnDecl&>(*section.members[0]);
    EXPECT_EQ(fn.accessPattern, AccessPattern::GatherScatter);
}

// --- record<...> composite type parsing ---

TEST(Parser, RecordTypeNamedFields) {
    std::string source = R"(
namespace orders {
    public:
        record<id: i64, amount: f64> build();
}
)";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FnDecl&>(*section.members[0]);
    const TypeNode& rt = fn.returnType;
    EXPECT_EQ(rt.stdlibId, stdlib::TypeId::Record);
    ASSERT_EQ(rt.recordFields.size(), 2u);
    EXPECT_EQ(rt.recordFields[0].name, "id");
    EXPECT_EQ(rt.recordFields[0].type().stdlibId, stdlib::TypeId::I64);
    EXPECT_EQ(rt.recordFields[1].name, "amount");
    EXPECT_EQ(rt.recordFields[1].type().stdlibId, stdlib::TypeId::F64);
}

TEST(Parser, RecordTypeNestedComposite) {
    std::string source = R"(
namespace orders {
    public:
        void take(record<key: string, items: slice<i64>> in);
}
)";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FnDecl&>(*section.members[0]);
    ASSERT_EQ(fn.params.size(), 1u);
    const TypeNode& pt = fn.params[0].type;
    EXPECT_EQ(pt.stdlibId, stdlib::TypeId::Record);
    ASSERT_EQ(pt.recordFields.size(), 2u);
    EXPECT_EQ(pt.recordFields[1].name, "items");
    EXPECT_EQ(pt.recordFields[1].type().stdlibId, stdlib::TypeId::Slice);
    ASSERT_EQ(pt.recordFields[1].type().templateArgs.size(), 1u);
    EXPECT_EQ(pt.recordFields[1].type().templateArgs[0].stdlibId, stdlib::TypeId::I64);
}

TEST(Parser, UnionTypeTaggedFields) {
    std::string source = R"(
namespace events {
    public:
        union<kind: u8, count: i64, ratio: f64> decode(string raw);
}
)";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FnDecl&>(*section.members[0]);
    const TypeNode& rt = fn.returnType;
    // union shares record's named-field AST vector; the tag is just the
    // first field (a Sema concern, not a distinct parse shape).
    EXPECT_EQ(rt.stdlibId, stdlib::TypeId::Union);
    ASSERT_EQ(rt.recordFields.size(), 3u);
    EXPECT_EQ(rt.recordFields[0].name, "kind");
    EXPECT_EQ(rt.recordFields[0].type().stdlibId, stdlib::TypeId::U8);
    EXPECT_EQ(rt.recordFields[2].name, "ratio");
    EXPECT_EQ(rt.recordFields[2].type().stdlibId, stdlib::TypeId::F64);
}

TEST(Parser, UnionTypeNestedComposite) {
    std::string source = R"(
namespace events {
    public:
        void take(union<tag: u8, items: slice<i64>,
                         order: record<id: i64, amount: f64>> ev);
}
)";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FnDecl&>(*section.members[0]);
    ASSERT_EQ(fn.params.size(), 1u);
    const TypeNode& pt = fn.params[0].type;
    EXPECT_EQ(pt.stdlibId, stdlib::TypeId::Union);
    ASSERT_EQ(pt.recordFields.size(), 3u);
    EXPECT_EQ(pt.recordFields[1].type().stdlibId, stdlib::TypeId::Slice);
    EXPECT_EQ(pt.recordFields[2].type().stdlibId, stdlib::TypeId::Record);
    ASSERT_EQ(pt.recordFields[2].type().recordFields.size(), 2u);
}

TEST(Parser, TimeNsScalarStdlibType) {
    std::string source = R"(
namespace clock {
    public:
        time_ns now();
        bool isFuture(time_ns t);
}
)";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& now = static_cast<FnDecl&>(*section.members[0]);
    // Scalar shape: recognized as its own TypeId, no template args.
    EXPECT_EQ(now.returnType.stdlibId, stdlib::TypeId::TimeNs);
    EXPECT_TRUE(now.returnType.templateArgs.empty());
    auto& isFuture = static_cast<FnDecl&>(*section.members[1]);
    ASSERT_EQ(isFuture.params.size(), 1u);
    EXPECT_EQ(isFuture.params[0].type.stdlibId, stdlib::TypeId::TimeNs);
}

TEST(Parser, UuidScalarStdlibType) {
    std::string source = R"(
namespace ids {
    public:
        uuid mint();
        bool same(uuid a);
}
)";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& mint = static_cast<FnDecl&>(*section.members[0]);
    EXPECT_EQ(mint.returnType.stdlibId, stdlib::TypeId::Uuid);
    EXPECT_TRUE(mint.returnType.templateArgs.empty());
    auto& same = static_cast<FnDecl&>(*section.members[1]);
    ASSERT_EQ(same.params.size(), 1u);
    EXPECT_EQ(same.params[0].type.stdlibId, stdlib::TypeId::Uuid);
}

TEST(Parser, Decimal128ScalarStdlibType) {
    std::string source = R"(
namespace money {
    public:
        decimal128 zero();
        bool gt(decimal128 a);
}
)";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& zero = static_cast<FnDecl&>(*section.members[0]);
    EXPECT_EQ(zero.returnType.stdlibId, stdlib::TypeId::Decimal128);
    EXPECT_TRUE(zero.returnType.templateArgs.empty());
    auto& gt = static_cast<FnDecl&>(*section.members[1]);
    ASSERT_EQ(gt.params.size(), 1u);
    EXPECT_EQ(gt.params[0].type.stdlibId, stdlib::TypeId::Decimal128);
}

TEST(Parser, BytesAndArrayStdlibTypes) {
    std::string source = R"(
namespace buf {
    public:
        bytes raw();
        void store(array<i64, 4> in);
        array<record<a: i64>, 2> grid();
}
)";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);

    // bytes — scalar-shaped stdlib type, no template args.
    auto& f0 = static_cast<FnDecl&>(*section.members[0]);
    EXPECT_EQ(f0.returnType.stdlibId, stdlib::TypeId::Bytes);
    EXPECT_TRUE(f0.returnType.templateArgs.empty());

    // array<i64, 4> — element type + integer N via nonTypeValue.
    auto& f1 = static_cast<FnDecl&>(*section.members[1]);
    const TypeNode& at = f1.params[0].type;
    EXPECT_EQ(at.stdlibId, stdlib::TypeId::Array);
    ASSERT_EQ(at.templateArgs.size(), 2u);
    EXPECT_EQ(at.templateArgs[0].stdlibId, stdlib::TypeId::I64);
    ASSERT_TRUE(at.templateArgs[1].nonTypeValue.has_value());
    EXPECT_EQ(*at.templateArgs[1].nonTypeValue, 4);

    // array<record<a: i64>, 2> — nested composite element.
    auto& f2 = static_cast<FnDecl&>(*section.members[2]);
    const TypeNode& gt = f2.returnType;
    EXPECT_EQ(gt.stdlibId, stdlib::TypeId::Array);
    ASSERT_EQ(gt.templateArgs.size(), 2u);
    EXPECT_EQ(gt.templateArgs[0].stdlibId, stdlib::TypeId::Record);
    ASSERT_EQ(gt.templateArgs[0].recordFields.size(), 1u);
    EXPECT_EQ(gt.templateArgs[0].recordFields[0].type().stdlibId, stdlib::TypeId::I64);
    ASSERT_TRUE(gt.templateArgs[1].nonTypeValue.has_value());
    EXPECT_EQ(*gt.templateArgs[1].nonTypeValue, 2);
}

// --- handler / flow declaration vocabulary ---

TEST(Parser, HandlerDeclDesugarsToFnDecl) {
    std::string source = R"(
namespace orders {
    public:
        handler parse(string raw) -> record<id: i64, amount: f64>;
}
)";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FnDecl&>(*section.members[0]);
    // Desugars to an ordinary FnDecl so every checker sees a function.
    EXPECT_EQ(fn.kind, ASTKind::FnDecl);
    // Carries the surface marker so it round-trips back to `handler`.
    EXPECT_TRUE(fn.hasModifier(ModifierData::Handler));
    EXPECT_EQ(fn.name, "parse");
    ASSERT_EQ(fn.params.size(), 1u);
    EXPECT_EQ(fn.params[0].type.stdlibId, stdlib::TypeId::String);
    EXPECT_EQ(fn.returnType.stdlibId, stdlib::TypeId::Record);
}

TEST(Parser, HandlerSourceZeroInputIsValid) {
    std::string source = R"(
namespace feed {
    public:
        handler tick() -> i64;
}
)";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    auto& fn = static_cast<FnDecl&>(*section.members[0]);
    EXPECT_TRUE(fn.hasModifier(ModifierData::Handler));
    EXPECT_TRUE(fn.params.empty());
    EXPECT_EQ(fn.returnType.stdlibId, stdlib::TypeId::I64);
}

TEST(Parser, HandlerMultiInputRejected) {
    std::string source = R"(
namespace bad {
    public:
        handler combine(i64 a, f64 b) -> bool;
}
)";
    auto [ast, diag] = parse(source);
    EXPECT_TRUE(diag.hasErrors());
}

TEST(Parser, FlowDesugarsToPipelineLogicBlock) {
    std::string source = R"(
namespace orders {
    public:
        handler parse(string raw) -> i64;
        handler persist(i64 o) -> bool;

        flow pipeline {
          parse -> persist;
          persist -> void;
        }
}
)";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
    auto& ns = static_cast<NamespaceDecl&>(*ast->declarations[0]);
    auto& section = static_cast<VisibilitySection&>(*ns.sections[0]);
    // members: parse, persist, flow-block
    auto& block = static_cast<FnLogicBlock&>(*section.members[2]);
    EXPECT_EQ(block.kind, ASTKind::FnLogicBlock);
    EXPECT_TRUE(block.hasModifier(ModifierData::Flow));
    EXPECT_TRUE(block.isPipeline());
    ASSERT_EQ(block.pipelineEdges.size(), 2u);
    EXPECT_EQ(block.pipelineEdges[0].source, "parse");
    EXPECT_EQ(block.pipelineEdges[0].target, "persist");
    EXPECT_TRUE(block.pipelineEdges[1].isTerminal);
}

TEST(Parser, FlowRejectsExplicitStage) {
    std::string source = R"(
namespace bad {
    public:
        handler a(i64 x) -> i64;
        flow f {
          stage<1> a();
          a -> a;
        }
}
)";
    auto [ast, diag] = parse(source);
    EXPECT_TRUE(diag.hasErrors());
}

// The plan's hard design constraint: handler/flow must survive a
// parse → AST-print round-trip as handler/flow, never re-rendered as a
// plain function / anonymous pipeline.
TEST(Parser, HandlerFlowRoundTripPreservesSurfaceForm) {
    std::string source = R"(
namespace orders {
    public:
        handler parse(string raw) -> i64;
        handler persist(i64 o) -> bool;

        flow pipeline {
          parse -> persist;
          persist -> void;
        }
}
)";
    auto [ast, diag] = parse(source);
    ASSERT_FALSE(diag.hasErrors());
    std::ostringstream os;
    ASTPrinter printer(os);
    printer.print(*ast);
    const std::string dump = os.str();
    EXPECT_NE(dump.find("HandlerDecl"), std::string::npos);
    EXPECT_NE(dump.find("FlowBlock 'pipeline'"), std::string::npos);
    // Must NOT collapse to the generic forms.
    EXPECT_EQ(dump.find("FunctionDecl 'i64 parse"), std::string::npos);
    EXPECT_EQ(dump.find("PipelineBlock 'pipeline'"), std::string::npos);
}
