#include "topo/AST/ASTNode.h"
#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Sema/SemanticAnalyzer.h"
#include <gtest/gtest.h>
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

// --- Safety: class without visibility triggers error, terminates quickly ---

TEST(ParserSafety, ClassWithoutVisibility) {
    auto [ast, diag] = parse("namespace x { class C { public: void f(); } }");
    // 'class' parses but "public:" without colon-after or missing colon triggers error
    // Key: must terminate quickly, not spin
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_LE(diag.errorCount(), 100);
}

// --- Safety: many bad tokens should not produce unbounded errors ---

TEST(ParserSafety, ManyErrors) {
    std::string input;
    for (int i = 0; i < 200; ++i) {
        input += "badtoken; ";
    }
    auto [ast, diag] = parse(input);
    EXPECT_TRUE(diag.hasErrors());
    // DiagnosticEngine should cap at kMaxErrors (100) + 1 limit message
    EXPECT_LE(diag.errorCount(), DiagnosticEngine::kMaxErrors + 1);
}

// --- Safety: deep binary expression chain should not stack overflow ---

TEST(ParserSafety, DeepNestedExpression) {
    // Build: namespace x { public: fn compute { stage<1> result = 1+1+1+...+1; } }
    std::string expr;
    for (int i = 0; i < 500; ++i) {
        if (i > 0) expr += "+";
        expr += "1";
    }
    std::string input = "namespace x { public: fn compute { stage<1> result = " + expr + "; } }";
    auto [ast, diag] = parse(input);
    // Should either parse successfully or hit recursion limit, but not crash
    EXPECT_NE(ast, nullptr);
}

// --- Safety: unclosed braces should produce error and return AST ---

TEST(ParserSafety, UnclosedBraces) {
    auto [ast, diag] = parse("namespace x { public: void f(");
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_NE(ast, nullptr);
}

// --- Safety: empty file is valid ---

TEST(ParserSafety, EmptyFile) {
    auto [ast, diag] = parse("");
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(diag.errorCount(), 0);
}

// --- Safety: missing colon after visibility keyword ---

TEST(ParserSafety, MalformedVisibility) {
    auto [ast, diag] = parse("namespace x { public void f(); }");
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_NE(ast, nullptr);
}

// --- Safety: missing class name ---

TEST(ParserSafety, MalformedClass) {
    auto [ast, diag] = parse("namespace x { public: class { } }");
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_NE(ast, nullptr);
}

// --- Safety: bad token inside class body ---

TEST(ParserSafety, NestedMalformedClass) {
    auto [ast, diag] = parse("namespace x { public: class A { badtoken } }");
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_NE(ast, nullptr);
}

// --- Safety: pipeline with many edges should not OOM ---

TEST(ParserSafety, PipelineManyEdges) {
    std::string input = "namespace x { public: fn process {\n";
    for (int i = 0; i < 999; ++i) {
        input += "  step" + std::to_string(i) + " -> step" + std::to_string(i + 1) + ";\n";
    }
    input += "  step999 -> void;\n";
    input += "} }";
    auto [ast, diag] = parse(input);
    // Should parse without OOM. May have errors for undeclared functions but that's sema.
    EXPECT_NE(ast, nullptr);
}

// --- Safety: circular ownership chain detected by sema, not crash ---

TEST(ParserSafety, CircularOwnership) {
    // Build 100 classes that form a shared ownership chain: A->B->C->...->A
    std::string input = "namespace x {\n  public:\n";
    for (int i = 0; i < 100; ++i) {
        std::string className = "C" + std::to_string(i);
        std::string nextClass = "C" + std::to_string((i + 1) % 100);
        input += "  type " + className + " {\n";
        input += "    public:\n";
        input += "      shared " + nextClass + " next;\n";
        input += "  }\n";
    }
    input += "}";

    auto [ast, diag] = parse(input);
    ASSERT_NE(ast, nullptr);

    // Run semantic analysis to trigger ownership cycle detection
    DiagnosticEngine semaDiag;
    SemanticAnalyzer sema(semaDiag);
    auto symbols = sema.analyze(*ast);

    // Should detect cycles, not OOM
    EXPECT_TRUE(semaDiag.hasErrors());
}

// --- Safety: error count stays bounded ---

TEST(ParserSafety, ErrorLimitReached) {
    DiagnosticEngine diag;
    EXPECT_FALSE(diag.reachedLimit());

    SourceLocation loc;
    for (int i = 0; i < 150; ++i) {
        diag.error(loc, "error " + std::to_string(i));
    }
    EXPECT_TRUE(diag.reachedLimit());
    // Should cap at kMaxErrors; errors beyond the limit are dropped
    EXPECT_EQ(diag.errorCount(), DiagnosticEngine::kMaxErrors);
    // Diagnostics vector: kMaxErrors errors + 1 "limit reached" message
    int errorDiags = 0;
    for (const auto& d : diag.diagnostics()) {
        if (d.level == DiagLevel::Error) ++errorDiags;
    }
    EXPECT_EQ(errorDiags, DiagnosticEngine::kMaxErrors + 1);
}

// --- Safety: deeply nested parenthesized expression ---

TEST(ParserSafety, RecursionDepthLimit) {
    // Build deeply nested expression: (((((...(1)...))))
    std::string input = "namespace x { public: fn compute { stage<1> result = ";
    for (int i = 0; i < 300; ++i) {
        input += "(";
    }
    input += "1";
    for (int i = 0; i < 300; ++i) {
        input += ")";
    }
    input += "; } }";

    auto [ast, diag] = parse(input);
    // Should hit recursion depth limit or parse, but not stack overflow
    EXPECT_NE(ast, nullptr);
}
