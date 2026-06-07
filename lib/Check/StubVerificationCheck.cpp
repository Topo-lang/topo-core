// StubVerificationCheck — L2 stub replacement verification.
//
// For each non-entry-point stage callee: stub it alone, compile,
// run user tests, report isolation status, restore. This catches
// hidden dependencies that L1 static analysis cannot detect.

#include "topo/Check/StubVerificationCheck.h"
#include "topo/Platform/Process.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <sstream>
#include <utility>

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

/// Run a single command, return true on exit code 0.
bool runCommand(const std::string& cmd, const std::string& workDir,
                bool verbose, std::string& error) {
    auto parts = splitCommand(cmd);
    if (parts.empty()) {
        error = "empty command";
        return false;
    }

    std::string exe = parts[0];
    std::vector<std::string> args(parts.begin() + 1, parts.end());

    if (verbose) {
        std::cerr << "    [run] " << cmd << "\n";
    }

    auto result = topo::platform::runProcessCapture(exe, args, workDir);

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

} // anonymous namespace

StubVerificationCheck::StubVerificationCheck(const SymbolTable& symbols,
                                             const analysis::StageAnalysisResult& stageInfo,
                                             std::unique_ptr<StubRewriter> rewriter,
                                             const StubVerificationConfig& config)
    : symbols_(symbols), stageInfo_(stageInfo), rewriter_(std::move(rewriter)), config_(config) {}

std::vector<std::pair<std::string, int>> StubVerificationCheck::collectCandidates() const {
    std::vector<std::pair<std::string, int>> candidates;

    // Entry points are functions that own logic blocks — they are orchestrators.
    // Callees referenced inside logic blocks are the verification targets.
    std::unordered_set<std::string> entryPoints;
    for (const auto& [blockName, block] : symbols_.logicBlocks()) {
        entryPoints.insert(blockName);
    }

    for (const auto& [qualifiedName, stage] : stageInfo_.calleeStageMap) {
        // Skip entry points (logic block owners)
        if (entryPoints.count(qualifiedName)) continue;

        // Skip assignment entries (prefixed with "<assign:")
        if (qualifiedName.find("<assign:") != std::string::npos) continue;

        if (matchesFilter(qualifiedName)) {
            candidates.emplace_back(qualifiedName, stage);
        }
    }

    // Sort by stage (ascending), then by name for deterministic ordering
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) {
                  if (a.second != b.second) return a.second < b.second;
                  return a.first < b.first;
              });

    return candidates;
}

bool StubVerificationCheck::matchesFilter(const std::string& funcName) const {
    if (config_.filterPattern.empty()) return true;
    return funcName.find(config_.filterPattern) != std::string::npos;
}

bool StubVerificationCheck::runBuildAndTest(std::string& error, bool& compileFailed) {
    compileFailed = false;

    // Run build command first (if provided). Its exit code authoritatively
    // tells compile vs test apart, so report the phase via compileFailed
    // rather than relying on the caller to string-match the message.
    if (!config_.buildCommand.empty()) {
        std::string buildError;
        if (!runCommand(config_.buildCommand, config_.projectDir, config_.verbose, buildError)) {
            error = "build failed: " + buildError;
            compileFailed = true;
            return false;
        }
    }

    // Run test command
    std::string testError;
    if (!runCommand(config_.testCommand, config_.projectDir, config_.verbose, testError)) {
        error = "test failed: " + testError;
        return false;
    }

    return true;
}

StubVerifyFunctionResult StubVerificationCheck::verifyFunction(const std::string& qualifiedName) {
    StubVerifyFunctionResult result;
    result.functionName = qualifiedName;

    // Look up stage
    auto it = stageInfo_.calleeStageMap.find(qualifiedName);
    if (it != stageInfo_.calleeStageMap.end()) {
        result.stage = it->second;
    }

    std::string simple = simpleName(qualifiedName);

    if (config_.verbose) {
        std::cerr << "  [l2-stub] " << qualifiedName << " (stage " << result.stage << ")\n";
    }

    // Stub this single function
    std::unordered_set<std::string> toStub = {simple};
    auto rewriteResult = rewriter_->stubFunctions(toStub);

    if (!rewriteResult.success) {
        result.error = "stub failed: " + rewriteResult.error;
        return result;
    }

    if (rewriteResult.stubbedFunctions.empty()) {
        // Function not found in any source file — skip. Nothing was written
        // (originalContents is empty), but still check the restore return so
        // an unexpected failure is not silently swallowed.
        result.error = "function not found in source files";
        if (!rewriter_->restore(rewriteResult)) {
            std::cerr << "error: failed to restore source after no-op stub of "
                      << qualifiedName << "\n";
        }
        return result;
    }

    // RAII: guarantee the stubbed source is restored on EVERY exit path —
    // including an exception thrown by runBuildAndTest / IO between here and
    // the explicit restore. Without this, a throw leaves the user's source
    // stubbed. Disarmed after the explicit, return-value-checked restore.
    RestoreGuard guard([&] { rewriter_->restore(rewriteResult); });

    // Run build + test
    std::string buildTestError;
    bool compileFailed = false;
    bool ok = runBuildAndTest(buildTestError, compileFailed);

    if (ok) {
        result.compileSuccess = true;
        result.testSuccess = true;
    } else if (compileFailed) {
        // Build step failed — classified from its actual exit code (not a
        // string match), so a stub-induced compile error is reported as a
        // compile failure rather than a test failure.
        result.compileSuccess = false;
        result.error = buildTestError;
    } else {
        // Compilation succeeded; the test step failed (hidden dependency).
        result.compileSuccess = true;
        result.testSuccess = false;
        result.error = buildTestError;
    }

    // Explicit, return-value-checked restore on the normal path; disarm the
    // guard so the destructor does not re-write the source.
    bool restoreOk = rewriter_->restore(rewriteResult);
    guard.disarm();
    if (!restoreOk) {
        // Restore failed — the user's source is left stubbed. Surface it as a
        // hard failure instead of silently continuing, and mark the result
        // inconclusive (passed() requires both compile and test success).
        result.compileSuccess = false;
        result.testSuccess = false;
        std::string note = "failed to restore source after stubbing " + qualifiedName +
                           "; working-tree source may be modified";
        result.error = result.error.empty() ? note : (result.error + "; " + note);
        std::cerr << "error: " << note << "\n";
    }

    return result;
}

StubVerificationResult StubVerificationCheck::verifyAll() {
    StubVerificationResult result;

    auto candidates = collectCandidates();

    if (config_.verbose) {
        std::cerr << "[l2-verify] " << candidates.size() << " candidate function(s) to verify\n";
    }

    if (candidates.empty()) {
        return result;
    }

    for (const auto& [qualifiedName, stage] : candidates) {
        auto funcResult = verifyFunction(qualifiedName);

        if (funcResult.error == "function not found in source files") {
            ++result.skipCount;
            if (config_.verbose) {
                std::cerr << "    [skip] " << qualifiedName << " — not found in source\n";
            }
        } else if (funcResult.passed()) {
            ++result.passCount;
            if (config_.verbose) {
                std::cerr << "    [pass] " << qualifiedName << "\n";
            }
        } else {
            ++result.failCount;
            if (config_.verbose) {
                std::cerr << "    [FAIL] " << qualifiedName << " — " << funcResult.error << "\n";
            }
        }

        result.functions.push_back(std::move(funcResult));
    }

    if (!candidates.empty()) {
        std::cerr << "[l2-verify] result: " << result.passCount << " passed, "
                  << result.failCount << " failed, "
                  << result.skipCount << " skipped (not found in source)\n";
    }

    return result;
}

} // namespace topo::check
