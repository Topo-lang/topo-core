#ifndef TOPO_BUILD_ARTIFACTEVIDENCE_H
#define TOPO_BUILD_ARTIFACTEVIDENCE_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace topo::build::evidence {

inline constexpr int SchemaVersion = 1;
using RootMap = std::map<std::string, std::filesystem::path>;

// Paths are portable, relative to a caller-approved root. Symlinks retain both
// their link text and the identity of the regular file consumed through them.
struct FileEntry {
    std::string root;
    std::string path;
    std::string kind = "file"; // file or symlink
    std::uint64_t size = 0;
    std::string sha256;
    bool executable = false;
    std::string symlinkTarget;
    std::string resolvedPath;
};

struct Action {
    std::string kind;
    std::string version;
    std::string toolIdentity;
    std::vector<std::string> argv;
    std::string cwd;
    std::map<std::string, std::string> environment;
    nlohmann::json parameters = nlohmann::json::object();
    std::vector<FileEntry> inputs;
};

struct Record {
    std::string id;
    std::string kind = "build-stage"; // build-stage, audit, cache, distribution
    Action action;
    std::vector<FileEntry> outputs;
    std::vector<FileEntry> metadata;
    std::vector<std::string> parents;
    // Producer identity is a declaration, authenticated separately by an
    // attestation. A self-reported manifest never establishes observed-build.
    std::map<std::string, std::string> producer;
    bool complete = false;
};

struct Manifest {
    int schemaVersion = SchemaVersion;
    std::vector<Record> nodes;
    std::vector<std::string> roots;
};

struct VerificationResult {
    bool integrity = false;
    bool complete = false;
    // These remain false for content-only verification.
    bool signerIdentity = false;
    bool buildEvidence = false;
    bool rebuildMatch = false;
    std::vector<std::string> diagnostics;
};

bool validDigest(const std::string& digest);
bool snapshotFile(const std::filesystem::path& root, const std::string& rootId,
                  const std::string& relativePath, FileEntry& out, std::string& error);
bool verifyFile(const FileEntry& entry, const RootMap& roots, std::string& error);
// Digests cover typed, versioned canonical JSON. Array order is preserved;
// files/parents are sorted only where their meaning is an unordered set.
std::string actionDigest(const Action& action);
std::string recordDigest(const Record& record);
std::string manifestDigest(const Manifest& manifest);
void sealRecord(Record& record);
nlohmann::json toJson(const Manifest& manifest);
bool fromJson(const nlohmann::json& json, Manifest& out, std::string& error);
bool writeManifest(const std::filesystem::path& path, const Manifest& manifest, std::string& error);
bool readManifest(const std::filesystem::path& path, Manifest& out, std::string& error,
                  std::uint64_t maxBytes = 16 * 1024 * 1024);
VerificationResult verifyManifest(const Manifest& manifest, const RootMap& roots);
// Integrity/identity helper only. Consumers of shared cache must additionally
// authenticate the enclosing manifest with their external trust policy.
VerificationResult verifyCacheReuse(const Record& record, const Action& expected,
                                    const RootMap& roots);

} // namespace topo::build::evidence
#endif
