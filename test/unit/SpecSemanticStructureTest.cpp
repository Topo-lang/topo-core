// SpecSemanticStructureTest — Pilot for structural assertions on spec fixtures.
//
// Motivation:
// The spec fixture driver (topo-core/test/language/CMakeLists.txt) invokes
// `topo --check` which only verifies that semantic analysis succeeds. It never
// inspects the SymbolTable that sema produces, so structural regressions
// (wrong visibility, missing inheritance link, unpopulated stage metadata) can
// slip through even though every spec test "passes".
//
// This pilot proves that the semantic state is reachable from a GTest harness
// by attaching explicit SymbolTable assertions to three representative spec
// fixtures. Full retrofit of assertions for all 127 fixtures is deliberately
// out of scope — see follow-up in the issue's Resolution section.

#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Sema/SemanticAnalyzer.h"
#include "topo/Sema/SymbolTable.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

using namespace topo;

namespace {

// Path to topo-core/test/language/spec — injected by CMake.
// Falls back to a relative path for IDE-only invocation convenience.
#ifndef TOPO_SPEC_FIXTURES_DIR
#define TOPO_SPEC_FIXTURES_DIR "topo-core/test/language/spec"
#endif

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Single-file analysis helper — spec fixtures are standalone files with no
// cross-file imports, so we avoid the ImportResolver path used by topo --check.
SymbolTable analyzeFixture(const std::string& relPath) {
    std::string full = std::string(TOPO_SPEC_FIXTURES_DIR) + "/" + relPath;
    std::string source = readFile(full);
    EXPECT_FALSE(source.empty()) << "fixture missing: " << full;

    DiagnosticEngine diag;
    Lexer lexer(source, full, diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    EXPECT_FALSE(diag.hasErrors()) << "parse failed for " << full;

    SemanticAnalyzer sema(diag);
    auto syms = sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_FALSE(diag.hasErrors()) << "sema failed for " << full;
    return syms;
}

} // namespace

// ============================================================================
// Fixture 1: declarations/valid/minimal_function.topo — declaration-heavy
// Verifies: function count, qualified name, visibility, return type.
// ============================================================================
TEST(SpecSemanticStructure, MinimalFunctionSymbolShape) {
    auto syms = analyzeFixture("declarations/valid/minimal_function.topo");

    EXPECT_EQ(syms.functions().size(), 1u)
        << "expected exactly one function symbol in minimal_function.topo";

    const FunctionSymbol* fn = syms.findFunction("app::run");
    ASSERT_NE(fn, nullptr) << "symbol 'app::run' must be present";

    EXPECT_EQ(fn->simpleName, "run");
    EXPECT_EQ(fn->visibility, Visibility::Public);
    EXPECT_TRUE(fn->params.empty()) << "run() declared with zero parameters";
}

// ============================================================================
// Fixture 2: stages/valid/monotonic_stages.topo — stage/logic-block metadata
// Verifies: logic block exists, stage vector populated and monotonic, called
// functions are recorded in parallel arrays.
// ============================================================================
TEST(SpecSemanticStructure, MonotonicStagesLogicBlockShape) {
    auto syms = analyzeFixture("stages/valid/monotonic_stages.topo");

    const LogicBlockEntry* block = syms.findLogicBlock("proc::run");
    ASSERT_NE(block, nullptr) << "logic block 'proc::run' must be recorded";

    ASSERT_EQ(block->stages.size(), 3u)
        << "three operations → three stage entries";
    EXPECT_EQ(block->stages[0], 1);
    EXPECT_EQ(block->stages[1], 2);
    EXPECT_EQ(block->stages[2], 3);

    ASSERT_EQ(block->calledFunctions.size(), 3u)
        << "calledFunctions must be populated in parallel with stages[]";
    // Operation-referenced names are captured (may be qualified or simple
    // depending on resolution path); the set must match declared protected fns.
    bool hasInit = false, hasCompute = false, hasFinalize = false;
    for (const auto& name : block->calledFunctions) {
        if (name == "init" || name == "proc::init") hasInit = true;
        if (name == "compute" || name == "proc::compute") hasCompute = true;
        if (name == "finalize" || name == "proc::finalize") hasFinalize = true;
    }
    EXPECT_TRUE(hasInit && hasCompute && hasFinalize)
        << "each stage operation must appear in calledFunctions[]";
}

// ============================================================================
// Fixture 3: classes/valid/inheritance.topo — inheritance link in SymbolTable
// Verifies: both classes recorded, Circle's baseClass references Shape.
// ============================================================================
TEST(SpecSemanticStructure, InheritanceBaseClassLink) {
    auto syms = analyzeFixture("classes/valid/inheritance.topo");

    const ClassSymbol* shape = syms.findClassSymbol("shapes::Shape");
    ASSERT_NE(shape, nullptr) << "class 'shapes::Shape' must be recorded";
    EXPECT_FALSE(shape->baseClass.has_value())
        << "Shape has no base class";

    const ClassSymbol* circle = syms.findClassSymbol("shapes::Circle");
    ASSERT_NE(circle, nullptr) << "class 'shapes::Circle' must be recorded";
    ASSERT_TRUE(circle->baseClass.has_value())
        << "Circle declares ': public Shape' — baseClass must be populated";

    // The baseClass TypeNode captures the parent type; TypeNode stores parts
    // in nameParts[]. We assert "Shape" appears as a terminal component — the
    // exact qualifier form depends on TypeResolver, so we only require the
    // base name match.
    const auto& parts = circle->baseClass->nameParts;
    ASSERT_FALSE(parts.empty()) << "baseClass nameParts must not be empty";
    EXPECT_EQ(parts.back(), "Shape")
        << "base class terminal name should be 'Shape', got: "
        << circle->baseClass->toString();
}

// ============================================================================
// Fixture 4: debug/invalid/render_unimplemented.topo — explicit-reject path.
// The `render` block parses but web-rendering semantics are NOT implemented.
// Per no-silent-degradation, SemanticAnalyzer must reject it with an explicit
// "render block: rendering not implemented" diagnostic instead of silently
// passing it through. Drives Parser+Sema directly (the shared analyzeFixture
// helper asserts no errors, which would not hold for an intentionally
// rejected fixture).
// ============================================================================
TEST(SpecSemanticStructure, RenderBlockExplicitlyRejected) {
    std::string full = std::string(TOPO_SPEC_FIXTURES_DIR) +
                       "/debug/invalid/render_unimplemented.topo";
    std::string source = readFile(full);
    ASSERT_FALSE(source.empty()) << "fixture missing: " << full;

    DiagnosticEngine diag;
    Lexer lexer(source, full, diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    ASSERT_FALSE(diag.hasErrors())
        << "render syntax must still PARSE cleanly — the rejection is a "
           "semantic decision, not a parse error";

    SemanticAnalyzer sema(diag);
    auto syms = sema.analyze(static_cast<const TopoFile&>(*ast));

    ASSERT_TRUE(diag.hasErrors())
        << "a debug declaration with a render block must be rejected";

    bool found = false;
    for (const auto& d : diag.diagnostics()) {
        if (d.level == DiagLevel::Error &&
            d.message.find("render block: rendering not implemented") !=
                std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "expected an explicit 'render block: rendering not implemented' "
           "error — accepting syntax while producing no behavior is a silent "
           "partial feature";

    // The captured render payload must NOT survive into the SymbolTable —
    // no downstream consumer may mistake it for an active feature.
    bool anyRenderRecorded = false;
    for (const auto& e : syms.debugEntries()) {
        if (!e.renderDecls.empty()) anyRenderRecorded = true;
    }
    EXPECT_FALSE(anyRenderRecorded)
        << "rejected render decls must be dropped, not recorded";
}
