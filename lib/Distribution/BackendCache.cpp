#include "topo/Distribution/BackendCache.h"

#include "topo/Distribution/SemVer.h"
#include "topo/Platform/TempFile.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <system_error>

namespace fs = std::filesystem;

namespace topo::dist {

namespace {

/// Resolve the user home directory cross-platform. Empty on failure.
/// `TOPO_HOME` overrides the OS-level env vars (documented escape hatch
/// for sandboxed CI). Everything else delegates to the shared platform
/// helper so the Windows USERPROFILE / HOMEDRIVE+HOMEPATH fallbacks
/// stay in one place.
std::string homeDir() {
    if (const char* h = std::getenv("TOPO_HOME"); h && *h) return h;
    fs::path p = topo::platform::homeDirectory();
    return p.empty() ? "" : p.string();
}

/// Default cache root: `$TOPO_HOME/backends` or `~/.topo/backends`.
std::string defaultRoot() {
    if (const char* th = std::getenv("TOPO_HOME"); th && *th)
        return (fs::path(th) / "backends").string();
    std::string home = homeDir();
    if (home.empty()) return "";
    return (fs::path(home) / ".topo" / "backends").string();
}

/// A unique staging directory name inside the cache root.
std::string stagingName() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    return ".staging-" + std::to_string(gen());
}

} // namespace

BackendCache::BackendCache() : root_(defaultRoot()) {}

BackendCache::BackendCache(std::string cacheRoot) : root_(std::move(cacheRoot)) {}

bool BackendCache::ensureRoot() {
    if (root_.empty()) return false;
    std::error_code ec;
    fs::create_directories(root_, ec);
    return !ec;
}

std::string BackendCache::indexPath() const {
    if (root_.empty()) return "";
    return (fs::path(root_) / "index.json").string();
}

std::vector<InstalledBackend> BackendCache::listInstalled() const {
    std::vector<InstalledBackend> out;
    if (root_.empty()) return out;
    std::error_code ec;
    if (!fs::is_directory(root_, ec)) return out;

    for (const auto& backendEntry : fs::directory_iterator(root_, ec)) {
        if (ec) break;
        if (!backendEntry.is_directory()) continue;
        std::string backendName = backendEntry.path().filename().string();
        if (!backendName.empty() && backendName[0] == '.') continue;  // staging

        std::string defaultVer = defaultVersion(backendName);
        for (const auto& verEntry : fs::directory_iterator(backendEntry.path(), ec)) {
            if (ec) break;
            std::string verName = verEntry.path().filename().string();
            // Skip the `default` symlink and any non-directory.
            if (verName == "default") continue;
            if (!verEntry.is_directory()) continue;

            InstalledBackend ib;
            ib.name = backendName;
            ib.version = verName;
            ib.path = verEntry.path().string();
            ib.isDefault = (verName == defaultVer);

            fs::path manifestPath = verEntry.path() / "topo-backend.toml";
            if (fs::exists(manifestPath)) {
                auto parsed = parseBackendManifestFile(manifestPath.string());
                if (parsed.ok) ib.manifest = std::move(parsed.manifest);
            }
            out.push_back(std::move(ib));
        }
    }
    return out;
}

std::vector<InstalledBackend> BackendCache::versionsOf(const std::string& name) const {
    std::vector<InstalledBackend> out;
    for (auto& ib : listInstalled()) {
        if (ib.name == name) out.push_back(std::move(ib));
    }
    return out;
}

bool BackendCache::isInstalled(const std::string& name,
                               const std::string& version) const {
    if (root_.empty()) return false;
    std::error_code ec;
    return fs::is_directory(fs::path(root_) / name / version, ec);
}

std::string BackendCache::defaultVersion(const std::string& name) const {
    if (root_.empty()) return "";
    fs::path link = fs::path(root_) / name / "default";
    std::error_code ec;
    if (!fs::is_symlink(link, ec)) {
        // Tolerate a plain-file fallback (Windows without symlink privilege):
        // a `default` regular file whose contents name the version.
        if (fs::is_regular_file(link, ec)) {
            std::ifstream in(link);
            std::string v;
            std::getline(in, v);
            return v;
        }
        return "";
    }
    fs::path target = fs::read_symlink(link, ec);
    if (ec) return "";
    return target.filename().string();
}

