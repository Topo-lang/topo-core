#include "topo/Build/ExecutionEvidence.h"
#include "topo/Distribution/Sha256.h"
#include "topo/Platform/ToolResolution.h"
#include "topo/Platform/TempFile.h"
#include <algorithm>
#include <cstdlib>

namespace topo::build::evidence {
namespace fs = std::filesystem;

namespace {
bool unsupportedRedirect(const fs::path& path, std::string& error) {
#ifdef _WIN32
    std::error_code ec;
    const auto absolute = fs::absolute(path, ec).lexically_normal();
    if (ec) { error = "cannot resolve observed file path"; return true; }
    auto component = absolute.root_path();
    for (const auto& part : absolute.relative_path()) {
        component /= part;
        const bool redirected = platform::isPathRedirect(component, ec);
        if (ec) { error = "cannot inspect observed path: " + component.string(); return true; }
        if (redirected) {
            error = "Windows reparse-point observation is unsupported: " + component.string();
            return true;
        }
    }
#else
    (void)path; (void)error;
#endif
    return false;
}

std::vector<std::string> expandFiles(const std::vector<std::string>& paths, std::vector<std::string>& errors) {
    std::vector<std::string> files;
    size_t count = 0;
    for (const auto& path : paths) {
        std::string error;
        if (unsupportedRedirect(path, error)) { errors.push_back(error); continue; }
        std::error_code ec;
        const bool redirected = platform::isPathRedirect(path, ec);
        if (ec) { errors.push_back("cannot inspect observed path: " + path); continue; }
        if (redirected && fs::is_directory(path, ec)) {
            errors.push_back("observed path is a symlink directory: " + path);
            continue;
        }
        const auto status = fs::symlink_status(path, ec);
        if (!ec && fs::is_directory(status)) {
            fs::recursive_directory_iterator cursor(path, ec), end;
            for (; !ec && cursor != end; cursor.increment(ec)) {
                if (++count > 100000) { errors.push_back("directory exceeds observation limit"); return files; }
                const bool redirectedEntry = platform::isPathRedirect(cursor->path(), ec);
                if (ec) { cursor.disable_recursion_pending(); break; }
#ifdef _WIN32
                if (redirectedEntry) {
                    cursor.disable_recursion_pending();
                    errors.push_back("Windows reparse-point observation is unsupported: " + cursor->path().string());
                    continue;
                }
#else
                if (redirectedEntry && cursor->is_directory(ec)) {
                    cursor.disable_recursion_pending();
                    errors.push_back("observed directory contains a symlink directory: " + cursor->path().string());
                    continue;
                }
#endif
                auto type = cursor->symlink_status(ec);
                if (ec) break;
                if (fs::is_directory(type)) continue;
                files.push_back(cursor->path().string());
            }
            if (ec) errors.push_back("cannot enumerate observed directory: " + path);
        } else files.push_back(path);
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}
}

ExecutionRecorder::ExecutionRecorder(RootMap roots, fs::path manifestPath, std::string producer)
    : roots_(std::move(roots)), manifestPath_(std::move(manifestPath)), producer_(std::move(producer)) {}

bool ExecutionRecorder::snapshot(const std::string& path, FileEntry& entry, std::string& error) const {
    if (unsupportedRedirect(path, error)) return false;
    std::error_code ec;
    auto absolute = fs::absolute(path, ec).lexically_normal();
    if (ec) { error = "cannot resolve observed file path"; return false; }
    // Normalize platform aliases such as /tmp -> /private/tmp, retaining the
    // final filename so snapshotFile still records a final symlink faithfully.
    auto parent = platform::canonicalPath(absolute.parent_path(), ec);
    if (ec) { error = "cannot resolve observed file parent: " + path; return false; }
    absolute = parent / absolute.filename();
    std::vector<std::pair<std::string, fs::path>> candidates;
    for (const auto& [id, root] : roots_) {
        auto canonical = platform::canonicalPath(root, ec);
        if (!ec) candidates.emplace_back(id, canonical);
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.second.native().size() > b.second.native().size();
    });
    for (const auto& [id, root] : candidates) {
        auto relative = absolute.lexically_relative(root).generic_string();
        if (!relative.empty() && relative != ".." && relative.rfind("../", 0) != 0 && relative.front() != '/')
            return snapshotFile(root, id, relative, entry, error);
    }
    error = "observed file is outside approved evidence roots: " + path;
    return false;
}

platform::ProcessResult ExecutionRecorder::run(const std::string& kind, const std::string& executable,
                                              const std::vector<std::string>& argv,
                                              const std::vector<std::string>& inputs,
                                              const std::vector<std::string>& outputs, bool verbose) {
    Record record;
    record.action.kind = kind;
    record.action.version = "1";
    record.action.argv = argv;
    record.action.cwd = fs::current_path().generic_string();
    record.action.parameters = {{"capture", "direct-command-observation"}, {"executable", executable},
                                {"coverage", "explicit-files-only"}};
    std::string resolved = platform::findSpawnableOnPath(executable);
    if (resolved.empty()) resolved = executable;
    auto toolDigest = dist::sha256File(resolved);
    record.action.toolIdentity = validDigest(toolDigest) ? "sha256:" + toolDigest : "unresolved:" + executable;
    for (const char* name : {"PATH", "CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH", "LIBRARY_PATH", "SDKROOT",
                             "MACOSX_DEPLOYMENT_TARGET", "LANG", "LC_ALL", "SOURCE_DATE_EPOCH",
                             "JAVA_HOME", "JDK_JAVA_OPTIONS", "JAVA_TOOL_OPTIONS", "JDK_JAVAC_OPTIONS",
                             "CLASSPATH", "_JAVA_OPTIONS"})
        if (const char* value = std::getenv(name)) record.action.environment[name] = value;
    std::vector<std::string> errors;
    const auto inputFiles = expandFiles(inputs, errors);
    for (const auto& path : inputFiles) {
        FileEntry entry; std::string error;
        if (snapshot(path, entry, error)) record.action.inputs.push_back(std::move(entry));
        else errors.push_back(error);
    }
    auto result = platform::runProcess(resolved, argv, verbose);
    for (const auto& input : record.action.inputs) {
        std::string error;
        if (!verifyFile(input, roots_, error)) errors.push_back("input changed during command: " + error);
    }
    if (expandFiles(inputs, errors) != inputFiles) errors.push_back("input file set changed during command");
    const auto outputFiles = expandFiles(outputs, errors);
    for (const auto& path : outputFiles) {
        FileEntry entry; std::string error;
        if (snapshot(path, entry, error)) record.outputs.push_back(std::move(entry));
        else errors.push_back(error);
    }
    if (dist::sha256File(resolved) != toolDigest) errors.push_back("tool changed during command execution");
    record.producer = {{"builder", producer_}, {"exitCode", std::to_string(result.exitCode)},
                       {"observation", "process-dispatch"}, {"isolation", "none"}};
    if (!errors.empty()) {
        nlohmann::json diagnostics = errors;
        record.producer["captureDiagnostics"] = diagnostics.dump();
    }
    record.complete = false;
    std::lock_guard<std::mutex> guard(mutex_);
    record.parents = manifest_.roots;
    sealRecord(record);
    if (record.id.empty()) diagnostics_.push_back("cannot encode command observation");
    else {
        manifest_.nodes.push_back(record);
        manifest_.roots = {record.id};
    }
    diagnostics_.insert(diagnostics_.end(), errors.begin(), errors.end());
    return result;
}

bool ExecutionRecorder::flush(std::string& error) const {
    std::lock_guard<std::mutex> guard(mutex_);
    return writeManifest(manifestPath_, manifest_, error);
}

Manifest ExecutionRecorder::manifest() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return manifest_;
}

std::vector<std::string> ExecutionRecorder::diagnostics() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return diagnostics_;
}

} // namespace topo::build::evidence
