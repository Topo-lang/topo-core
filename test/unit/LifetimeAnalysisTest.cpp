#include <gtest/gtest.h>

#include "topo/Analysis/LifetimeAnalysis.h"
#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Sema/SemanticAnalyzer.h"

using namespace topo;

static SymbolTable parseAndAnalyze(const std::string& source) {
    DiagnosticEngine diag;
    Lexer lexer(source, "test.topo", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    EXPECT_FALSE(diag.hasErrors()) << "Parse errors in test input";
    SemanticAnalyzer sema(diag);
    return sema.analyze(*ast);
}

TEST(LifetimeAnalysis, BasicRangeResolvesToFunctions) {
    auto symbols = parseAndAnalyze(R"(
using int = std::cpp17::int;
namespace engine {
    lifetime frame = create..render;
    public:
        void run();
        fn run {
            stage<1> create();
            stage<2> compute();
            stage<3> render();
        }
        lifetime<frame, int> create(int n);
        int compute(int x);
        void render();
}
)");

    auto result = analysis::analyzeLifetimes(symbols);
    ASSERT_TRUE(result.scopes.count("frame"));
    const auto& scope = result.scopes.at("frame");
    EXPECT_TRUE(scope.coveredFunctions.count("engine::create"));
    EXPECT_TRUE(scope.coveredFunctions.count("engine::compute"));
    EXPECT_TRUE(scope.coveredFunctions.count("engine::render"));
}

TEST(LifetimeAnalysis, SingleFuncScope) {
    auto symbols = parseAndAnalyze(R"(
using int = std::cpp17::int;
namespace engine {
    lifetime temp = compute;
    public:
        void run();
        fn run {
            stage<1> init();
            stage<2> compute();
            stage<3> finish();
        }
        void init();
        lifetime<temp, int> compute(int x);
        void finish();
}
)");

    auto result = analysis::analyzeLifetimes(symbols);
    ASSERT_TRUE(result.scopes.count("temp"));
    const auto& scope = result.scopes.at("temp");
    EXPECT_TRUE(scope.coveredFunctions.count("engine::compute"));
    EXPECT_FALSE(scope.coveredFunctions.count("engine::init"));
    EXPECT_FALSE(scope.coveredFunctions.count("engine::finish"));
}

TEST(LifetimeAnalysis, AnnotatedFunctionsDetected) {
    auto symbols = parseAndAnalyze(R"(
using int = std::cpp17::int;
namespace engine {
    lifetime frame = create..render;
    public:
        void run();
        fn run {
            stage<1> create();
            stage<2> render();
        }
        lifetime<frame, int> create(int n);
        void render();
}
)");

    auto result = analysis::analyzeLifetimes(symbols);
    EXPECT_TRUE(result.lifetimeAnnotatedFunctions.count("engine::create"));
    EXPECT_FALSE(result.lifetimeAnnotatedFunctions.count("engine::render"));
}

// --- Scope-interaction edge cases ------------------------------------------
//
// The four TESTs below construct a SymbolTable directly so we can cover
// scope-interaction edge cases without depending on additional surface
// syntax. Analyzer contract (from LifetimeAnalysis.cpp): resolves each
// lifetime group to the set of functions in the inclusive stage range
// [startStage, endStage] within the block containing startFunc.

namespace {

static LogicBlockEntry makeBlock(const std::string& qn, std::vector<std::string> calls, std::vector<int> stages) {
    LogicBlockEntry block;
    block.qualifiedName = qn;
    auto pos = qn.rfind("::");
    block.simpleName = (pos == std::string::npos) ? qn : qn.substr(pos + 2);
    block.calledFunctions = std::move(calls);
    block.stages = std::move(stages);
    return block;
}

static LifetimeGroupEntry makeGroup(const std::string& name,
                                    const std::string& startFunc,
                                    const std::string& endFunc,
                                    bool isOpenEnded = false,
                                    bool isSingleFunc = false) {
    LifetimeGroupEntry g;
    g.name = name;
    g.startFunc = startFunc;
    g.endFunc = endFunc;
    g.isOpenEnded = isOpenEnded;
    g.isSingleFunc = isSingleFunc;
    return g;
}

} // namespace

TEST(LifetimeAnalysis, OverlappingScopesShareOverlapFunctions) {
    // Block run: stage 0 setup, 1 alpha, 2 beta, 3 gamma, 4 finalize
    // Scope outer  = alpha..finalize  (stages 1..4)
    // Scope inner  = beta..gamma      (stages 2..3)  — fully overlaps outer
    SymbolTable symbols;
    symbols.addLogicBlock(makeBlock("engine::run", {"setup", "alpha", "beta", "gamma", "finalize"}, {0, 1, 2, 3, 4}));
    symbols.addLifetimeGroup(makeGroup("outer", "alpha", "finalize"));
    symbols.addLifetimeGroup(makeGroup("inner", "beta", "gamma"));

    auto result = analysis::analyzeLifetimes(symbols);

    ASSERT_TRUE(result.scopes.count("outer"));
    const auto& outer = result.scopes.at("outer");
    EXPECT_TRUE(outer.coveredFunctions.count("engine::alpha"));
    EXPECT_TRUE(outer.coveredFunctions.count("engine::beta"));
    EXPECT_TRUE(outer.coveredFunctions.count("engine::gamma"));
    EXPECT_TRUE(outer.coveredFunctions.count("engine::finalize"));
    EXPECT_FALSE(outer.coveredFunctions.count("engine::setup"));

    ASSERT_TRUE(result.scopes.count("inner"));
    const auto& inner = result.scopes.at("inner");
    EXPECT_TRUE(inner.coveredFunctions.count("engine::beta"));
    EXPECT_TRUE(inner.coveredFunctions.count("engine::gamma"));
    // Overlap: beta/gamma are in both scopes
    EXPECT_EQ(inner.coveredFunctions.count("engine::beta"), outer.coveredFunctions.count("engine::beta"));
}

TEST(LifetimeAnalysis, NestedScopesFullyContainChildScope) {
    // outer contains inner entirely: outer = a..d (stages 0..3), inner = b..c (stages 1..2)
    SymbolTable symbols;
    symbols.addLogicBlock(makeBlock("engine::run", {"a", "b", "c", "d"}, {0, 1, 2, 3}));
    symbols.addLifetimeGroup(makeGroup("outer", "a", "d"));
    symbols.addLifetimeGroup(makeGroup("inner", "b", "c"));

    auto result = analysis::analyzeLifetimes(symbols);

    ASSERT_TRUE(result.scopes.count("outer"));
    ASSERT_TRUE(result.scopes.count("inner"));
    const auto& outer = result.scopes.at("outer");
    const auto& inner = result.scopes.at("inner");

    // outer is superset of inner
    for (const auto& fn : inner.coveredFunctions) {
        EXPECT_TRUE(outer.coveredFunctions.count(fn)) << "outer missing nested function: " << fn;
    }
    // outer has strictly more functions than inner
    EXPECT_GT(outer.coveredFunctions.size(), inner.coveredFunctions.size());
    EXPECT_TRUE(outer.coveredFunctions.count("engine::a"));
    EXPECT_TRUE(outer.coveredFunctions.count("engine::d"));
    EXPECT_FALSE(inner.coveredFunctions.count("engine::a"));
    EXPECT_FALSE(inner.coveredFunctions.count("engine::d"));
}

TEST(LifetimeAnalysis, ScopeCoversAllFunctionsInRangeIncludingSameStage) {
    // Multiple functions share the same stage: scope must include all of them.
    // stages: 0=init, 1=parseA, 1=parseB, 2=emit
    SymbolTable symbols;
    symbols.addLogicBlock(makeBlock("engine::run", {"init", "parseA", "parseB", "emit"}, {0, 1, 1, 2}));
    symbols.addLifetimeGroup(makeGroup("parse", "parseA", "parseB"));

    auto result = analysis::analyzeLifetimes(symbols);
    ASSERT_TRUE(result.scopes.count("parse"));
    const auto& scope = result.scopes.at("parse");

    // Both parseA and parseB live at stage 1; parse scope must include both.
    EXPECT_TRUE(scope.coveredFunctions.count("engine::parseA"));
    EXPECT_TRUE(scope.coveredFunctions.count("engine::parseB"));
    EXPECT_FALSE(scope.coveredFunctions.count("engine::init"));
    EXPECT_FALSE(scope.coveredFunctions.count("engine::emit"));
}

TEST(LifetimeAnalysis, OpenEndedScopeExtendsToLastStage) {
    // Open-ended scope from stage 1 should cover everything from stage 1 to end.
    SymbolTable symbols;
    symbols.addLogicBlock(makeBlock("engine::run", {"prelude", "enter", "work", "exit"}, {0, 1, 2, 3}));
    symbols.addLifetimeGroup(makeGroup("session", "enter", "", /*isOpenEnded=*/true));

    auto result = analysis::analyzeLifetimes(symbols);
    ASSERT_TRUE(result.scopes.count("session"));
    const auto& scope = result.scopes.at("session");

    EXPECT_TRUE(scope.coveredFunctions.count("engine::enter"));
    EXPECT_TRUE(scope.coveredFunctions.count("engine::work"));
    EXPECT_TRUE(scope.coveredFunctions.count("engine::exit"));
    EXPECT_FALSE(scope.coveredFunctions.count("engine::prelude"));
}
