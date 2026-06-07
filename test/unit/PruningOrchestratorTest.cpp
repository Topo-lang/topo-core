// Unit tests for PruningOrchestrator and StubRewriter.
//
// Uses mock StubGenerator to avoid filesystem I/O.
// Verifies stage grouping, stub/restore flow, and result reporting.

#include "topo/Check/PruningOrchestrator.h"
#include "topo/Check/StubRewriter.h"
#include "topo/Analysis/StageAnalysis.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

using namespace topo;
using namespace topo::check;
using namespace topo::analysis;

// --- Mock StubGenerator ---

class MockStubGen : public StubGenerator {
public:
    std::vector<std::string> stubbedFunctions;
    bool shouldFail = false;
    std::string failError = "mock failure";
    std::string failOnFunction; // fail only when stubbing this function

    StubResult stubFunction(const std::string& /*filePath*/, const std::string& funcName) override {
        StubResult result;
        if (shouldFail || funcName == failOnFunction) {
            result.error = failError;
            return result;
        }
        stubbedFunctions.push_back(funcName);
        result.success = true;
        result.originalContent = "original:" + funcName;
        return result;
    }

    bool restoreFile(const std::string& /*filePath*/, const StubResult& /*result*/) override { return true; }
};

// --- StubRewriter Tests ---

TEST(StubRewriter, StubFunctionsWithNoSourceFiles) {
    auto stubGen = std::make_unique<MockStubGen>();
    StubRewriter rewriter(std::move(stubGen), {});

    std::unordered_set<std::string> funcs = {"init", "process"};
    auto result = rewriter.stubFunctions(funcs);

    // No source files → no functions found → success with nothing stubbed
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.stubbedFunctions.empty());
    EXPECT_TRUE(result.originalContents.empty());
}

TEST(StubRewriter, RestoreEmptyResult) {
    auto stubGen = std::make_unique<MockStubGen>();
    StubRewriter rewriter(std::move(stubGen), {});

    StubRewriteResult emptyResult;
    emptyResult.success = true;

    EXPECT_TRUE(rewriter.restore(emptyResult));
}

// Regression (#3 — restore return must be honored): restore() returns false
// when a target path cannot be opened for writing. PruningOrchestrator and
// StubVerificationCheck now propagate this bool to mark the verification
// inconclusive; this test pins the StubRewriter contract those checks rely on.
// A directory path cannot be opened with ofstream on any supported platform,
// so it deterministically forces the open-failure branch.
TEST(StubRewriter, RestoreReportsFailureWhenTargetUnwritable) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "topo_stubrewriter_restore_fail";
    fs::remove_all(dir);
    fs::create_directories(dir);

    auto stubGen = std::make_unique<MockStubGen>();
    StubRewriter rewriter(std::move(stubGen), {});

    StubRewriteResult result;
    result.success = true;
    // Key is a directory path → ofstream open fails → restore reports false.
    result.originalContents[dir.string()] = "irrelevant content";

    EXPECT_FALSE(rewriter.restore(result))
        << "restore must report failure when a file cannot be written back";

    fs::remove_all(dir);
}

// --- PruningOrchestrator Tests ---
//
// Fixture portability: runBuildAndTest() directly execs testCommand's first
// token (no shell) in projectDir and only checks the exit code. So the fixture
// uses a real cross-platform executable that exits 0 ("hostname") rather than
// "echo" (a cmd.exe builtin, not an .exe on Windows), and "." as the working
// directory rather than "/tmp" (not a valid Windows path). On Windows the old
// fixture aborted with "failed to start process 'echo': The directory name is
// invalid" — only on the verifyStage(N>lowest) cases that actually run it.

TEST(PruningOrchestrator, EmptySymbolTablePassesTrivially) {
    SymbolTable symbols;
    StageAnalysisResult stageInfo;

    auto stubGen = std::make_unique<MockStubGen>();
    auto rewriter = std::make_unique<StubRewriter>(std::move(stubGen), std::vector<std::string>{});

    PruningConfig config;
    config.projectDir = ".";
    config.testCommand = "hostname";

    PruningOrchestrator orch(stageInfo, std::move(rewriter), config);
    auto result = orch.verifyAllStages();

    EXPECT_TRUE(result.passed());
    EXPECT_TRUE(result.stages.empty());
    EXPECT_EQ(result.passCount, 0);
    EXPECT_EQ(result.failCount, 0);
}

TEST(PruningOrchestrator, SingleStagePassesTrivially) {
    // With only one stage, there's nothing to stub (no lower stages)
    SymbolTable symbols;

    FunctionSymbol initSym;
    initSym.qualifiedName = "engine::init";
    initSym.simpleName = "init";
    initSym.visibility = Visibility::Public;
    symbols.addFunction(initSym);

    StageAnalysisResult stageInfo;
    stageInfo.calleeStageMap["engine::init"] = 1;

    auto stubGen = std::make_unique<MockStubGen>();
    auto rewriter = std::make_unique<StubRewriter>(std::move(stubGen), std::vector<std::string>{});

    PruningConfig config;
    config.projectDir = ".";
    config.testCommand = "hostname";

    PruningOrchestrator orch(stageInfo, std::move(rewriter), config);
    auto result = orch.verifyAllStages();

    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.stages.size(), 1u);
    EXPECT_EQ(result.stages[0].stage, 1);
    EXPECT_TRUE(result.stages[0].passed());
    // No functions were stubbed since there are no lower stages
    EXPECT_TRUE(result.stages[0].stubbedFunctions.empty());
}

