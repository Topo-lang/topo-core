#include "topo/Analysis/ImportPathCheck.h"

#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace topo::analysis {

void checkImportPaths(const SymbolTable& symbols, const ImportPathConfig& config, check::CheckResult& result) {
    const auto& imports = symbols.imports();
    if (imports.empty()) return;

    // Build search directories list
    std::vector<fs::path> dirs;
    for (const auto& d : config.searchDirs) {
        fs::path p(d);
        if (fs::is_directory(p)) {
            dirs.push_back(p);
        }
    }
    // Always include project directory as fallback
    if (!config.projectDir.empty()) {
        fs::path projDir(config.projectDir);
        if (fs::is_directory(projDir)) {
            dirs.push_back(projDir);
        }
    }

    check::Severity severity = config.warnOnly ? check::Severity::Warning : check::Severity::Error;
    // If no search dirs at all, downgrade to warning
    if (dirs.empty()) {
        severity = check::Severity::Warning;
    }

    std::unordered_set<std::string> checked;
    for (const auto& entry : imports) {
        if (entry.path.empty()) continue;
        if (!checked.insert(entry.path).second) continue; // skip duplicates

        bool found = false;
        for (const auto& dir : dirs) {
            fs::path candidate = dir / entry.path;
            if (fs::exists(candidate)) {
                found = true;
                break;
            }
        }

        if (!found) {
            check::CheckDiagnostic diag;
            diag.severity = severity;
            diag.check = "import-path";
            diag.message = "std::import path '" + entry.path + "' not found";
            if (!dirs.empty()) {
                diag.message += " (searched: ";
                for (size_t i = 0; i < dirs.size(); ++i) {
                    if (i > 0) diag.message += ", ";
                    diag.message += dirs[i].string();
                }
                diag.message += ")";
            }
            diag.file = entry.location.file;
            diag.line = entry.location.line;
            diag.column = entry.location.column;
            result.addDiagnostic(std::move(diag));
        }
    }
}

} // namespace topo::analysis
