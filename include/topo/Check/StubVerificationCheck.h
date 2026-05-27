#ifndef TOPO_CHECK_STUBVERIFICATIONCHECK_H
#define TOPO_CHECK_STUBVERIFICATIONCHECK_H

#include "topo/Check/StubRewriter.h"
#include "topo/Analysis/StageAnalysis.h"
#include "topo/Sema/SymbolTable.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace topo::check {

/// Result of L2 verification for a single function.
struct StubVerifyFunctionResult {
    std::string functionName;   // qualified name of the stubbed function
    int stage = 0;              // stage the function belongs to
    bool compileSuccess = false;
    bool testSuccess = false;
    std::string error;

    bool passed() const { return compileSuccess && testSuccess; }
};

/// Aggregated result of L2 stub verification across all candidate functions.
struct StubVerificationResult {
    std::vector<StubVerifyFunctionResult> functions;
    int passCount = 0;
    int failCount = 0;
    int skipCount = 0; // functions not found in source files

    bool passed() const { return failCount == 0; }

    /// Functions that failed — these have hidden dependencies.
    std::vector<std::string> failedFunctions() const {
        std::vector<std::string> result;
        for (const auto& f : functions) {
            if (!f.passed()) result.push_back(f.functionName);
        }
        return result;
    }
};

/// Configuration for L2 stub verification.
struct StubVerificationConfig {
    std::string projectDir;
    std::string buildCommand;  // e.g., "cmake --build build"
    std::string testCommand;   // e.g., "ctest -R unit"
    std::vector<std::string> sourceFiles;
    bool verbose = false;

    /// If non-empty, only verify functions matching this pattern.
    std::string filterPattern;
};

/// L2 Stub Replacement Verification.
///
/// For each non-entry-point stage callee:
///   1. Replace the function body with a minimal stub (return default)
///   2. Attempt to compile the project
///   3. If compilation succeeds, run the user's test suite
///   4. Report whether the function is truly isolated (tests pass)
///      or has hidden dependencies (tests fail)
///   5. Restore the original source
///
/// This goes beyond L1 static checks by actually modifying and running
/// the code, catching dependencies that static analysis misses:
/// - Global state coupling through shared variables
/// - Pointer aliasing across stage boundaries
/// - Implicit ordering through side effects
///
/// Gated behind `--l2-verify` in topo-test.
class StubVerificationCheck {
public:
    StubVerificationCheck(const SymbolTable& symbols,
                          const analysis::StageAnalysisResult& stageInfo,
                          std::unique_ptr<StubRewriter> rewriter,
                          const StubVerificationConfig& config);

    /// Run L2 verification for all candidate functions.
    StubVerificationResult verifyAll();

    /// Run L2 verification for a single function by qualified name.
    StubVerifyFunctionResult verifyFunction(const std::string& qualifiedName);

private:
    /// Identify candidate functions: non-entry-point stage callees.
    /// Entry points are functions that own logic blocks (they are the
    /// orchestrators, not the callees).
    std::vector<std::pair<std::string, int>> collectCandidates() const;

    /// Run build command, then test command.
    /// Returns true if both succeed.
    bool runBuildAndTest(std::string& error);

    /// Check if a function name matches the filter pattern (simple substring).
    bool matchesFilter(const std::string& funcName) const;

    const SymbolTable& symbols_;
    analysis::StageAnalysisResult stageInfo_;
    std::unique_ptr<StubRewriter> rewriter_;
    StubVerificationConfig config_;
};

} // namespace topo::check

#endif // TOPO_CHECK_STUBVERIFICATIONCHECK_H
