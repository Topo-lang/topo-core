#include "topo/Analysis/PriorityAnalysis.h"
#include "topo/Sema/SymbolTable.h"
#include <gtest/gtest.h>

using namespace topo;
using namespace topo::analysis;

// Helper: build a SymbolTable with functions and logic blocks for testing.
static SymbolTable buildTestSymbols() {
    SymbolTable symbols;

    // Functions with explicit priorities
    FunctionSymbol critical_fn;
    critical_fn.qualifiedName = "app::init";
    critical_fn.simpleName = "init";
    critical_fn.visibility = Visibility::Public;
    critical_fn.priority = PriorityLevel::Critical;
    symbols.addFunction(critical_fn);

    FunctionSymbol normal_fn;
    normal_fn.qualifiedName = "app::process";
    normal_fn.simpleName = "process";
    normal_fn.visibility = Visibility::Public;
    normal_fn.priority = PriorityLevel::Normal;
    symbols.addFunction(normal_fn);

    FunctionSymbol helper_fn;
    helper_fn.qualifiedName = "app::helper";
    helper_fn.simpleName = "helper";
    helper_fn.visibility = Visibility::Private;
    helper_fn.priority = PriorityLevel::Normal;
    symbols.addFunction(helper_fn);

    FunctionSymbol low_fn;
    low_fn.qualifiedName = "app::cleanup";
    low_fn.simpleName = "cleanup";
    low_fn.visibility = Visibility::Public;
    low_fn.priority = PriorityLevel::Low;
    symbols.addFunction(low_fn);

    // Logic block: init calls helper (should propagate Critical to helper)
    LogicBlockEntry initBlock;
    initBlock.qualifiedName = "app::init";
    initBlock.simpleName = "init";
    initBlock.calledFunctions = {"helper"};
    initBlock.stages = {0};
    symbols.addLogicBlock(initBlock);

    return symbols;
}

TEST(PriorityAnalysis, ExplicitPrioritiesPreserved) {
    auto symbols = buildTestSymbols();
    auto result = analyzePriority(symbols);

    EXPECT_EQ(result.effectivePriority["app::init"], PriorityLevel::Critical);
    EXPECT_EQ(result.effectivePriority["app::cleanup"], PriorityLevel::Low);
}

TEST(PriorityAnalysis, PropagationFromCriticalCaller) {
    auto symbols = buildTestSymbols();
    auto result = analyzePriority(symbols);

    // helper is called by init (Critical), so should inherit Critical
    EXPECT_EQ(result.effectivePriority["app::helper"], PriorityLevel::Critical);
}

TEST(PriorityAnalysis, ExplicitOverridesPropagation) {
    SymbolTable symbols;

    FunctionSymbol caller;
    caller.qualifiedName = "app::main";
    caller.simpleName = "main";
    caller.visibility = Visibility::Public;
    caller.priority = PriorityLevel::Critical;
    symbols.addFunction(caller);

    // Callee has explicit Low priority — should not be overridden
    FunctionSymbol callee;
    callee.qualifiedName = "app::bg_task";
    callee.simpleName = "bg_task";
    callee.visibility = Visibility::Public;
    callee.priority = PriorityLevel::Low;
    symbols.addFunction(callee);

    // main calls bg_task
    LogicBlockEntry mainBlock;
    mainBlock.qualifiedName = "app::main";
    mainBlock.simpleName = "main";
    mainBlock.calledFunctions = {"bg_task"};
    mainBlock.stages = {0};
    symbols.addLogicBlock(mainBlock);

    auto result = analyzePriority(symbols);

    // Explicit Low is preserved even though caller is Critical
    EXPECT_EQ(result.effectivePriority["app::bg_task"], PriorityLevel::Low);
}

TEST(PriorityAnalysis, NormalCallerNoUpgrade) {
    SymbolTable symbols;

    FunctionSymbol caller;
    caller.qualifiedName = "app::run";
    caller.simpleName = "run";
    caller.visibility = Visibility::Public;
    caller.priority = PriorityLevel::Normal;
    symbols.addFunction(caller);

    FunctionSymbol callee;
    callee.qualifiedName = "app::step";
    callee.simpleName = "step";
    callee.visibility = Visibility::Public;
    callee.priority = PriorityLevel::Normal;
    symbols.addFunction(callee);

    LogicBlockEntry runBlock;
    runBlock.qualifiedName = "app::run";
    runBlock.simpleName = "run";
    runBlock.calledFunctions = {"step"};
    runBlock.stages = {0};
    symbols.addLogicBlock(runBlock);

    auto result = analyzePriority(symbols);

    // Normal caller should not elevate callee
    EXPECT_EQ(result.effectivePriority["app::step"], PriorityLevel::Normal);
}

