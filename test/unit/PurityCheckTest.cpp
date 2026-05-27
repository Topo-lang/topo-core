// Unit tests for checkPurity free function.

#include "topo/Check/PurityCheck.h"
#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Sema/SemanticAnalyzer.h"
#include "topo/Sema/SymbolTable.h"

#include <gtest/gtest.h>

using namespace topo;
using namespace topo::check;

// Test 1: No parallel stages — no violations possible
TEST(PurityCheck, NoParallelStages) {
    SymbolTable symbols;
    FunctionSymbol f;
    f.qualifiedName = "ns::a";
    f.simpleName = "a";
    f.visibility = Visibility::Public;
    symbols.addFunction(f);

    LogicBlockEntry lb;
    lb.qualifiedName = "ns::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"a"};
    lb.stages = {1};
    symbols.addLogicBlock(lb);

    // Write to global, but function is not in a parallel stage
    std::vector<SymbolAccess> accesses;
    accesses.push_back({"a", "globalVar", true, "test.cpp", 10});
    CheckResult result;
    checkPurity(symbols, accesses, result);
    EXPECT_TRUE(result.passed());
}

// Test 2: Parallel stage function with no global access — should pass
TEST(PurityCheck, ParallelNoGlobalAccessPasses) {
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
    lb.stages = {1, 1}; // parallel: same stage, multiple functions
    symbols.addLogicBlock(lb);

    std::vector<SymbolAccess> accesses; // empty
    CheckResult result;
    checkPurity(symbols, accesses, result);
    EXPECT_TRUE(result.passed());
}

// Test 3: Parallel function writes global — should error
TEST(PurityCheck, ParallelWriteGlobalFails) {
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

    std::vector<SymbolAccess> accesses;
    accesses.push_back({"a", "globalVar", true, "test.cpp", 10});
    CheckResult result;
    checkPurity(symbols, accesses, result);
    EXPECT_FALSE(result.passed());
    EXPECT_GT(result.errorCount, 0);
}

// Test 4: Parallel function reads global — warning but still passes
TEST(PurityCheck, ParallelReadGlobalWarns) {
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

    std::vector<SymbolAccess> accesses;
    accesses.push_back({"a", "globalVar", false, "test.cpp", 10}); // read only
    CheckResult result;
    checkPurity(symbols, accesses, result);
    // Read-only in parallel: produces a warning but still passes (no error)
    EXPECT_TRUE(result.passed());
    EXPECT_GE(result.warningCount, 1);
}

// Test 5: Non-parallel function with global write — should pass (purity only for parallel)
TEST(PurityCheck, NonParallelWriteGlobalPasses) {
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
    lb.stages = {1, 2}; // different stages, not parallel
    symbols.addLogicBlock(lb);

    std::vector<SymbolAccess> accesses;
    accesses.push_back({"a", "globalVar", true, "test.cpp", 10});
    CheckResult result;
    checkPurity(symbols, accesses, result);
    EXPECT_TRUE(result.passed());
}

// Test 6: Empty accesses — should pass regardless
TEST(PurityCheck, EmptyAccesses) {
    SymbolTable symbols;
    std::vector<SymbolAccess> accesses;
    CheckResult result;
    checkPurity(symbols, accesses, result);
    EXPECT_TRUE(result.passed());
}

// Test 7: Multiple parallel violations in same logic block
TEST(PurityCheck, MultipleViolations) {
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

    std::vector<SymbolAccess> accesses;
    accesses.push_back({"a", "globalVar1", true, "test.cpp", 10});
    accesses.push_back({"b", "globalVar2", true, "test.cpp", 20});
    CheckResult result;
    checkPurity(symbols, accesses, result);
    EXPECT_FALSE(result.passed());
    EXPECT_GE(result.errorCount, 2);
}