InstallOutcome BackendCache::installFromUnpacked(const std::string& unpackedDir,
                                                 const std::string& coreVersion,
                                                 bool requireSignature) {
    InstallOutcome out;
    if (root_.empty()) {
        out.exitCode = 5;
        out.error = "cannot resolve a cache root (no HOME / TOPO_HOME)";
        return out;
    }

    fs::path manifestPath = fs::path(unpackedDir) / "topo-backend.toml";
    if (!fs::exists(manifestPath)) {
        out.exitCode = 1;
        out.error = "package has no topo-backend.toml at its root";
        return out;
    }
    auto parsed = parseBackendManifestFile(manifestPath.string());
    if (!parsed.ok) {
        out.exitCode = 1;
        out.error = "invalid manifest — " + parsed.error;
        return out;
    }
    const BackendManifest& m = parsed.manifest;

    // ── compatibility check (exit 4) ──────────────────────────────
    if (!satisfiesRange(coreVersion, m.backend.coreCompat)) {
        out.exitCode = 4;
        out.error = "backend " + m.backend.name + "@" + m.backend.version +
                    " requires topo-core " + m.backend.coreCompat +
                    " — running topo-core is " + coreVersion;
        return out;
    }

    // ── signature gate (exit 3) ───────────────────────────────────
    // Ed25519 authenticity verification is not yet wired (no crypto
    // dependency in topo-core). When a caller demands a verified signature
    // we must refuse rather than silently install — failing closed matches
    // the spec's "signature failure is non-negotiable" rule.
    if (requireSignature) {
        out.exitCode = 3;
        out.error = "Ed25519 signature verification is not yet implemented; "
                    "install refused under --require-signature";
        return out;
    }

    // ── payload presence check ────────────────────────────────────
    for (const auto& rel : m.payloadPaths()) {
        bool isDir = !rel.empty() && rel.back() == '/';
        fs::path p = fs::path(unpackedDir) / rel;
        std::error_code ec;
        if (isDir) {
            if (!fs::is_directory(p, ec)) {
                out.exitCode = 1;
                out.error = "manifest names directory '" + rel +
                            "' but it is absent from the package";
                return out;
            }
        } else if (!fs::exists(p, ec)) {
            out.exitCode = 1;
            out.error = "manifest names file '" + rel +
                        "' but it is absent from the package";
            return out;
        }
    }

    fs::path finalDir = fs::path(root_) / m.backend.name / m.backend.version;
    if (fs::exists(finalDir)) {
        // <version> directories are immutable — installing over one is a no-op
        // success so callers can be idempotent.
        out.ok = true;
        out.installedPath = finalDir.string();
        return out;
    }

    // ── transactional copy ────────────────────────────────────────
    std::error_code ec;
    fs::create_directories(fs::path(root_) / m.backend.name, ec);
    if (ec) {
        out.exitCode = 5;
        out.error = "cannot create backend directory: " + ec.message();
        return out;
    }

    fs::path staging = fs::path(root_) / stagingName();
    fs::copy(unpackedDir, staging,
             fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    if (ec) {
        fs::remove_all(staging, ec);
        out.exitCode = 5;
        out.error = "failed to stage package: " + ec.message();
        return out;
    }

    // Atomic rename into the final location.
    fs::rename(staging, finalDir, ec);
    if (ec) {
        // Cross-device rename can fail; fall back to copy + cleanup, still
        // never leaving a half-installed final dir.
        fs::copy(staging, finalDir,
                 fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        std::error_code rmEc;
        fs::remove_all(staging, rmEc);
        if (ec) {
            fs::remove_all(finalDir, rmEc);
            out.exitCode = 5;
            out.error = "failed to commit package into cache: " + ec.message();
            return out;
        }
    }

    out.ok = true;
    out.installedPath = finalDir.string();
    return out;
}

bool BackendCache::removeVersion(const std::string& name, const std::string& version,
                                 std::string& error) {
    if (root_.empty()) {
        error = "cannot resolve a cache root";
        return false;
    }
    fs::path verDir = fs::path(root_) / name / version;
    std::error_code ec;
    if (!fs::is_directory(verDir, ec)) {
        error = name + "@" + version + " is not installed";
        return false;
    }
    fs::remove_all(verDir, ec);
    if (ec) {
        error = "failed to remove " + verDir.string() + ": " + ec.message();
        return false;
    }
    // If the `default` symlink now dangles, drop it.
    if (defaultVersion(name) == version) {
        fs::remove(fs::path(root_) / name / "default", ec);
    }
    // Remove the now-empty backend directory.
    fs::path backendDir = fs::path(root_) / name;
    if (fs::is_directory(backendDir, ec) && fs::is_empty(backendDir, ec)) {
        fs::remove(backendDir, ec);
    }
    return true;
}

bool BackendCache::removeBackend(const std::string& name, std::string& error) {
    if (root_.empty()) {
        error = "cannot resolve a cache root";
        return false;
    }
    fs::path backendDir = fs::path(root_) / name;
    std::error_code ec;
    if (!fs::is_directory(backendDir, ec)) {
        error = name + " is not installed";
        return false;
    }
    fs::remove_all(backendDir, ec);
    if (ec) {
        error = "failed to remove " + backendDir.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool BackendCache::setDefault(const std::string& name, const std::string& version,
                              std::string& error) {
    if (!isInstalled(name, version)) {
        error = name + "@" + version + " is not installed";
        return false;
    }
    fs::path link = fs::path(root_) / name / "default";
    std::error_code ec;
    fs::remove(link, ec);  // tolerate absent
    fs::create_directory_symlink(version, link, ec);
    if (ec) {
        // Symlink creation can be denied (Windows without privilege); fall
        // back to a plain file naming the version. defaultVersion() reads
        // both forms.
        std::ofstream out(link);
        if (!out) {
            error = "cannot record the default version: " + ec.message();
            return false;
        }
        out << version << "\n";
    }
    return true;
}

} // namespace topo::dist
