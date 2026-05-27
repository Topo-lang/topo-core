// Unit tests for checkStageIsolation free function.

#include "topo/Check/StageIsolationCheck.h"
#include "topo/Sema/SymbolTable.h"

#include <gtest/gtest.h>

using namespace topo;
using namespace topo::check;

// Test 1: Empty call graph — no edges, no violations
TEST(StageIsolationCheck, EmptyCallGraph) {
    SymbolTable symbols;
    std::vector<CallEdge> edges;
    CheckResult result;
    checkStageIsolation(symbols, edges, result);
    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
}

// Test 2: No logic blocks — edges exist but no stage info, should pass
TEST(StageIsolationCheck, NoLogicBlocks) {
    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::a";
    f1.simpleName = "a";
    f1.visibility = Visibility::Public;
    symbols.addFunction(f1);

    FunctionSymbol f2;
    f2.qualifiedName = "ns::b";
    f2.simpleName = "b";
    f2.visibility = Visibility::Public;
    symbols.addFunction(f2);

    std::vector<CallEdge> edges;
    edges.push_back({"ns::a", "ns::b", "test.cpp", 10});
    CheckResult result;
    checkStageIsolation(symbols, edges, result);
    EXPECT_TRUE(result.passed());
}

// Test 3: Same-stage call — should pass (functions in same stage can call each other)
TEST(StageIsolationCheck, SameStageCallPasses) {
    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::a";
    f1.simpleName = "a";
    f1.visibility = Visibility::Public;
    symbols.addFunction(f1);

    FunctionSymbol f2;
    f2.qualifiedName = "ns::b";
    f2.simpleName = "b";
    f2.visibility = Visibility::Public;
    symbols.addFunction(f2);

    LogicBlockEntry lb;
    lb.qualifiedName = "ns::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"a", "b"};
    lb.stages = {1, 1};
    symbols.addLogicBlock(lb);

    // "a" calls "b" — both stage 1, same stage is fine
    std::vector<CallEdge> edges;
    edges.push_back({"a", "b", "test.cpp", 10});
    CheckResult result;
    checkStageIsolation(symbols, edges, result);
    EXPECT_TRUE(result.passed());
}

// Test 4: Forward stage call (stage 1 calls stage 2) — violation:
// Stage isolation means stage N must not depend on stage N+1 outputs.
// The check flags when a function at stage N calls into a later stage.
TEST(StageIsolationCheck, ForwardStageCallFails) {
    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::init";
    f1.simpleName = "init";
    f1.visibility = Visibility::Public;
    symbols.addFunction(f1);

    FunctionSymbol f2;
    f2.qualifiedName = "ns::process";
    f2.simpleName = "process";
    f2.visibility = Visibility::Public;
    symbols.addFunction(f2);

    LogicBlockEntry lb;
    lb.qualifiedName = "ns::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"init", "process"};
    lb.stages = {1, 2};
    symbols.addLogicBlock(lb);

    // init (stage 1) calls process (stage 2) — forward dependency violation
    std::vector<CallEdge> edges;
    edges.push_back({"init", "process", "test.cpp", 10});
    CheckResult result;
    checkStageIsolation(symbols, edges, result);
    EXPECT_FALSE(result.passed());
    EXPECT_GT(result.errorCount, 0);
}

// Test 5: Backward stage call (stage 2 calls stage 1) — should pass
// A later stage calling an earlier stage is allowed.
TEST(StageIsolationCheck, BackwardStageCallPasses) {
    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::init";
    f1.simpleName = "init";
    f1.visibility = Visibility::Public;
    symbols.addFunction(f1);

    FunctionSymbol f2;
    f2.qualifiedName = "ns::process";
    f2.simpleName = "process";
    f2.visibility = Visibility::Public;
    symbols.addFunction(f2);

    LogicBlockEntry lb;
    lb.qualifiedName = "ns::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"init", "process"};
    lb.stages = {1, 2};
    symbols.addLogicBlock(lb);

    // process (stage 2) calls init (stage 1) — backward, allowed
    std::vector<CallEdge> edges;
    edges.push_back({"process", "init", "test.cpp", 20});
    CheckResult result;
    checkStageIsolation(symbols, edges, result);
    EXPECT_TRUE(result.passed());
}

// Test 6: Multiple logic blocks — violations checked per logic block
TEST(StageIsolationCheck, MultipleLogicBlocks) {
    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::a";
    f1.simpleName = "a";
    f1.visibility = Visibility::Public;
    symbols.addFunction(f1);

    FunctionSymbol f2;
    f2.qualifiedName = "ns::b";
    f2.simpleName = "b";
    f2.visibility = Visibility::Public;
    symbols.addFunction(f2);

    FunctionSymbol f3;
    f3.qualifiedName = "ns::c";
    f3.simpleName = "c";
    f3.visibility = Visibility::Public;
    symbols.addFunction(f3);

    FunctionSymbol f4;
    f4.qualifiedName = "ns::d";
    f4.simpleName = "d";
    f4.visibility = Visibility::Public;
    symbols.addFunction(f4);

    LogicBlockEntry lb1;
    lb1.qualifiedName = "ns::run1";
    lb1.simpleName = "run1";
    lb1.calledFunctions = {"a", "b"};
    lb1.stages = {1, 2};
    symbols.addLogicBlock(lb1);

    LogicBlockEntry lb2;
    lb2.qualifiedName = "ns::run2";
    lb2.simpleName = "run2";
    lb2.calledFunctions = {"c", "d"};
    lb2.stages = {1, 2};
    symbols.addLogicBlock(lb2);

    // Both logic blocks have forward violations (stage 1 calling stage 2)
    std::vector<CallEdge> edges;
    edges.push_back({"a", "b", "test.cpp", 10});
    edges.push_back({"c", "d", "test.cpp", 20});
    CheckResult result;
    checkStageIsolation(symbols, edges, result);
    EXPECT_FALSE(result.passed());
    EXPECT_GE(result.errorCount, 2);
}

