// PruningOrchestrator — L2 pruning-based stage isolation verifier.
//
// For each stage S (highest→lowest): stub all functions in stages < S,
// compile, run user tests, report isolation status, restore.

#include "topo/Check/PruningOrchestrator.h"
#include "topo/Platform/Process.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace topo::check {

namespace {

/// RAII guard that runs a restore action on every scope exit (normal return
/// or an in-flight exception thrown by build/test/IO). The stub→run→restore
/// window must never leave the user's working-tree source stubbed: a throw
/// between stub and restore would otherwise corrupt their source silently.
/// Disarmed once the explicit, return-value-checked restore has run.
class RestoreGuard {
public:
    explicit RestoreGuard(std::function<void()> restore) : restore_(std::move(restore)) {}
    ~RestoreGuard() {
        if (armed_ && restore_) restore_();
    }
    void disarm() { armed_ = false; }

    RestoreGuard(const RestoreGuard&) = delete;
    RestoreGuard& operator=(const RestoreGuard&) = delete;

private:
    std::function<void()> restore_;
    bool armed_ = true;
};

/// Split a command string into executable and arguments.
/// Handles simple quoting (double quotes only).
std::vector<std::string> splitCommand(const std::string& cmd) {
    std::vector<std::string> parts;
    std::string current;
    bool inQuote = false;

    for (char c : cmd) {
        if (c == '"') {
            inQuote = !inQuote;
        } else if (c == ' ' && !inQuote) {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

/// Extract the simple (unqualified) name from a qualified name.
std::string simpleName(const std::string& qualifiedName) {
    auto pos = qualifiedName.rfind("::");
    if (pos != std::string::npos) return qualifiedName.substr(pos + 2);
    return qualifiedName;
}

} // anonymous namespace

PruningOrchestrator::PruningOrchestrator(const analysis::StageAnalysisResult& stageInfo,
                                         std::unique_ptr<StubRewriter> rewriter,
                                         const PruningConfig& config)
    : stageInfo_(stageInfo), rewriter_(std::move(rewriter)), config_(config) {}

bool PruningOrchestrator::runBuildAndTest(std::string& error) {
    auto parts = splitCommand(config_.testCommand);
    if (parts.empty()) {
        error = "empty test command";
        return false;
    }

    std::string exe = parts[0];
    std::vector<std::string> args(parts.begin() + 1, parts.end());

    if (config_.verbose) {
        std::cerr << "  [run] " << config_.testCommand << "\n";
    }

    auto result = topo::platform::runProcessCapture(exe, args, config_.projectDir);

    if (result.exitCode != 0) {
        error = "command failed (exit " + std::to_string(result.exitCode) + ")";
        // Compiler errors and most test-runner detail go to stderr, not
        // stdout — surfacing stdout alone yields a near-empty, unactionable
        // diagnostic. Prefer stderr when stdout is empty, otherwise show both.
        std::string detail = result.stdoutOutput;
        if (!result.stderrOutput.empty()) {
            if (detail.empty()) {
                detail = result.stderrOutput;
            } else {
                detail += "\n[stderr]\n" + result.stderrOutput;
            }
        }
        if (!detail.empty()) {
            if (detail.size() > 500) {
                error += ": " + detail.substr(0, 500) + "...";
            } else {
                error += ": " + detail;
            }
        }
        return false;
    }

    return true;
}

std::unordered_map<int, std::unordered_set<std::string>> PruningOrchestrator::buildStageGroups() const {
    std::unordered_map<int, std::unordered_set<std::string>> groups;

    for (const auto& [qualifiedName, stage] : stageInfo_.calleeStageMap) {
        groups[stage].insert(simpleName(qualifiedName));
    }

    return groups;
}

StageIsolationResult PruningOrchestrator::verifyStage(int targetStage) {
    StageIsolationResult result;
    result.stage = targetStage;

    auto stageGroups = buildStageGroups();

    // Collect all functions in stages < targetStage to stub
    std::unordered_set<std::string> toStub;
    for (const auto& [stage, funcs] : stageGroups) {
        if (stage < targetStage) {
            toStub.insert(funcs.begin(), funcs.end());
        }
    }

    // Collect live functions (stage >= targetStage)
    for (const auto& [stage, funcs] : stageGroups) {
        if (stage >= targetStage) {
            for (const auto& f : funcs) {
                result.liveFunctions.push_back(f);
            }
        }
    }

    if (toStub.empty()) {
        // Nothing to stub — stage passes trivially (no lower stages)
        result.compileSuccess = true;
        result.testSuccess = true;
        return result;
    }

    if (config_.verbose) {
        std::cerr << "  [prune] stage " << targetStage << ": stubbing " << toStub.size()
                  << " functions from lower stages\n";
    }

    // Apply stubs
    auto rewriteResult = rewriter_->stubFunctions(toStub);
    if (!rewriteResult.success) {
        result.error = rewriteResult.error;
        return result;
    }

    result.stubbedFunctions = rewriteResult.stubbedFunctions;

    // RAII: guarantee every stubbed file is restored on EVERY exit path —
    // including an exception thrown by runBuildAndTest / IO between here and
    // the explicit restore. Without this, a throw leaves the user's source
    // stubbed. Disarmed after the explicit, return-value-checked restore.
    RestoreGuard guard([&] { rewriter_->restore(rewriteResult); });

    // Run build + test
    std::string buildError;
    result.testSuccess = runBuildAndTest(buildError);
    // Single combined build+test command: compile and test failures are not
    // separable, so derive compileSuccess from the run result rather than
    // hardcoding true (a stub-induced compile error must not be reported as a
    // successful compile).
    result.compileSuccess = result.testSuccess;

    if (!result.testSuccess) {
        result.error = buildError;
    }

    // Explicit, return-value-checked restore on the normal path; disarm the
    // guard so the destructor does not re-write the files.
    bool restoreOk = rewriter_->restore(rewriteResult);
    guard.disarm();
    if (!restoreOk) {
        // Restore failed — the user's source is left stubbed. Surface it as a
        // hard failure instead of silently continuing, and mark the result
        // inconclusive (passed() requires both compile and test success).
        result.compileSuccess = false;
        result.testSuccess = false;
        std::string note = "failed to restore source file(s) after stubbing stage < " +
                           std::to_string(targetStage) + "; working-tree source may be modified";
        result.error = result.error.empty() ? note : (result.error + "; " + note);
        std::cerr << "error: " << note << "\n";
    }

    return result;
}

PruningResult PruningOrchestrator::verifyAllStages() {
    PruningResult result;

    auto stageGroups = buildStageGroups();

    // Collect all stage numbers and sort descending (highest first)
    std::set<int, std::greater<int>> stageNumbers;
    for (const auto& [stage, _] : stageGroups) {
        stageNumbers.insert(stage);
    }

    if (config_.verbose) {
        std::cerr << "[pruning] verifying " << stageNumbers.size() << " stages (highest→lowest)\n";
    }

    for (int stage : stageNumbers) {
        if (config_.verbose) {
            std::cerr << "\n[pruning] === stage " << stage << " ===\n";
        }

        auto stageResult = verifyStage(stage);
        if (stageResult.passed()) {
            ++result.passCount;
        } else {
            ++result.failCount;
        }
        result.stages.push_back(std::move(stageResult));
    }

    return result;
}

} // namespace topo::check
