#ifndef TOPO_DEBUG_PASSREPORTSLOADER_H
#define TOPO_DEBUG_PASSREPORTSLOADER_H

// Loader for `<output>.topo-passes/*.json` sidecars.
//
// `topo debug query` calls `PassReportsRegistry::loadFromDirectory()` once
// per CLI invocation. The registry holds one PassReport per JSON file found
// in the directory; missing or malformed files are skipped (the registry
// simply lacks that Pass entry). Per principle 19, this code does not
// understand any Pass-specific detail schema — it just preserves the raw
// detail JSON for downstream consumers.

#include "topo/Debug/PassReports.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace topo::debug_meta {

class PassReportsRegistry {
public:
    PassReportsRegistry() = default;

    // Load every `<dir>/<PassName>.json` into the registry. Returns
    // std::nullopt with `err` populated only on irrecoverable directory-
    // level errors (path is not a directory, etc.). Individual unparseable
    // files are silently skipped — the goal is best-effort visibility.
    // If `dir` does not exist, returns an empty registry (no error) so the
    // caller can treat "build had no sidecars yet" the same as "no Pass
    // info available".
    static std::optional<PassReportsRegistry> loadFromDirectory(
        const std::filesystem::path& dir, std::string* err = nullptr);

    // Lookup by Pass name (e.g. "DataLayoutPass"). Returns nullptr if the
    // Pass is not in the registry.
    const PassReportHeader* findHeader(const std::string& passName) const;
    const nlohmann::json* findDetail(const std::string& passName) const;

    // Whole-Pass lookup; nullptr if absent.
    const PassReport* findReport(const std::string& passName) const;

    // Size + name iteration for diagnostics / tests.
    std::size_t size() const { return reports_.size(); }
    bool empty() const { return reports_.empty(); }
    const std::map<std::string, PassReport>& all() const { return reports_; }

private:
    std::map<std::string, PassReport> reports_;
};

} // namespace topo::debug_meta

#endif // TOPO_DEBUG_PASSREPORTSLOADER_H
