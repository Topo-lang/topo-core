// VerificationOrchestrator — Code-stripping verification driver.
//
// For each check type (stage-isolation, pipeline-independence), the orchestrator:
// 1. Identifies functions to stub based on SymbolTable analysis
// 2. Stubs each function, runs build+test, records result, restores
// 3. Collects all results into a VerificationResult

#include "topo/Check/VerificationOrchestrator.h"
#include "topo/Platform/Process.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace topo::check {

namespace {

/// Split a command string into executable and arguments.
/// Handles simple quoting (double quotes only).
std::vector<std::string> splitCommand(const std::string& cmd) {
    std::vector<std::string> parts;
    std::string current;
    bool inQuote = false;

    for (size_t i = 0; i < cmd.size(); ++i) {
        char c = cmd[i];
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

/// Read a file to check if it contains a function name.
bool fileContainsFunction(const std::string& filePath, const std::string& funcName) {
    std::ifstream ifs(filePath);
    if (!ifs) return false;
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.find(funcName) != std::string::npos) return true;
    }
    return false;
}

/// Extract the simple (unqualified) name from a qualified name.
/// e.g., "engine::core::init" -> "init"
std::string simpleName(const std::string& qualifiedName) {
    auto pos = qualifiedName.rfind("::");
    if (pos != std::string::npos) return qualifiedName.substr(pos + 2);
    return qualifiedName;
}

} // anonymous namespace

VerificationOrchestrator::VerificationOrchestrator(const SymbolTable& symbols,
                                                   std::unique_ptr<StubGenerator> stubGen,
                                                   const VerificationConfig& config)
    : symbols_(symbols), stubGen_(std::move(stubGen)), config_(config) {}

