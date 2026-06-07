#ifndef TOPO_BUILD_INCREMENTALCACHE_H
#define TOPO_BUILD_INCREMENTALCACHE_H

#include "topo/Sema/VisibilityCollector.h"
#include "topo/Sema/SymbolTable.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace topo::build {

// Forward declaration (avoid pulling in full Config.h here)
struct BuildConfig;

/// Persistent state stored in .topo-cache/manifest.json.
struct CacheManifest {
    // v3: pre-existing on-disk manifests were written before the build
    // actually compared the config fingerprint on a cache hit. Bumping the
    // version invalidates those once, so a stale frontend cache cannot be
    // reused across a config change that the old (uncompared) fingerprint
    // never guarded against. Stays the single source of truth — the loader's
    // CACHE_VERSION and this default must match.
    int version = 3;
    std::string configFingerprint;                 // compile-config hash
    std::map<std::string, int64_t> topoFileMtimes; // .topo path -> mtime
    int64_t topoTomlMtime = 0;                     // Topo.toml mtime
};

/// Manages the project-level .topo-cache/ directory for incremental builds.
class IncrementalCache {
public:
    explicit IncrementalCache(const std::filesystem::path& projectDir);

    /// Root cache directory (.topo-cache/).
    std::filesystem::path cacheDir() const;

    /// IR output directory (.topo-cache/ir/).
    std::filesystem::path irDir() const;

    // --- Manifest ---

    bool loadManifest(CacheManifest& out) const;
    /// Returns false if the manifest stream could not be opened or the write
    /// failed — consistent with saveSymbolTable/saveVisibilityEntries, so the
    /// caller never reports a successful cache write when the stream failed.
    bool saveManifest(const CacheManifest& m) const;

    // --- Validity checks ---

    /// Check if .topo frontend cache (symbols + visibility) is still valid.
    /// Returns true when all .topo file mtimes match and Topo.toml mtime matches.
    bool isTopoFrontendValid(const CacheManifest& m, const std::vector<std::string>& topoPaths) const;

    /// Check if compile config fingerprint matches.
    bool isCompileConfigValid(const CacheManifest& m, const BuildConfig& cfg) const;

    // --- SymbolTable + VisibilityEntries serialization ---

    bool saveSymbolTable(const SymbolTable& symbols) const;
    bool loadSymbolTable(SymbolTable& symbols) const;

    bool saveVisibilityEntries(const std::vector<VisibilityEntry>& v) const;
    bool loadVisibilityEntries(std::vector<VisibilityEntry>& v) const;

    // --- Config fingerprint ---

    /// Compute a fingerprint of compile-affecting config fields.
    /// Includes: hostCompilerPath, standard, includeDirs, sources, embedIR,
    /// adaptiveCfg.isEnabled(), outputType. Excludes optLevel (only affects Step 6).
    static std::string computeConfigFingerprint(const BuildConfig& cfg);

    /// Remove all cache contents.
    void clean() const;

    /// Ensure .topo-cache/ and ir/ subdirectories exist.
    void ensureDirectories() const;

    /// Get mtime of a file as int64_t (0 if not found).
    static int64_t getFileMtime(const std::filesystem::path& path);

private:
    std::filesystem::path projectDir_;
};

} // namespace topo::build

#endif // TOPO_BUILD_INCREMENTALCACHE_H
