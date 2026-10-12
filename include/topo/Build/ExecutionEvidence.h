#ifndef TOPO_BUILD_EXECUTIONEVIDENCE_H
#define TOPO_BUILD_EXECUTIONEVIDENCE_H

#include "topo/Build/ArtifactEvidence.h"
#include "topo/Platform/Process.h"
#include <mutex>

namespace topo::build::evidence {

// Observes direct process dispatch and explicit inputs/outputs. It deliberately
// records incomplete coverage: compiler child processes, undeclared environment
// and transitive file reads require a separate controlled-build collector.
class ExecutionRecorder {
public:
    ExecutionRecorder(RootMap roots, std::filesystem::path manifestPath, std::string producer);
    // Output directories expand to their actual regular files after execution
    // (for javac's generated nested/anonymous classes). Symlink directories are
    // rejected; the record continues to report incomplete observation coverage.
    platform::ProcessResult run(const std::string& kind, const std::string& executable,
                                const std::vector<std::string>& argv,
                                const std::vector<std::string>& inputs,
                                const std::vector<std::string>& outputs, bool verbose = false);
    bool flush(std::string& error) const;
    Manifest manifest() const;
    std::vector<std::string> diagnostics() const;
private:
    bool snapshot(const std::string& path, FileEntry& entry, std::string& error) const;
    RootMap roots_;
    std::filesystem::path manifestPath_;
    std::string producer_;
    mutable std::mutex mutex_;
    Manifest manifest_;
    std::vector<std::string> diagnostics_;
};

} // namespace topo::build::evidence
#endif