bool VerificationOrchestrator::runBuildAndTest(std::string& error) {
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
            // Truncate output to avoid flooding
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

std::string VerificationOrchestrator::findSourceFileForFunction(const std::string& funcName) const {
    std::string simple = simpleName(funcName);

    for (const auto& srcFile : config_.sourceFiles) {
        if (fileContainsFunction(srcFile, simple)) {
            return srcFile;
        }
    }
    return {};
}

VerificationResult VerificationOrchestrator::verifyStageIsolation() {
    VerificationResult result;
    result.checkName = "stage-isolation";

    // For each logic block, group called functions by stage.
    // For each stage with 2+ functions, stub one and verify the others
    // can still compile and pass tests.
    for (const auto& [blockName, block] : symbols_.logicBlocks()) {
        if (block.isPipeline) continue; // pipeline has its own check
        if (block.calledFunctions.empty()) continue;

        // Group functions by stage
        std::map<int, std::vector<std::string>> stageToFunctions;
        for (size_t i = 0; i < block.calledFunctions.size(); ++i) {
            int stage = (i < block.stages.size()) ? block.stages[i] : 0;
            stageToFunctions[stage].push_back(block.calledFunctions[i]);
        }

        // For each stage with multiple functions, test isolation
        for (const auto& [stage, funcs] : stageToFunctions) {
            if (funcs.size() < 2) continue;

            for (const auto& stubFunc : funcs) {
                std::string srcFile = findSourceFileForFunction(stubFunc);
                if (srcFile.empty()) {
                    VerificationTestResult tr;
                    tr.stubbedFunction = stubFunc;
                    tr.checkedProperty = "stage " + std::to_string(stage) + " isolation in " + blockName;
                    tr.error = "source file not found for function: " + stubFunc;
                    result.tests.push_back(tr);
                    ++result.failCount;
                    continue;
                }

                std::string simple = simpleName(stubFunc);

                if (config_.verbose) {
                    std::cerr << "  [stub] " << stubFunc << " (stage " << stage << " in " << blockName << ")\n";
                }

                // Stub the function
                auto stubRes = stubGen_->stubFunction(srcFile, simple);
                if (!stubRes.success) {
                    VerificationTestResult tr;
                    tr.stubbedFunction = stubFunc;
                    tr.checkedProperty = "stage " + std::to_string(stage) + " isolation in " + blockName;
                    tr.error = stubRes.error;
                    result.tests.push_back(tr);
                    ++result.failCount;
                    continue;
                }

                // Run build + test
                VerificationTestResult tr;
                tr.stubbedFunction = stubFunc;
                tr.checkedProperty = "stage " + std::to_string(stage) + " isolation in " + blockName;

                std::string buildError;
                tr.compileSuccess = true; // assume compilation works
                tr.testSuccess = runBuildAndTest(buildError);

                if (!tr.testSuccess) {
                    // If tests fail after stubbing a same-stage peer, it means
                    // there's an unexpected dependency within the stage.
                    tr.error = buildError;
                    ++result.failCount;
                } else {
                    ++result.passCount;
                }

                result.tests.push_back(tr);

                // Restore
                stubGen_->restoreFile(srcFile, stubRes);
            }
        }
    }

    return result;
}

VerificationResult VerificationOrchestrator::verifyPipelineIndependence() {
    VerificationResult result;
    result.checkName = "pipeline-independence";

    // For each pipeline logic block, identify independent branches.
    // Stub all functions in one branch and verify other branches still work.
    for (const auto& [blockName, block] : symbols_.logicBlocks()) {
        if (!block.isPipeline) continue;
        if (block.calledFunctions.empty()) continue;

        // Build adjacency: which functions have edges between them
        std::set<std::string> allFuncs(block.calledFunctions.begin(), block.calledFunctions.end());
        std::map<std::string, std::set<std::string>> adj;
        for (const auto& edge : block.edges) {
            adj[edge.source].insert(edge.target);
            adj[edge.target].insert(edge.source);
        }

        // Find "source" nodes (no incoming edges) — these define branch roots
        std::set<std::string> hasIncoming;
        for (const auto& edge : block.edges) {
            hasIncoming.insert(edge.target);
        }

        std::vector<std::string> sourceNodes;
        for (const auto& f : block.calledFunctions) {
            if (hasIncoming.find(f) == hasIncoming.end()) {
                sourceNodes.push_back(f);
            }
        }

        // If fewer than 2 source nodes, there's only one branch — skip
        if (sourceNodes.size() < 2) continue;

        // For each source node (branch root), stub all functions reachable
        // from it and verify other branches still work
        for (const auto& branchRoot : sourceNodes) {
            // BFS to find all functions in this branch
            std::set<std::string> branchFuncs;
            std::vector<std::string> queue = {branchRoot};
            while (!queue.empty()) {
                std::string cur = queue.back();
                queue.pop_back();
                if (branchFuncs.count(cur)) continue;
                branchFuncs.insert(cur);
                for (const auto& edge : block.edges) {
                    if (edge.source == cur && !branchFuncs.count(edge.target)) {
                        queue.push_back(edge.target);
                    }
                }
            }

            if (config_.verbose) {
                std::cerr << "  [branch] stubbing branch from " << branchRoot << " (" << branchFuncs.size()
                          << " functions)\n";
            }

            // Stub all functions in this branch
            std::vector<std::pair<std::string, StubResult>> stubs;
            bool stubFailed = false;

            for (const auto& func : branchFuncs) {
                std::string srcFile = findSourceFileForFunction(func);
                if (srcFile.empty()) continue;

                std::string simple = simpleName(func);
                auto stubRes = stubGen_->stubFunction(srcFile, simple);
                if (stubRes.success) {
                    stubs.push_back({srcFile, stubRes});
                } else {
                    stubFailed = true;
                    // Restore any stubs already applied. Restore each file only
                    // once, from the FIRST stub recorded for it: its
                    // originalContent is the true whole-file original. Multiple
                    // branch functions can share a source file, and a later
                    // stub of that file captured already-stubbed content.
                    std::set<std::string> restored;
                    for (auto& [sf, sr] : stubs) {
                        if (!restored.insert(sf).second) continue;
                        stubGen_->restoreFile(sf, sr);
                    }
                    break;
                }
            }

            if (stubFailed) {
                VerificationTestResult tr;
                tr.stubbedFunction = branchRoot + " (branch)";
                tr.checkedProperty = "branch independence in " + blockName;
                tr.error = "failed to stub branch functions";
                result.tests.push_back(tr);
                ++result.failCount;
                continue;
            }

            // Run build + test
            VerificationTestResult tr;
            tr.stubbedFunction = branchRoot + " (branch)";
            tr.checkedProperty = "branch independence in " + blockName;

            std::string buildError;
            tr.compileSuccess = true;
            tr.testSuccess = runBuildAndTest(buildError);

            if (!tr.testSuccess) {
                tr.error = buildError;
                ++result.failCount;
            } else {
                ++result.passCount;
            }

            result.tests.push_back(tr);

            // Restore all stubs. Restore each file only once, from the FIRST
            // stub recorded for it: the stub generator captures whole-file
            // content per call, so a second function stubbed in the same file
            // captured already-stubbed content. Writing that snapshot back
            // would re-apply the first stub and corrupt the user's source.
            std::set<std::string> restored;
            for (auto& [sf, sr] : stubs) {
                if (!restored.insert(sf).second) continue;
                stubGen_->restoreFile(sf, sr);
            }
        }
    }

    return result;
}

std::vector<VerificationResult> VerificationOrchestrator::verifyAll() {
    std::vector<VerificationResult> results;
    results.push_back(verifyStageIsolation());
    results.push_back(verifyPipelineIndependence());
    return results;
}

} // namespace topo::check
