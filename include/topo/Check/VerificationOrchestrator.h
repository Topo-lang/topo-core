#ifndef TOPO_CHECK_VERIFICATIONORCHESTRATOR_H
#define TOPO_CHECK_VERIFICATIONORCHESTRATOR_H

#include "topo/Check/StubGenerator.h"
#include "topo/Sema/SymbolTable.h"

#include <memory>
#include <string>
#include <vector>

namespace topo::check {

/// Configuration for verification runs.
struct VerificationConfig {
    std::string projectDir;
    std::string testCommand; // e.g., "ctest -R unit"
    std::vector<std::string> sourceFiles;
    bool verbose = false;
};

/// Result of a single verification test (one function stubbed).
struct VerificationTestResult {
    std::string stubbedFunction;
    std::string checkedProperty; // what was verified
    bool compileSuccess = false;
    bool testSuccess = false;
    std::string error;
};

/// Result of a complete verification run.
struct VerificationResult {
    std::string checkName; // e.g., "stage-isolation"
    std::vector<VerificationTestResult> tests;
    int passCount = 0;
    int failCount = 0;

    bool passed() const { return failCount == 0; }
};

/// Orchestrates code-stripping verification.
///
/// For each verification scenario, the orchestrator:
/// 1. Stubs a function body in source code
/// 2. Runs build + test commands
/// 3. Checks whether tests pass or fail as expected
/// 4. Restores original source
class VerificationOrchestrator {
public:
    VerificationOrchestrator(const SymbolTable& symbols,
                             std::unique_ptr<StubGenerator> stubGen,
                             const VerificationConfig& config);

    /// Verify stage isolation: for each stage with multiple functions,
    /// stub one and verify the others still compile and pass tests.
    VerificationResult verifyStageIsolation();

    /// Verify pipeline branch independence: stub one branch's functions,
    /// verify other branches still work.
    VerificationResult verifyPipelineIndependence();

    /// Run all applicable verifications.
    std::vector<VerificationResult> verifyAll();

private:
    /// Run build + test with current source state.
    /// Parses testCommand into executable + args and runs via platform::runProcess.
    bool runBuildAndTest(std::string& error);

    /// Find the source file containing a given function name.
    std::string findSourceFileForFunction(const std::string& funcName) const;

    const SymbolTable& symbols_;
    std::unique_ptr<StubGenerator> stubGen_;
    VerificationConfig config_;
};

} // namespace topo::check

#endif // TOPO_CHECK_VERIFICATIONORCHESTRATOR_H
