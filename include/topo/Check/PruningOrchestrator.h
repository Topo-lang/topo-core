#ifndef TOPO_CHECK_PRUNINGORCHESTRATOR_H
#define TOPO_CHECK_PRUNINGORCHESTRATOR_H

#include "topo/Check/StubRewriter.h"
#include "topo/Analysis/StageAnalysis.h"
#include "topo/Sema/SymbolTable.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace topo::check {

/// Result of verifying isolation for a single stage.
struct StageIsolationResult {
    int stage = 0;
    bool compileSuccess = false;
    bool testSuccess = false;
    std::string error;

    /// Functions that were stubbed (all functions in stages < this stage).
    std::vector<std::string> stubbedFunctions;

    /// Functions that remained live (in this stage and above).
    std::vector<std::string> liveFunctions;

    bool passed() const { return compileSuccess && testSuccess; }
};

/// Aggregated result of pruning-based verification across all stages.
struct PruningResult {
    std::vector<StageIsolationResult> stages;
    int passCount = 0;
    int failCount = 0;

    bool passed() const { return failCount == 0; }
};

/// Configuration for pruning-based verification.
struct PruningConfig {
    std::string projectDir;
    std::string testCommand; // e.g., "ctest -R unit"
    std::vector<std::string> sourceFiles;
    bool verbose = false;
};

/// Pruning-based stage isolation verifier (L2 verification).
///
/// For each stage S (highest to lowest):
///   1. Stub all functions in stages < S
///   2. Compile the project
///   3. Run user tests
///   4. Report whether stage S is properly isolated
///   5. Restore all stubbed files
///
/// This is a stronger guarantee than L1 single-function stubbing:
/// it proves that a stage's tests depend only on its own stage and
/// higher stages, not on lower-stage implementation details.
class PruningOrchestrator {
public:
    PruningOrchestrator(const SymbolTable& symbols,
                        const analysis::StageAnalysisResult& stageInfo,
                        std::unique_ptr<StubRewriter> rewriter,
                        const PruningConfig& config);

    /// Run pruning verification for all stages.
    /// Iterates from highest stage to lowest.
    PruningResult verifyAllStages();

    /// Run pruning verification for a single stage.
    /// Stubs all functions in stages < targetStage.
    StageIsolationResult verifyStage(int targetStage);

private:
    /// Run build + test with current source state.
    bool runBuildAndTest(std::string& error);

    /// Collect functions grouped by stage number.
    /// Key: stage number, Value: set of simple function names.
    std::unordered_map<int, std::unordered_set<std::string>> buildStageGroups() const;

    const SymbolTable& symbols_;
    analysis::StageAnalysisResult stageInfo_;
    std::unique_ptr<StubRewriter> rewriter_;
    PruningConfig config_;
};

} // namespace topo::check

#endif // TOPO_CHECK_PRUNINGORCHESTRATOR_H
