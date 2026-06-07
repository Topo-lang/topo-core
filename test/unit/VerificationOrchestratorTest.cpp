// Unit tests for VerificationOrchestrator with mock StubGenerator.

#include "topo/Check/VerificationOrchestrator.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
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

// A stub generator backed by real files. It mirrors the production
// whole-file read/modify/write + whole-file restore semantics, so it
// actually mutates the source on disk — exercising the restore path where
// the source-corruption bug lived (two functions sharing one file).
class FileBackedStubGenerator : public StubGenerator {
public:
    StubResult stubFunction(const std::string& filePath, const std::string& funcName) override {
        StubResult result;
        std::ifstream ifs(filePath, std::ios::binary);
        if (!ifs) {
            result.error = "open failed";
            return result;
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();
        result.originalContent = content; // whole-file snapshot, as production does
        // "Stub" the function by swapping its body marker.
        const std::string from = funcName + "_BODY";
        const std::string to = funcName + "_STUB";
        auto pos = content.find(from);
        if (pos != std::string::npos) content.replace(pos, from.size(), to);
        std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
        ofs << content;
        result.success = true;
        return result;
    }

    bool restoreFile(const std::string& filePath, const StubResult& result) override {
        std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
        if (!ofs) return false;
        ofs << result.originalContent;
        return true;
    }
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

// Regression: when a pipeline branch contains two functions defined in the
// SAME source file, verifyPipelineIndependence must restore that file fully.
// The pre-fix code restored each StubResult in push order, so the second
// (already-stubbed) whole-file snapshot overwrote the true original and left
// the first function permanently stubbed in the user's source.
TEST(VerificationOrchestrator, PipelineIndependenceRestoresSharedFileFully) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "topo_verif_orch_shared_file";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::path file1 = dir / "branch_a.cpp"; // holds funcA AND funcB (same branch)
    fs::path file2 = dir / "branch_c.cpp"; // holds funcC AND funcD (other branch)
    const std::string orig1 = "void funcA() { funcA_BODY; }\nvoid funcB() { funcB_BODY; }\n";
    const std::string orig2 = "void funcC() { funcC_BODY; }\nvoid funcD() { funcD_BODY; }\n";
    { std::ofstream(file1.string(), std::ios::binary) << orig1; }
    { std::ofstream(file2.string(), std::ios::binary) << orig2; }

    SymbolTable symbols;
    auto addFn = [&](const std::string& simple) {
        FunctionSymbol s;
        s.qualifiedName = "ns::" + simple;
        s.simpleName = simple;
        s.visibility = Visibility::Public;
        symbols.addFunction(s);
    };
    addFn("funcA");
    addFn("funcB");
    addFn("funcC");
    addFn("funcD");

    LogicBlockEntry lb;
    lb.qualifiedName = "ns::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"funcA", "funcB", "funcC", "funcD"};
    lb.stages = {1, 1, 1, 1};
    lb.isPipeline = true;
    // Two independent branches: funcA->funcB and funcC->funcD. funcA and
    // funcC are the (two) source nodes, so pipeline-independence runs.
    PipelineEdge e1;
    e1.source = "funcA";
    e1.target = "funcB";
    PipelineEdge e2;
    e2.source = "funcC";
    e2.target = "funcD";
    lb.edges = {e1, e2};
    symbols.addLogicBlock(lb);

    auto stubGen = std::make_unique<FileBackedStubGenerator>();
    VerificationConfig config;
    config.projectDir = dir.string();
    config.testCommand = "echo ok"; // restore runs regardless of test outcome
    config.sourceFiles = {file1.string(), file2.string()};

    VerificationOrchestrator orch(symbols, std::move(stubGen), config);
    orch.verifyPipelineIndependence();

    auto readAll = [](const fs::path& p) {
        std::ifstream ifs(p.string(), std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    };
    EXPECT_EQ(readAll(file1), orig1) << "shared-file branch left user source corrupted";
    EXPECT_EQ(readAll(file2), orig2);

    fs::remove_all(dir);
}

// A file-backed generator that throws on the Nth stubFunction call, after
// having mutated earlier files on disk. Models a throw inside the
// stub→run→restore window (allocation / IO / process failure). The RAII
// restore guard must restore every already-stubbed file before the exception
// escapes the orchestrator.
class ThrowingStubGenerator : public StubGenerator {
public:
    int throwOnCall = 2; // throw on the 2nd stub (1st already written to disk)
    int calls = 0;

    StubResult stubFunction(const std::string& filePath, const std::string& funcName) override {
        ++calls;
        if (calls >= throwOnCall) {
            throw std::runtime_error("simulated mid-window failure");
        }
        StubResult result;
        std::ifstream ifs(filePath, std::ios::binary);
        if (!ifs) {
            result.error = "open failed";
            return result;
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();
        result.originalContent = content;
        const std::string from = funcName + "_BODY";
        const std::string to = funcName + "_STUB";
        auto pos = content.find(from);
        if (pos != std::string::npos) content.replace(pos, from.size(), to);
        std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
        ofs << content;
        result.success = true;
        return result;
    }

    bool restoreFile(const std::string& filePath, const StubResult& result) override {
        std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
        if (!ofs) return false;
        ofs << result.originalContent;
        return true;
    }
};

// Regression (#2 — restore guaranteed on exception): if stubFunction throws
// after an earlier function in the same branch was already stubbed on disk,
// the RAII guard must restore the already-modified file before the exception
// propagates, leaving the user's working tree untouched.
TEST(VerificationOrchestrator, PipelineIndependenceRestoresOnException) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "topo_verif_orch_throw";
    fs::remove_all(dir);
    fs::create_directories(dir);
    // funcA and funcB live in the SAME branch file; funcA is stubbed first,
    // then the generator throws while stubbing funcB.
    fs::path file1 = dir / "branch_a.cpp";
    fs::path file2 = dir / "branch_c.cpp";
    const std::string orig1 = "void funcA() { funcA_BODY; }\nvoid funcB() { funcB_BODY; }\n";
    const std::string orig2 = "void funcC() { funcC_BODY; }\nvoid funcD() { funcD_BODY; }\n";
    { std::ofstream(file1.string(), std::ios::binary) << orig1; }
    { std::ofstream(file2.string(), std::ios::binary) << orig2; }

    SymbolTable symbols;
    auto addFn = [&](const std::string& simple) {
        FunctionSymbol s;
        s.qualifiedName = "ns::" + simple;
        s.simpleName = simple;
        s.visibility = Visibility::Public;
        symbols.addFunction(s);
    };
    addFn("funcA");
    addFn("funcB");
    addFn("funcC");
    addFn("funcD");

    LogicBlockEntry lb;
    lb.qualifiedName = "ns::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"funcA", "funcB", "funcC", "funcD"};
    lb.stages = {1, 1, 1, 1};
    lb.isPipeline = true;
    PipelineEdge e1;
    e1.source = "funcA";
    e1.target = "funcB";
    PipelineEdge e2;
    e2.source = "funcC";
    e2.target = "funcD";
    lb.edges = {e1, e2};
    symbols.addLogicBlock(lb);

    auto stubGen = std::make_unique<ThrowingStubGenerator>();
    VerificationConfig config;
    config.projectDir = dir.string();
    config.testCommand = "echo ok";
    config.sourceFiles = {file1.string(), file2.string()};

    VerificationOrchestrator orch(symbols, std::move(stubGen), config);
    // The exception must propagate (not be swallowed) ...
    EXPECT_THROW(orch.verifyPipelineIndependence(), std::runtime_error);

    auto readAll = [](const fs::path& p) {
        std::ifstream ifs(p.string(), std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    };
    // ... and the already-stubbed file must have been restored by the guard.
    EXPECT_EQ(readAll(file1), orig1) << "exception in stub window left user source corrupted";
    EXPECT_EQ(readAll(file2), orig2);

    fs::remove_all(dir);
}

// A generator whose restoreFile always fails. Used to verify the orchestrator
// surfaces the failure rather than silently dropping the bool return.
class RestoreFailingStubGenerator : public StubGenerator {
public:
    StubResult stubFunction(const std::string& /*filePath*/, const std::string& funcName) override {
        StubResult result;
        result.success = true;
        result.originalContent = "original:" + funcName;
        return result;
    }
    bool restoreFile(const std::string& /*filePath*/, const StubResult& /*result*/) override {
        return false; // restore always fails
    }
};

// Regression (#3 — restore return checked): when restoreFile returns false,
// stage-isolation must mark the test failed and record a diagnostic noting the
// source may be modified, instead of silently reporting success.
TEST(VerificationOrchestrator, StageIsolationSurfacesRestoreFailure) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "topo_verif_orch_restore_fail";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::path file1 = dir / "stage.cpp";
    // Both init and setup must be findable in the file so findSourceFile hits.
    { std::ofstream(file1.string(), std::ios::binary) << "void init(){} void setup(){}\n"; }

    SymbolTable symbols;
    auto addFn = [&](const std::string& simple) {
        FunctionSymbol s;
        s.qualifiedName = "ns::" + simple;
        s.simpleName = simple;
        s.visibility = Visibility::Public;
        symbols.addFunction(s);
    };
    addFn("init");
    addFn("setup");

    LogicBlockEntry lb;
    lb.qualifiedName = "ns::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"init", "setup"};
    lb.stages = {1, 1}; // same stage → isolation is exercised
    symbols.addLogicBlock(lb);

    auto stubGen = std::make_unique<RestoreFailingStubGenerator>();
    VerificationConfig config;
    config.projectDir = dir.string();
    config.testCommand = "echo ok"; // build+test "passes"; restore is what fails
    config.sourceFiles = {file1.string()};

    VerificationOrchestrator orch(symbols, std::move(stubGen), config);
    auto result = orch.verifyStageIsolation();

    ASSERT_FALSE(result.tests.empty());
    // Every test that reached the restore step must be marked failed, with a
    // diagnostic mentioning the unrestored source — never a silent pass.
    for (const auto& tr : result.tests) {
        EXPECT_FALSE(tr.testSuccess) << "restore failure was reported as success";
        EXPECT_FALSE(tr.compileSuccess);
        EXPECT_NE(tr.error.find("failed to restore"), std::string::npos)
            << "restore-failure diagnostic missing: " << tr.error;
    }
    EXPECT_FALSE(result.passed());

    fs::remove_all(dir);
}