// Test 7: Edge with unresolved function (not in any logic block) — should be ignored
TEST(StageIsolationCheck, UnresolvedFunctionIgnored) {
    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::a";
    f1.simpleName = "a";
    f1.visibility = Visibility::Public;
    symbols.addFunction(f1);

    LogicBlockEntry lb;
    lb.qualifiedName = "ns::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"a"};
    lb.stages = {1};
    symbols.addLogicBlock(lb);

    // Edge references unknown function not in any logic block
    std::vector<CallEdge> edges;
    edges.push_back({"a", "unknown", "test.cpp", 10});
    CheckResult result;
    checkStageIsolation(symbols, edges, result);
    EXPECT_TRUE(result.passed());
}

// Test 8 (M3 E.2 adversarial): "forward_call_indirect" — direct-only behavior.
// Stage 1 fn `a` calls stage 1 fn `b`; `b` then calls stage 2 fn `c`.
// The check sees two edges: a→b (same stage, OK) and b→c (forward, violation).
// It does NOT compose them into a transitive a→c violation. Documents the
// direct-only analysis boundary that mirrors containment-analysis-limits.
TEST(StageIsolationCheck, ForwardCallIndirect_DirectOnly) {
    SymbolTable symbols;
    FunctionSymbol fa, fb, fc;
    fa.qualifiedName = "ns::a"; fa.simpleName = "a"; fa.visibility = Visibility::Public;
    fb.qualifiedName = "ns::b"; fb.simpleName = "b"; fb.visibility = Visibility::Public;
    fc.qualifiedName = "ns::c"; fc.simpleName = "c"; fc.visibility = Visibility::Public;
    symbols.addFunction(fa);
    symbols.addFunction(fb);
    symbols.addFunction(fc);

    LogicBlockEntry lb;
    lb.qualifiedName = "ns::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"a", "b", "c"};
    lb.stages = {1, 1, 2};
    symbols.addLogicBlock(lb);

    std::vector<CallEdge> edges;
    edges.push_back({"a", "b", "test.cpp", 10}); // same stage → OK
    edges.push_back({"b", "c", "test.cpp", 11}); // forward → violation
    // No edge a→c synthesized; the transitive path is not analyzed.
    CheckResult result;
    checkStageIsolation(symbols, edges, result);
    EXPECT_FALSE(result.passed());
    // Exactly one violation is reported (b→c), not two.
    EXPECT_EQ(result.errorCount, 1);
}

// Test 9 (M3 E.2 adversarial): "cross_logic_block" — two independent logic
// blocks each have a stage 1; functions in different logic blocks do not
// participate in each other's stage isolation analysis.
TEST(StageIsolationCheck, CrossLogicBlock_BothStage1_NoCrossAnalysis) {
    SymbolTable symbols;
    FunctionSymbol fa, fb;
    fa.qualifiedName = "modA::work"; fa.simpleName = "work"; fa.visibility = Visibility::Public;
    fb.qualifiedName = "modB::work"; fb.simpleName = "work"; fb.visibility = Visibility::Public;
    symbols.addFunction(fa);
    symbols.addFunction(fb);

    LogicBlockEntry lba;
    lba.qualifiedName = "modA::run";
    lba.simpleName = "run";
    lba.calledFunctions = {"work"};
    lba.stages = {1};
    symbols.addLogicBlock(lba);

    LogicBlockEntry lbb;
    lbb.qualifiedName = "modB::run";
    lbb.simpleName = "run";
    lbb.calledFunctions = {"work"};
    lbb.stages = {1};
    symbols.addLogicBlock(lbb);

    // Hypothetical cross-logic-block call (does not normally occur in real
    // code) — must not produce a stage isolation violation since the two
    // stages live in independent block hierarchies.
    std::vector<CallEdge> edges;
    edges.push_back({"modA::work", "modB::work", "test.cpp", 5});
    CheckResult result;
    checkStageIsolation(symbols, edges, result);
    EXPECT_TRUE(result.passed());
}

// Test 10 (M3 E.2 adversarial): "single_stage_self_recurse" — a stage 1
// function recursively calls itself. The self-edge is same-stage and must pass.
TEST(StageIsolationCheck, SingleStageSelfRecurse_Passes) {
    SymbolTable symbols;
    FunctionSymbol f;
    f.qualifiedName = "ns::recurse";
    f.simpleName = "recurse";
    f.visibility = Visibility::Public;
    symbols.addFunction(f);

    LogicBlockEntry lb;
    lb.qualifiedName = "ns::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"recurse"};
    lb.stages = {1};
    symbols.addLogicBlock(lb);

    std::vector<CallEdge> edges;
    edges.push_back({"recurse", "recurse", "test.cpp", 10}); // self-recursion
    CheckResult result;
    checkStageIsolation(symbols, edges, result);
    EXPECT_TRUE(result.passed());
}
