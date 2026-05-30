#ifndef TOPO_DISTRIBUTION_BACKENDCACHE_H
#define TOPO_DISTRIBUTION_BACKENDCACHE_H

/// Local backend cache management — the `~/.topo/backends/` tree.
///
/// Local cache layout:
///
///   ~/.topo/backends/
///     <backend>/
///       <version>/
///         topo-backend.toml
///         bin/  lib/  toolchain/  ...
///       default -> <version>
///     index.json
///
/// `<version>` directories are immutable once installed. Install is
/// transactional: a package is unpacked into a staging directory, verified,
/// and only on full success atomically renamed into place.

#include "topo/Distribution/BackendManifest.h"

#include <string>
#include <vector>

namespace topo::dist {

/// One installed `<backend>/<version>/` entry.
struct InstalledBackend {
    std::string name;
    std::string version;
    std::string path;       // absolute path of the <version> directory
    bool isDefault = false; // true when the `default` symlink points here
    BackendManifest manifest;
};

/// Outcome of a transactional install.
struct InstallOutcome {
    bool ok = false;
    int exitCode = 0;       // backend CLI exit-code table
    std::string error;
    std::string installedPath;  // <version> dir, set when ok
};

/// Manages one `~/.topo/backends/` cache root.
class BackendCache {
public:
    /// Use the default root: `$TOPO_HOME/backends` if TOPO_HOME is set,
    /// else `~/.topo/backends`. On failure to resolve a home directory the
    /// cache root is empty and operations report a filesystem error.
    BackendCache();

    /// Use an explicit cache root (test / `--cache-root` override).
    explicit BackendCache(std::string cacheRoot);

    const std::string& root() const { return root_; }
    bool rootResolved() const { return !root_.empty(); }

    /// Ensure the cache root directory exists. Returns false on failure.
    bool ensureRoot();

    /// Scan the cache and return every installed backend version.
    std::vector<InstalledBackend> listInstalled() const;

    /// All installed versions of one backend (may be empty).
    std::vector<InstalledBackend> versionsOf(const std::string& name) const;

    /// True when `<backend>/<version>/` exists.
    bool isInstalled(const std::string& name, const std::string& version) const;

    /// Transactionally install a package that has already been unpacked into
    /// `unpackedDir` (its root contains `topo-backend.toml`). Re-verifies the
    /// manifest, copies into a staging dir, and atomically renames into
    /// `<backend>/<version>/`. Any failure leaves the cache clean.
    ///
    /// `coreVersion` is the running topo-core version, checked against the
    /// manifest `core_compat`. `requireSignature` controls whether a missing
    /// or unverifiable signature aborts the install.
    InstallOutcome installFromUnpacked(const std::string& unpackedDir,
                                       const std::string& coreVersion,
                                       bool requireSignature);

    /// Remove one `<backend>/<version>/` tree. Returns false when absent or
    /// on filesystem error. Fixes up the `default` symlink if it dangled.
    bool removeVersion(const std::string& name, const std::string& version,
                       std::string& error);

    /// Remove every cached version of `name`. Returns false on error.
    bool removeBackend(const std::string& name, std::string& error);

    /// Point `<backend>/default` at `<version>`. Returns false when that
    /// version is not installed or on filesystem error.
    bool setDefault(const std::string& name, const std::string& version,
                    std::string& error);

    /// The version `<backend>/default` resolves to, or empty.
    std::string defaultVersion(const std::string& name) const;

    /// Path of the cached registry index snapshot (`<root>/index.json`).
    std::string indexPath() const;

private:
    std::string root_;
};

} // namespace topo::dist

#endif // TOPO_DISTRIBUTION_BACKENDCACHE_H
