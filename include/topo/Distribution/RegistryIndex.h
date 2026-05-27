#ifndef TOPO_DISTRIBUTION_REGISTRYINDEX_H
#define TOPO_DISTRIBUTION_REGISTRYINDEX_H

/// In-memory model + parser for the registry `index.json`.
///
/// The index is a flat snapshot of what a registry holds; the CLI fetches
/// it once per `list --available` / `install` / `update` and caches it.

#include <string>
#include <vector>

namespace topo::dist {

/// One per-platform artifact of one backend version.
struct IndexArtifact {
    std::string platform;  // "<os>-<arch>", e.g. "macos-aarch64"
    std::string path;      // registry-root-relative archive path
    std::string sha256;    // archive digest, lowercase hex
};

/// One backend version with all its platform artifacts.
struct IndexVersion {
    std::string version;
    std::string coreCompat;  // duplicated from the manifest for pre-filtering
    std::vector<IndexArtifact> artifacts;
};

/// One backend across all its versions.
struct IndexBackend {
    std::string name;
    std::vector<std::string> language;
    std::vector<IndexVersion> versions;
};

/// A parsed registry index (schema 1).
struct RegistryIndex {
    int schema = 0;
    std::vector<IndexBackend> backends;

    /// Find a backend by name. Returns nullptr when absent.
    const IndexBackend* findBackend(const std::string& name) const;
};

struct IndexParseResult {
    bool ok = false;
    RegistryIndex index;
    std::string error;
};

/// Parse an `index.json` from an in-memory string.
IndexParseResult parseRegistryIndex(const std::string& jsonText,
                                    const std::string& sourceLabel);

/// Parse an `index.json` from a file path.
IndexParseResult parseRegistryIndexFile(const std::string& path);

} // namespace topo::dist

#endif // TOPO_DISTRIBUTION_REGISTRYINDEX_H