// Test 8 (M3 E.2 adversarial): "parallel_call_impure" — direct-only behavior.
// A parallel function `a` calls a non-parallel helper `helper`. The helper writes
// to a global. Only `helper` is flagged; the call edge a → helper does NOT cause
// `a` itself to be marked impure. This documents that PurityCheck is direct-only
// and does NOT do transitive taint propagation across the call graph.
TEST(PurityCheck, ParallelCallImpureHelper_DirectOnly) {
    SymbolTable symbols;
    FunctionSymbol fa;
    fa.qualifiedName = "ns::a";
    fa.simpleName = "a";
    fa.visibility = Visibility::Public;
    symbols.addFunction(fa);

    FunctionSymbol fb;
    fb.qualifiedName = "ns::b";
    fb.simpleName = "b";
    fb.visibility = Visibility::Public;
    symbols.addFunction(fb);

    FunctionSymbol fhelper;
    fhelper.qualifiedName = "ns::helper";
    fhelper.simpleName = "helper";
    fhelper.visibility = Visibility::Private;
    symbols.addFunction(fhelper);

    LogicBlockEntry lb;
    lb.qualifiedName = "ns::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"a", "b"};
    lb.stages = {1, 1};
    symbols.addLogicBlock(lb);

    // `a` itself does NOT write any global. Only `helper` does.
    // Since `helper` is not in `parallelFunctions`, it's not flagged either.
    // The end result: the violation that would be caught by transitive analysis
    // is NOT caught. This is the documented direct-only behavior.
    std::vector<SymbolAccess> accesses;
    accesses.push_back({"helper", "globalVar", true, "test.cpp", 30});
    CheckResult result;
    checkPurity(symbols, accesses, result);
    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
    EXPECT_EQ(result.warningCount, 0);
}

// Test 9 (M3 E.2 adversarial): "parallel_thread_local" — currently treated as
// generic global write and thus flagged. Documents the lack of differentiation
// between thread_local and shared globals — purity check sees only the symbol
// name and the isWrite flag, not storage class.
TEST(PurityCheck, ParallelThreadLocalWrite_FlaggedAsGlobalEdge) {
    SymbolTable symbols;
    FunctionSymbol fa;
    fa.qualifiedName = "ns::a";
    fa.simpleName = "a";
    fa.visibility = Visibility::Public;
    symbols.addFunction(fa);

    FunctionSymbol fb;
    fb.qualifiedName = "ns::b";
    fb.simpleName = "b";
    fb.visibility = Visibility::Public;
    symbols.addFunction(fb);

    LogicBlockEntry lb;
    lb.qualifiedName = "ns::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"a", "b"};
    lb.stages = {1, 1};
    symbols.addLogicBlock(lb);

    // The SymbolAccessExtractor supplies a SymbolAccess for a thread_local write.
    // PurityCheck cannot distinguish this from a shared-global write, so it
    // flags it as a violation. Tightening this is left as future work.
    std::vector<SymbolAccess> accesses;
    accesses.push_back({"a", "thread_local_counter", true, "test.cpp", 15});
    CheckResult result;
    checkPurity(symbols, accesses, result);
    EXPECT_FALSE(result.passed());
    EXPECT_GE(result.errorCount, 1);
}

// --- handler / flow consumed by the existing checkers (end-to-end) ---
//
// These parse real handler/flow .topo, run the full SemanticAnalyzer to
// build the SymbolTable, then run PurityCheck — proving the desugared
// form lands exactly in the structure the checker reads, with no
// checker change. A flow whose two handlers share a source makes them
// parallel candidates within one stage (multiple independent handlers
// off the same source -> same-stage parallel candidates), so PurityCheck
// guards their purity.

namespace {
SymbolTable analyzeSource(const std::string& src) {
    DiagnosticEngine diag;
    Lexer lexer(src, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    EXPECT_FALSE(diag.hasErrors());
    SemanticAnalyzer sema(diag);
    return sema.analyze(static_cast<const TopoFile&>(*ast));
}
} // namespace

// A handler that declares In/Out but has a hidden side effect (writes a
// global) is caught by PurityCheck when it is a parallel candidate in a
// flow — purity is enforced by core PurityCheck.
TEST(PurityCheck, FlowParallelHandlerHiddenSideEffectCaught) {
    const std::string src = R"(
namespace orders {
  public:
    handler ingest(string raw) -> i64;
    handler scoreA(i64 id) -> f64;
    handler scoreB(i64 id) -> f64;
    handler decide(f64 s) -> bool;

    flow decision {
      ingest -> scoreA;
      ingest -> scoreB;
      scoreA -> decide;
      scoreB -> decide;
      decide -> void;
    }
}
)";
    SymbolTable symbols = analyzeSource(src);

    // scoreA and scoreB are fed by the same source (ingest): the DAG
    // places them on the same stage → parallel candidates. The pipeline
    // path stores qualified node names in calledFunctions, which is what
    // PurityCheck matches against. scoreA hides a global write.
    std::vector<SymbolAccess> accesses;
    accesses.push_back({"orders::scoreA", "g_cache", true, "host.cpp", 12});
    CheckResult result;
    checkPurity(symbols, accesses, result);
    EXPECT_FALSE(result.passed());
    EXPECT_GE(result.errorCount, 1);
}

