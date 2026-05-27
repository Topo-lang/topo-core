// PruningOrchestrator — L2 pruning-based stage isolation verifier.
//
// For each stage S (highest→lowest): stub all functions in stages < S,
// compile, run user tests, report isolation status, restore.

#include "topo/Check/PruningOrchestrator.h"
#include "topo/Platform/Process.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>

namespace topo::check {

namespace {

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

PruningOrchestrator::PruningOrchestrator(const SymbolTable& symbols,
                                         const analysis::StageAnalysisResult& stageInfo,
                                         std::unique_ptr<StubRewriter> rewriter,
                                         const PruningConfig& config)
    : symbols_(symbols), stageInfo_(stageInfo), rewriter_(std::move(rewriter)), config_(config) {}

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
        if (!result.stdoutOutput.empty()) {
            if (result.stdoutOutput.size() > 500) {
                error += ": " + result.stdoutOutput.substr(0, 500) + "...";
            } else {
                error += ": " + result.stdoutOutput;
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

    // Run build + test
    std::string buildError;
    result.compileSuccess = true;
    result.testSuccess = runBuildAndTest(buildError);

    if (!result.testSuccess) {
        result.error = buildError;
    }

    // Restore
    rewriter_->restore(rewriteResult);

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