TEST(PriorityAnalysis, EmptySymbolTable) {
    SymbolTable symbols;
    auto result = analyzePriority(symbols);
    EXPECT_TRUE(result.effectivePriority.empty());
}

// --- M4.1 extensions --------------------------------------------------------
//
// Helper utility to keep the extension tests compact and self-contained.

namespace {

static void addFn(SymbolTable& symbols, const std::string& qn, PriorityLevel pri) {
    FunctionSymbol fn;
    fn.qualifiedName = qn;
    auto pos = qn.rfind("::");
    fn.simpleName = (pos == std::string::npos) ? qn : qn.substr(pos + 2);
    fn.visibility = Visibility::Public;
    fn.priority = pri;
    symbols.addFunction(fn);
}

static void addBlockCall(SymbolTable& symbols, const std::string& callerQn, const std::string& calleeSimple) {
    LogicBlockEntry block;
    block.qualifiedName = callerQn;
    auto pos = callerQn.rfind("::");
    block.simpleName = (pos == std::string::npos) ? callerQn : callerQn.substr(pos + 2);
    block.calledFunctions = {calleeSimple};
    block.stages = {0};
    symbols.addLogicBlock(block);
}

} // namespace

TEST(PriorityAnalysis, MultiLevelPropagationReachesTransitiveCallees) {
    // critical -> helperA -> helperB (both helpers must become Critical)
    SymbolTable symbols;
    addFn(symbols, "app::critical", PriorityLevel::Critical);
    addFn(symbols, "app::helperA", PriorityLevel::Normal);
    addFn(symbols, "app::helperB", PriorityLevel::Normal);

    addBlockCall(symbols, "app::critical", "helperA");
    addBlockCall(symbols, "app::helperA", "helperB");

    auto result = analyzePriority(symbols);

    EXPECT_EQ(result.effectivePriority["app::critical"], PriorityLevel::Critical);
    EXPECT_EQ(result.effectivePriority["app::helperA"], PriorityLevel::Critical);
    EXPECT_EQ(result.effectivePriority["app::helperB"], PriorityLevel::Critical);
}

TEST(PriorityAnalysis, InferredPriorityDoesNotOverrideExplicitLower) {
    // Critical caller and High caller both call `shared`, but `shared` has
    // explicit Low priority — it must stay Low regardless of caller elevation.
    SymbolTable symbols;
    addFn(symbols, "app::critical", PriorityLevel::Critical);
    addFn(symbols, "app::high", PriorityLevel::High);
    addFn(symbols, "app::shared", PriorityLevel::Low);

    addBlockCall(symbols, "app::critical", "shared");
    addBlockCall(symbols, "app::high", "shared");

    auto result = analyzePriority(symbols);

    // Explicit annotation on `shared` is preserved.
    EXPECT_EQ(result.effectivePriority["app::shared"], PriorityLevel::Low);
    EXPECT_EQ(result.effectivePriority["app::critical"], PriorityLevel::Critical);
    EXPECT_EQ(result.effectivePriority["app::high"], PriorityLevel::High);
}

TEST(PriorityAnalysis, CyclicCallGraphTerminates) {
    // A <-> B with A Critical and B Normal. The BFS propagation must
    // terminate: B picks up Critical once, then the back-edge from B to A
    // cannot raise A any further, so the worklist drains.
    SymbolTable symbols;
    addFn(symbols, "app::A", PriorityLevel::Critical);
    addFn(symbols, "app::B", PriorityLevel::Normal);

    // A calls B
    LogicBlockEntry ab;
    ab.qualifiedName = "app::A";
    ab.simpleName = "A";
    ab.calledFunctions = {"B"};
    ab.stages = {0};
    symbols.addLogicBlock(ab);

    // B calls A (cycle)
    LogicBlockEntry ba;
    ba.qualifiedName = "app::B";
    ba.simpleName = "B";
    ba.calledFunctions = {"A"};
    ba.stages = {0};
    symbols.addLogicBlock(ba);

    auto result = analyzePriority(symbols);

    EXPECT_EQ(result.effectivePriority["app::A"], PriorityLevel::Critical);
    EXPECT_EQ(result.effectivePriority["app::B"], PriorityLevel::Critical);
}

TEST(PriorityAnalysis, HighCallerElevatesNormalCalleeToHigh) {
    // High caller propagates High to a Normal callee — not Critical.
    SymbolTable symbols;
    addFn(symbols, "app::highCaller", PriorityLevel::High);
    addFn(symbols, "app::callee", PriorityLevel::Normal);

    addBlockCall(symbols, "app::highCaller", "callee");

    auto result = analyzePriority(symbols);
    EXPECT_EQ(result.effectivePriority["app::callee"], PriorityLevel::High);
}