// The same flow, with every handler clean, passes.
TEST(PurityCheck, FlowParallelHandlersCleanPasses) {
    const std::string src = R"(
namespace orders {
  public:
    handler ingest(string raw) -> i64;
    handler scoreA(i64 id) -> f64;
    handler scoreB(i64 id) -> f64;
    handler decide(f64 s) -> bool;

    flow decision {
      ingest -> scoreA;
      ingest -> scoreB;
      scoreA -> decide;
      scoreB -> decide;
      decide -> void;
    }
}
)";
    SymbolTable symbols = analyzeSource(src);
    std::vector<SymbolAccess> accesses; // no global access anywhere
    CheckResult result;
    checkPurity(symbols, accesses, result);
    EXPECT_TRUE(result.passed());
}

// Test 10 (M3 E.2 adversarial): mixed parallel + sequential stages in the same
// logic block. Only functions in parallel stages are subject to purity checks;
// functions in sequential stages can safely write globals.
TEST(PurityCheck, MixedParallelAndSequentialStages) {
    SymbolTable symbols;
    FunctionSymbol fa;
    fa.qualifiedName = "ns::par_a";
    fa.simpleName = "par_a";
    fa.visibility = Visibility::Public;
    symbols.addFunction(fa);

    FunctionSymbol fb;
    fb.qualifiedName = "ns::par_b";
    fb.simpleName = "par_b";
    fb.visibility = Visibility::Public;
    symbols.addFunction(fb);

    FunctionSymbol fc;
    fc.qualifiedName = "ns::seq";
    fc.simpleName = "seq";
    fc.visibility = Visibility::Public;
    symbols.addFunction(fc);

    LogicBlockEntry lb;
    lb.qualifiedName = "ns::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"par_a", "par_b", "seq"};
    lb.stages = {1, 1, 2}; // par_a, par_b in parallel stage 1; seq alone in stage 2
    symbols.addLogicBlock(lb);

    // par_a writes a global → violation
    // seq writes a global → no violation (sequential stage)
    std::vector<SymbolAccess> accesses;
    accesses.push_back({"par_a", "shared", true, "test.cpp", 10});
    accesses.push_back({"seq", "shared", true, "test.cpp", 20});
    CheckResult result;
    checkPurity(symbols, accesses, result);
    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 1);
}

// Test 11: analysis-boundary disclosure. A parallel-stage purity check emits
// a Severity::Note stating it verified direct global access only — not
// transitive purity, not commutativity. The Note never changes pass/fail, so
// a clean check still passes while no longer silently overclaiming.
TEST(PurityCheck, ParallelStageEmitsAnalysisBoundaryNote) {
    SymbolTable symbols;
    FunctionSymbol fa;
    fa.qualifiedName = "ns::a";
    fa.simpleName = "a";
    fa.visibility = Visibility::Public;
    symbols.addFunction(fa);

    FunctionSymbol fb;
    fb.qualifiedName = "ns::b";
    fb.simpleName = "b";
    fb.visibility = Visibility::Public;
    symbols.addFunction(fb);

    LogicBlockEntry lb;
    lb.qualifiedName = "ns::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"a", "b"};
    lb.stages = {1, 1}; // parallel
    symbols.addLogicBlock(lb);

    std::vector<SymbolAccess> accesses; // clean — no global access
    CheckResult result;
    checkPurity(symbols, accesses, result);

    EXPECT_TRUE(result.passed()); // the Note does not fail the check
    int notes = 0;
    for (const auto& d : result.diagnostics) {
        if (d.severity == Severity::Note &&
            d.message.find("direct-global-access depth") != std::string::npos)
            ++notes;
    }
    EXPECT_EQ(notes, 1)
        << "a parallel-stage purity check must disclose its analysis boundary";
}

// Test 12: no parallel stages → nothing was checked → no boundary note.
TEST(PurityCheck, NoParallelStagesEmitsNoBoundaryNote) {
    SymbolTable symbols;
    FunctionSymbol fa;
    fa.qualifiedName = "ns::a";
    fa.simpleName = "a";
    fa.visibility = Visibility::Public;
    symbols.addFunction(fa);

    LogicBlockEntry lb;
    lb.qualifiedName = "ns::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"a"};
    lb.stages = {1}; // single op — not a parallel stage
    symbols.addLogicBlock(lb);

    std::vector<SymbolAccess> accesses;
    CheckResult result;
    checkPurity(symbols, accesses, result);
    for (const auto& d : result.diagnostics) {
        EXPECT_EQ(d.message.find("direct-global-access depth"),
                  std::string::npos)
            << "no boundary note when there are no parallel stages";
    }
}