TEST(PruningOrchestrator, MultipleStagesHighestFirst) {
    // Verify stages are processed highest→lowest
    SymbolTable symbols;

    FunctionSymbol initSym;
    initSym.qualifiedName = "engine::init";
    initSym.simpleName = "init";
    initSym.visibility = Visibility::Public;
    symbols.addFunction(initSym);

    FunctionSymbol processSym;
    processSym.qualifiedName = "engine::process";
    processSym.simpleName = "process";
    processSym.visibility = Visibility::Public;
    symbols.addFunction(processSym);

    FunctionSymbol finalizeSym;
    finalizeSym.qualifiedName = "engine::finalize";
    finalizeSym.simpleName = "finalize";
    finalizeSym.visibility = Visibility::Public;
    symbols.addFunction(finalizeSym);

    StageAnalysisResult stageInfo;
    stageInfo.calleeStageMap["engine::init"] = 1;
    stageInfo.calleeStageMap["engine::process"] = 2;
    stageInfo.calleeStageMap["engine::finalize"] = 3;

    auto stubGen = std::make_unique<MockStubGen>();
    auto rewriter = std::make_unique<StubRewriter>(std::move(stubGen), std::vector<std::string>{});

    PruningConfig config;
    config.projectDir = ".";
    config.testCommand = "hostname";

    PruningOrchestrator orch(stageInfo, std::move(rewriter), config);
    auto result = orch.verifyAllStages();

    EXPECT_EQ(result.stages.size(), 3u);
    // Highest stage first
    EXPECT_EQ(result.stages[0].stage, 3);
    EXPECT_EQ(result.stages[1].stage, 2);
    EXPECT_EQ(result.stages[2].stage, 1);
}

TEST(PruningOrchestrator, VerifySingleStageStubsLowerStages) {
    // Verify that verifyStage(2) stubs stage 1 functions
    SymbolTable symbols;

    FunctionSymbol initSym;
    initSym.qualifiedName = "engine::init";
    initSym.simpleName = "init";
    initSym.visibility = Visibility::Public;
    symbols.addFunction(initSym);

    FunctionSymbol processSym;
    processSym.qualifiedName = "engine::process";
    processSym.simpleName = "process";
    processSym.visibility = Visibility::Public;
    symbols.addFunction(processSym);

    StageAnalysisResult stageInfo;
    stageInfo.calleeStageMap["engine::init"] = 1;
    stageInfo.calleeStageMap["engine::process"] = 2;

    // No source files means no functions are found to stub,
    // but we can verify the live function set
    auto stubGen = std::make_unique<MockStubGen>();
    auto rewriter = std::make_unique<StubRewriter>(std::move(stubGen), std::vector<std::string>{});

    PruningConfig config;
    config.projectDir = ".";
    config.testCommand = "hostname";

    PruningOrchestrator orch(stageInfo, std::move(rewriter), config);
    auto result = orch.verifyStage(2);

    EXPECT_EQ(result.stage, 2);
    // Stage 2 should keep "process" live
    EXPECT_EQ(result.liveFunctions.size(), 1u);
    EXPECT_EQ(result.liveFunctions[0], "process");
    // Stage 1's "init" should have been in the stub set
    // (but not actually stubbed since no source files found)
    EXPECT_TRUE(result.passed());
}

TEST(PruningOrchestrator, LowestStageHasNothingToStub) {
    SymbolTable symbols;

    FunctionSymbol initSym;
    initSym.qualifiedName = "engine::init";
    initSym.simpleName = "init";
    initSym.visibility = Visibility::Public;
    symbols.addFunction(initSym);

    StageAnalysisResult stageInfo;
    stageInfo.calleeStageMap["engine::init"] = 1;

    auto stubGen = std::make_unique<MockStubGen>();
    auto rewriter = std::make_unique<StubRewriter>(std::move(stubGen), std::vector<std::string>{});

    PruningConfig config;
    config.projectDir = ".";
    config.testCommand = "hostname";

    PruningOrchestrator orch(stageInfo, std::move(rewriter), config);
    auto result = orch.verifyStage(1);

    EXPECT_TRUE(result.passed());
    EXPECT_TRUE(result.stubbedFunctions.empty());
    EXPECT_EQ(result.liveFunctions.size(), 1u);
}

TEST(PruningOrchestrator, StageGroupingWithMultipleFunctionsPerStage) {
    SymbolTable symbols;

    FunctionSymbol aSym;
    aSym.qualifiedName = "engine::a";
    aSym.simpleName = "a";
    aSym.visibility = Visibility::Public;
    symbols.addFunction(aSym);

    FunctionSymbol bSym;
    bSym.qualifiedName = "engine::b";
    bSym.simpleName = "b";
    bSym.visibility = Visibility::Public;
    symbols.addFunction(bSym);

    FunctionSymbol cSym;
    cSym.qualifiedName = "engine::c";
    cSym.simpleName = "c";
    cSym.visibility = Visibility::Public;
    symbols.addFunction(cSym);

    StageAnalysisResult stageInfo;
    stageInfo.calleeStageMap["engine::a"] = 1;
    stageInfo.calleeStageMap["engine::b"] = 1;
    stageInfo.calleeStageMap["engine::c"] = 2;

    auto stubGen = std::make_unique<MockStubGen>();
    auto rewriter = std::make_unique<StubRewriter>(std::move(stubGen), std::vector<std::string>{});

    PruningConfig config;
    config.projectDir = ".";
    config.testCommand = "hostname";

    PruningOrchestrator orch(stageInfo, std::move(rewriter), config);

    // Stage 2 should stub both stage-1 functions
    auto result = orch.verifyStage(2);
    EXPECT_EQ(result.stage, 2);
    EXPECT_EQ(result.liveFunctions.size(), 1u); // only "c"
    EXPECT_TRUE(result.passed());
}
