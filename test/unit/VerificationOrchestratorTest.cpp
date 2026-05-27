// Unit tests for VerificationOrchestrator with mock StubGenerator.

#include "topo/Check/VerificationOrchestrator.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace topo;
using namespace topo::check;

// --- Mock StubGenerator ---

class MockStubGenerator : public StubGenerator {
public:
    std::vector<std::string> stubbedFunctions;
    bool shouldFail = false;
    std::string failError = "mock failure";

    StubResult stubFunction(const std::string& /*filePath*/, const std::string& funcName) override {
        StubResult result;
        if (shouldFail) {
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

// --- Tests ---

TEST(VerificationOrchestrator, StageIsolationWithNoLogicBlocks) {
    // Empty symbol table — no logic blocks to check
    SymbolTable symbols;

    auto stubGen = std::make_unique<MockStubGenerator>();
    VerificationConfig config;
    config.projectDir = "/tmp";
    config.testCommand = "echo ok";

    VerificationOrchestrator orch(symbols, std::move(stubGen), config);
    auto result = orch.verifyStageIsolation();

    EXPECT_EQ(result.checkName, "stage-isolation");
    EXPECT_TRUE(result.tests.empty());
    EXPECT_TRUE(result.passed());
}

TEST(VerificationOrchestrator, StageIsolationWithSingleFunctionPerStage) {
    // Stages with only 1 function each — no isolation to check
    // Build SymbolTable programmatically to avoid parser dependency
    SymbolTable symbols;

    FunctionSymbol initSym;
    initSym.qualifiedName = "engine::core::init";
    initSym.simpleName = "init";
    initSym.visibility = Visibility::Public;
    symbols.addFunction(initSym);

    FunctionSymbol processSym;
    processSym.qualifiedName = "engine::core::process";
    processSym.simpleName = "process";
    processSym.visibility = Visibility::Public;
    symbols.addFunction(processSym);

    LogicBlockEntry lb;
    lb.qualifiedName = "engine::core::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"init", "process"};
    lb.stages = {1, 2};
    symbols.addLogicBlock(lb);

    auto stubGen = std::make_unique<MockStubGenerator>();
    VerificationConfig config;
    config.projectDir = "/tmp";
    config.testCommand = "echo ok";

    VerificationOrchestrator orch(symbols, std::move(stubGen), config);
    auto result = orch.verifyStageIsolation();

    // Each stage has exactly 1 function, so no pairs to test
    EXPECT_TRUE(result.passed());
}

TEST(VerificationOrchestrator, StageIsolationWithMultipleFunctionsInStage) {
    // Two functions in the same stage — should generate test cases
    SymbolTable symbols;

    FunctionSymbol initSym;
    initSym.qualifiedName = "engine::core::init";
    initSym.simpleName = "init";
    initSym.visibility = Visibility::Public;
    symbols.addFunction(initSym);

    FunctionSymbol setupSym;
    setupSym.qualifiedName = "engine::core::setup";
    setupSym.simpleName = "setup";
    setupSym.visibility = Visibility::Public;
    symbols.addFunction(setupSym);

    FunctionSymbol processSym;
    processSym.qualifiedName = "engine::core::process";
    processSym.simpleName = "process";
    processSym.visibility = Visibility::Public;
    symbols.addFunction(processSym);

    LogicBlockEntry lb;
    lb.qualifiedName = "engine::core::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"init", "setup", "process"};
    lb.stages = {1, 1, 2};
    symbols.addLogicBlock(lb);

    auto stubGen = std::make_unique<MockStubGenerator>();
    VerificationConfig config;
    config.projectDir = "/tmp";
    config.testCommand = "echo ok";
    // sourceFiles is empty, so findSourceFileForFunction will fail,
    // producing error results without calling runBuildAndTest

    VerificationOrchestrator orch(symbols, std::move(stubGen), config);
    auto result = orch.verifyStageIsolation();

    EXPECT_EQ(result.checkName, "stage-isolation");
    // Should have attempted to test the two stage-1 functions (init, setup)
    EXPECT_GE(result.tests.size(), 2u);
}

TEST(VerificationOrchestrator, PipelineIndependenceWithNoPipelines) {
    // Non-pipeline logic blocks only
    SymbolTable symbols;

    FunctionSymbol initSym;
    initSym.qualifiedName = "engine::core::init";
    initSym.simpleName = "init";
    initSym.visibility = Visibility::Public;
    symbols.addFunction(initSym);

    LogicBlockEntry lb;
    lb.qualifiedName = "engine::core::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"init"};
    lb.stages = {1};
    // isPipeline defaults to false
    symbols.addLogicBlock(lb);

    auto stubGen = std::make_unique<MockStubGenerator>();
    VerificationConfig config;
    config.projectDir = "/tmp";
    config.testCommand = "echo ok";

    VerificationOrchestrator orch(symbols, std::move(stubGen), config);
    auto result = orch.verifyPipelineIndependence();

    EXPECT_EQ(result.checkName, "pipeline-independence");
    EXPECT_TRUE(result.tests.empty());
    EXPECT_TRUE(result.passed());
}

TEST(VerificationOrchestrator, VerifyAllRunsBothChecks) {
    SymbolTable symbols;

    auto stubGen = std::make_unique<MockStubGenerator>();
    VerificationConfig config;
    config.projectDir = "/tmp";
    config.testCommand = "echo ok";

    VerificationOrchestrator orch(symbols, std::move(stubGen), config);
    auto results = orch.verifyAll();

    EXPECT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].checkName, "stage-isolation");
    EXPECT_EQ(results[1].checkName, "pipeline-independence");
}
