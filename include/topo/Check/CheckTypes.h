#ifndef TOPO_CHECK_CHECKTYPES_H
#define TOPO_CHECK_CHECKTYPES_H

#include <string>
#include <vector>

namespace topo::check {

/// Severity level for check diagnostics.
enum class Severity { Error, Warning, Note };

/// A single diagnostic from a check.
struct CheckDiagnostic {
    Severity severity = Severity::Error;
    std::string check; // e.g. "stage-isolation", "visibility", "purity"
    std::string message;
    std::string file;
    int line = 0;
    int column = 0;
};

static constexpr int kMaxCheckDiagnostics = 1000;

/// Result of running all checks.
struct CheckResult {
    std::vector<CheckDiagnostic> diagnostics;
    int errorCount = 0;
    int warningCount = 0;
    bool truncated = false;

    void addDiagnostic(CheckDiagnostic diag) {
        // Always count severity — even when truncating diagnostic storage
        if (diag.severity == Severity::Error)
            ++errorCount;
        else if (diag.severity == Severity::Warning)
            ++warningCount;
        if (static_cast<int>(diagnostics.size()) >= kMaxCheckDiagnostics) {
            truncated = true;
            return;
        }
        diagnostics.push_back(std::move(diag));
    }

    bool passed() const { return errorCount == 0; }
};

} // namespace topo::check

#endif // TOPO_CHECK_CHECKTYPES_H
