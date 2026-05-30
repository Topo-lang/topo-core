#include "topo/Distribution/BackendCli.h"

#include "topo/Distribution/BackendCache.h"
#include "topo/Distribution/BackendManifest.h"
#include "topo/Distribution/RegistryIndex.h"
#include "topo/Distribution/SemVer.h"
#include "topo/Distribution/Sha256.h"
#include "topo/Platform/Platform.h"
#include "topo/Platform/Process.h"
#include "topo/Platform/TempFile.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace topo::dist {

namespace {

// ── exit codes (backend CLI exit-code table) ─────────────────────
// Code 4 (compatibility error) is produced inside BackendCache as
// InstallOutcome.exitCode and propagated verbatim, so it has no CLI-level
// constant here.
constexpr int kOk = 0;
constexpr int kUsageError = 1;
constexpr int kRegistryError = 2;
constexpr int kSignatureError = 3;
constexpr int kCacheError = 5;

/// The platform triple of the running build, e.g. "macos-aarch64".
std::string currentPlatform() {
    std::string os = platform::IsWindows  ? "windows"
                     : platform::IsMacOS  ? "macos"
                                          : "linux";
#if defined(__aarch64__) || defined(_M_ARM64)
    std::string arch = "aarch64";
#else
    std::string arch = "x86_64";
#endif
    return os + "-" + arch;
}

/// Split "name@version" → {name, version}. version is empty when absent.
std::pair<std::string, std::string> splitNameVersion(const std::string& spec) {
    auto at = spec.find('@');
    if (at == std::string::npos) return {spec, ""};
    return {spec.substr(0, at), spec.substr(at + 1)};
}

/// Pick the newest version string from a list (SemVer core order).
std::string newestVersion(const std::vector<std::string>& versions) {
    std::string best;
    SemVer bestV;
    for (const auto& v : versions) {
        SemVer sv = parseSemVer(v);
        if (!sv.valid) continue;
        if (best.empty() || sv.compareCore(bestV) > 0) {
            best = v;
            bestV = sv;
        }
    }
    return best;
}

/// Extract an archive (`.tar.zst`, `.tar.gz`, `.tar`) into `destDir` by
/// shelling out to `tar`. POSIX `tar` (macOS/Linux) and Windows 10+ bsdtar
/// both auto-detect compression. Returns false on failure.
bool extractArchive(const std::string& archive, const std::string& destDir,
                    std::string& error) {
    std::error_code ec;
    fs::create_directories(destDir, ec);
    if (ec) {
        error = "cannot create extraction directory: " + ec.message();
        return false;
    }
    auto res = platform::runProcessCapture(
        "tar", {"-xf", archive, "-C", destDir});
    if (res.exitCode != 0) {
        error = "tar extraction failed (exit " + std::to_string(res.exitCode) +
                "): " + res.stderrOutput;
        return false;
    }
    return true;
}

/// Locate the package root inside an extracted tree: the directory that
/// directly contains `topo-backend.toml`. Some archives wrap the package in a
/// single top-level directory; descend one level when needed.
std::string findPackageRoot(const std::string& extractedDir) {
    if (fs::exists(fs::path(extractedDir) / "topo-backend.toml"))
        return extractedDir;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(extractedDir, ec)) {
        if (e.is_directory() &&
            fs::exists(e.path() / "topo-backend.toml")) {
            return e.path().string();
        }
    }
    return "";
}

/// Resolve a source archive for `name`[@`version`] under a `--from` directory
/// laid out as the registry leaf tree. Returns the archive path, or empty.
std::string resolveFromDir(const std::string& fromDir, const std::string& name,
                           const std::string& version, const std::string& platformTriple,
                           std::string& resolvedVersion, std::string& error) {
    // Prefer an index.json fragment when present; otherwise scan the tree.
    fs::path idxPath = fs::path(fromDir) / "index.json";
    if (fs::exists(idxPath)) {
        auto idx = parseRegistryIndexFile(idxPath.string());
        if (!idx.ok) {
            error = idx.error;
            return "";
        }
        const IndexBackend* b = idx.index.findBackend(name);
        if (!b) {
            error = "backend '" + name + "' not found in " + idxPath.string();
            return "";
        }
        std::vector<std::string> candidates;
        for (const auto& v : b->versions) candidates.push_back(v.version);
        std::string pick = version.empty() ? newestVersion(candidates) : version;
        for (const auto& v : b->versions) {
            if (v.version != pick) continue;
            for (const auto& a : v.artifacts) {
                if (a.platform == platformTriple) {
                    resolvedVersion = pick;
                    return (fs::path(fromDir) / a.path).string();
                }
            }
            error = "no " + platformTriple + " artifact for " + name + "@" + pick;
            return "";
        }
        error = "version " + pick + " of " + name + " not found in " + idxPath.string();
        return "";
    }

    // No index: scan <fromDir>/<name>/<version>/ for a matching archive.
    fs::path backendDir = fs::path(fromDir) / name;
    std::error_code ec;
    if (!fs::is_directory(backendDir, ec)) {
        error = "no '" + name + "' directory or index.json under " + fromDir;
        return "";
    }
    std::vector<std::string> versionDirs;
    for (const auto& e : fs::directory_iterator(backendDir, ec)) {
        if (e.is_directory()) versionDirs.push_back(e.path().filename().string());
    }
    std::string pick = version.empty() ? newestVersion(versionDirs) : version;
    fs::path verDir = backendDir / pick;
    if (!fs::is_directory(verDir, ec)) {
        error = "version " + pick + " of " + name + " not found under " + fromDir;
        return "";
    }
    for (const auto& e : fs::directory_iterator(verDir, ec)) {
        std::string fn = e.path().filename().string();
        if (e.is_regular_file() && fn.find(platformTriple) != std::string::npos &&
            (platform::endsWith(fn, ".tar.zst") || platform::endsWith(fn, ".tar.gz") ||
             platform::endsWith(fn, ".tar"))) {
            resolvedVersion = pick;
            return e.path().string();
        }
    }
    error = "no " + platformTriple + " archive for " + name + "@" + pick + " under " + fromDir;
    return "";
}

/// A unique temp directory for staging extractions.
std::string makeTempDir(const std::string& tag) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    fs::path p = topo::platform::tempDirectory() /
                 ("topo-backend-" + tag + "-" + std::to_string(gen()));
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.string();
}

// ── option parsing helpers ───────────────────────────────────────

struct Options {
    std::string fromDir;
    std::string platformTriple;
    std::string cacheRoot;
    std::string outputDir;
    bool installedFlag = false;
    bool availableFlag = false;
    bool requireSignature = false;
    bool yes = false;  // skip confirmation
    std::vector<std::string> positionals;
    std::string parseError;
};

Options parseOptions(const std::vector<std::string>& args, std::size_t startAt) {
    Options o;
    for (std::size_t i = startAt; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto needValue = [&](const char* name) -> std::string {
            if (i + 1 >= args.size()) {
                o.parseError = std::string("option ") + name + " needs a value";
                return "";
            }
            return args[++i];
        };
        if (a == "--from") {
            o.fromDir = needValue("--from");
        } else if (a == "--platform") {
            o.platformTriple = needValue("--platform");
        } else if (a == "--cache-root") {
            o.cacheRoot = needValue("--cache-root");
        } else if (a == "-o" || a == "--output") {
            o.outputDir = needValue(a.c_str());
        } else if (a == "--installed") {
            o.installedFlag = true;
        } else if (a == "--available") {
            o.availableFlag = true;
        } else if (a == "--require-signature") {
            o.requireSignature = true;
        } else if (a == "--yes" || a == "-y") {
            o.yes = true;
        } else if (!a.empty() && a[0] == '-') {
            o.parseError = "unknown option '" + a + "'";
        } else {
            o.positionals.push_back(a);
        }
    }
    return o;
}

BackendCache makeCache(const Options& o) {
    return o.cacheRoot.empty() ? BackendCache() : BackendCache(o.cacheRoot);
}

// ── subcommands ──────────────────────────────────────────────────

int cmdList(const Options& o) {
    BackendCache cache = makeCache(o);
    bool wantAvailable = o.availableFlag;
    // --installed is the default; explicit --installed also fine.
    bool wantInstalled = o.installedFlag || !wantAvailable;

    if (wantInstalled) {
        auto installed = cache.listInstalled();
        std::cout << "Installed backends (" << cache.root() << "):\n";
        if (installed.empty()) {
            std::cout << "  (none)\n";
        } else {
            for (const auto& ib : installed) {
                std::cout << "  " << ib.name << "@" << ib.version
                          << (ib.isDefault ? "  [default]" : "");
                if (!ib.manifest.backend.language.empty()) {
                    std::cout << "  lang=";
                    for (std::size_t i = 0; i < ib.manifest.backend.language.size(); ++i)
                        std::cout << (i ? "," : "") << ib.manifest.backend.language[i];
                }
                std::cout << "\n";
            }
        }
    }

    if (wantAvailable) {
        std::string idxPath = cache.indexPath();
        if (idxPath.empty() || !fs::exists(idxPath)) {
            std::cerr << "error: no cached registry index at "
                      << (idxPath.empty() ? "<unresolved cache root>" : idxPath)
                      << "\n  network registry fetch is not yet implemented; "
                         "place an index.json there or use --from for offline installs\n";
            return kRegistryError;
        }
        auto idx = parseRegistryIndexFile(idxPath);
        if (!idx.ok) {
            std::cerr << "error: " << idx.error << "\n";
            return kRegistryError;
        }
        std::cout << "Available backends (registry index " << idxPath << "):\n";
        for (const auto& b : idx.index.backends) {
            std::cout << "  " << b.name << "\n";
            for (const auto& v : b.versions) {
                std::cout << "    " << v.version << "  core_compat="
                          << v.coreCompat << "\n";
            }
        }
    }
    return kOk;
}

int cmdInstall(const Options& o) {
    if (o.positionals.empty()) {
        std::cerr << "error: 'install' needs a backend name\n";
        return kUsageError;
    }
    auto [name, version] = splitNameVersion(o.positionals[0]);
    std::string platformTriple =
        o.platformTriple.empty() ? currentPlatform() : o.platformTriple;

    if (o.fromDir.empty()) {
        std::cerr << "error: network registry install is not yet implemented\n"
                  << "  use the offline path: 'topo backend pack' on a connected\n"
                  << "  machine, then 'topo backend install " << name
                  << " --from <dir>'\n";
        return kRegistryError;
    }

    std::string resolvedVersion;
    std::string err;
    std::string archive = resolveFromDir(o.fromDir, name, version, platformTriple,
                                         resolvedVersion, err);
    if (archive.empty()) {
        std::cerr << "error: " << err << "\n";
        return kRegistryError;
    }
    if (!fs::exists(archive)) {
        std::cerr << "error: archive not found: " << archive << "\n";
        return kRegistryError;
    }

    // Verify the archive digest if the --from index records one.
    {
        fs::path idxPath = fs::path(o.fromDir) / "index.json";
        if (fs::exists(idxPath)) {
            auto idx = parseRegistryIndexFile(idxPath.string());
            if (idx.ok) {
                if (const IndexBackend* b = idx.index.findBackend(name)) {
                    for (const auto& v : b->versions) {
                        if (v.version != resolvedVersion) continue;
                        for (const auto& a : v.artifacts) {
                            if (a.platform == platformTriple && !a.sha256.empty()) {
                                std::string actual = sha256File(archive);
                                if (actual != a.sha256) {
                                    std::cerr << "error: archive digest mismatch for "
                                              << name << "@" << resolvedVersion << "\n"
                                              << "  index sha256:  " << a.sha256 << "\n"
                                              << "  archive sha256: " << actual << "\n";
                                    return kSignatureError;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    std::string tmp = makeTempDir("install");
    std::string extractErr;
    if (!extractArchive(archive, tmp, extractErr)) {
        std::error_code ec;
        fs::remove_all(tmp, ec);
        std::cerr << "error: " << extractErr << "\n";
        return kCacheError;
    }
    std::string pkgRoot = findPackageRoot(tmp);
    if (pkgRoot.empty()) {
        std::error_code ec;
        fs::remove_all(tmp, ec);
        std::cerr << "error: archive contains no topo-backend.toml\n";
        return kUsageError;
    }

    BackendCache cache = makeCache(o);
    if (!cache.ensureRoot()) {
        std::error_code ec;
        fs::remove_all(tmp, ec);
        std::cerr << "error: cannot create the backend cache at "
                  << cache.root() << "\n";
        return kCacheError;
    }

    InstallOutcome outcome = cache.installFromUnpacked(pkgRoot, kTopoCoreVersion,
                                                       o.requireSignature);
    std::error_code ec;
    fs::remove_all(tmp, ec);

    if (!outcome.ok) {
        std::cerr << "error: " << outcome.error << "\n";
        return outcome.exitCode;
    }
    std::cout << "Installed " << name << "@" << resolvedVersion << " into "
              << outcome.installedPath << "\n";
    // First install of a backend becomes its default.
    if (cache.defaultVersion(name).empty()) {
        std::string setErr;
        if (cache.setDefault(name, resolvedVersion, setErr))
            std::cout << "  set as default for " << name << "\n";
    }
    return kOk;
}

int cmdRemove(const Options& o) {
    if (o.positionals.empty()) {
        std::cerr << "error: 'remove' needs a backend name\n";
        return kUsageError;
    }
    auto [name, version] = splitNameVersion(o.positionals[0]);
    BackendCache cache = makeCache(o);
    std::string err;

    if (version.empty()) {
        auto versions = cache.versionsOf(name);
        if (versions.empty()) {
            std::cerr << "error: " << name << " is not installed\n";
            return kCacheError;
        }
        if (!o.yes) {
            std::cerr << "About to remove all " << versions.size()
                      << " cached version(s) of " << name << ". Re-run with --yes "
                         "to confirm.\n";
            return kUsageError;
        }
        if (!cache.removeBackend(name, err)) {
            std::cerr << "error: " << err << "\n";
            return kCacheError;
        }
        std::cout << "Removed all cached versions of " << name << "\n";
        return kOk;
    }

    if (!cache.removeVersion(name, version, err)) {
        std::cerr << "error: " << err << "\n";
        return kCacheError;
    }
    std::cout << "Removed " << name << "@" << version << "\n";
    return kOk;
}

int cmdDefault(const Options& o) {
    if (o.positionals.empty()) {
        std::cerr << "error: 'default' needs a backend name\n";
        return kUsageError;
    }
    auto [name, version] = splitNameVersion(o.positionals[0]);
    BackendCache cache = makeCache(o);

    if (version.empty()) {
        auto versions = cache.versionsOf(name);
        if (versions.empty()) {
            std::cerr << "error: " << name << " is not installed\n";
            return kCacheError;
        }
        std::vector<std::string> all;
        for (const auto& v : versions) all.push_back(v.version);
        version = newestVersion(all);
        if (version.empty()) version = versions.front().version;
    }

    std::string err;
    if (!cache.setDefault(name, version, err)) {
        std::cerr << "error: " << err << "\n";
        return kCacheError;
    }
    std::cout << "Default backend for " << name << " is now " << version << "\n";
    return kOk;
}

int cmdUpdate(const Options& o) {
    BackendCache cache = makeCache(o);
    std::string idxPath = cache.indexPath();
    bool haveIndex = !idxPath.empty() && fs::exists(idxPath);

    if (!haveIndex) {
        std::cerr << "error: no cached registry index at "
                  << (idxPath.empty() ? "<unresolved cache root>" : idxPath) << "\n"
                  << "  network registry fetch is not yet implemented; an "
                     "up-to-date index.json must be present in the cache root\n";
        return kRegistryError;
    }
    auto idx = parseRegistryIndexFile(idxPath);
    if (!idx.ok) {
        std::cerr << "error: " << idx.error << "\n";
        return kRegistryError;
    }

    std::vector<std::string> targets;
    if (!o.positionals.empty()) {
        targets.push_back(splitNameVersion(o.positionals[0]).first);
    } else {
        std::vector<std::string> seen;
        for (const auto& ib : cache.listInstalled()) {
            if (std::find(seen.begin(), seen.end(), ib.name) == seen.end()) {
                seen.push_back(ib.name);
                targets.push_back(ib.name);
            }
        }
    }
    if (targets.empty()) {
        std::cout << "No installed backends to update.\n";
        return kOk;
    }

    int updatable = 0;
    for (const auto& name : targets) {
        const IndexBackend* b = idx.index.findBackend(name);
        if (!b) {
            std::cerr << "warning: " << name
                      << " is not present in the registry index\n";
            continue;
        }
        // Newest registry version satisfying core_compat for our platform.
        std::string bestVer;
        SemVer bestSv;
        for (const auto& v : b->versions) {
            if (!satisfiesRange(kTopoCoreVersion, v.coreCompat)) continue;
            SemVer sv = parseSemVer(v.version);
            if (!sv.valid) continue;
            if (bestVer.empty() || sv.compareCore(bestSv) > 0) {
                bestVer = v.version;
                bestSv = sv;
            }
        }
        if (bestVer.empty()) {
            std::cerr << "warning: no core-compatible registry version of "
                      << name << "\n";
            continue;
        }
        if (cache.isInstalled(name, bestVer)) {
            std::cout << name << " is up to date (" << bestVer << ")\n";
            continue;
        }
        std::cout << name << ": newer core-compatible version available — "
                  << bestVer << "\n";
        ++updatable;
    }
    if (updatable > 0) {
        std::cout << "\n" << updatable
                  << " backend(s) have a newer version. Network install is not\n"
                     "yet implemented; fetch the archive and run "
                     "'topo backend install <name> --from <dir>'.\n";
    }
    return kOk;
}

int cmdPack(const Options& o) {
    if (o.positionals.empty()) {
        std::cerr << "error: 'pack' needs a backend name\n";
        return kUsageError;
    }
    if (o.outputDir.empty()) {
        std::cerr << "error: 'pack' needs an output directory (-o <dir>)\n";
        return kUsageError;
    }
    auto [name, version] = splitNameVersion(o.positionals[0]);
    std::string platformTriple =
        o.platformTriple.empty() ? currentPlatform() : o.platformTriple;

    // pack assembles a portable registry-leaf directory for one backend by
    // copying it out of the local cache. (Packing straight from a registry
    // would need the unimplemented network fetch.)
    BackendCache cache = makeCache(o);
    auto versions = cache.versionsOf(name);
    if (versions.empty()) {
        std::cerr << "error: " << name
                  << " is not in the local cache; nothing to pack\n";
        return kCacheError;
    }
    if (version.empty()) {
        std::vector<std::string> all;
        for (const auto& v : versions) all.push_back(v.version);
        version = newestVersion(all);
    }
    const InstalledBackend* src = nullptr;
    for (const auto& v : versions) {
        if (v.version == version) src = &v;
    }
    if (!src) {
        std::cerr << "error: " << name << "@" << version
                  << " is not in the local cache\n";
        return kCacheError;
    }

    std::error_code ec;
    fs::path leafDir = fs::path(o.outputDir) / name / version;
    fs::create_directories(leafDir, ec);
    if (ec) {
        std::cerr << "error: cannot create output directory: " << ec.message() << "\n";
        return kCacheError;
    }

    std::string archiveName =
        name + "-" + version + "-" + platformTriple + ".tar.gz";
    fs::path archivePath = leafDir / archiveName;

    // Build the archive with `tar` so `install --from` can re-extract it.
    // The cached <version>/ directory IS the package root.
    auto res = platform::runProcessCapture(
        "tar", {"-czf", archivePath.string(), "-C", src->path, "."});
    if (res.exitCode != 0) {
        std::cerr << "error: tar packing failed: " << res.stderrOutput << "\n";
        return kCacheError;
    }

    // Write a fragment index.json so `install --from` can resolve it.
    std::string sha = sha256File(archivePath.string());
    std::ofstream idx(fs::path(o.outputDir) / "index.json");
    idx << "{\n  \"schema\": 1,\n  \"backends\": [\n    {\n"
        << "      \"name\": \"" << name << "\",\n"
        << "      \"language\": [";
    for (std::size_t i = 0; i < src->manifest.backend.language.size(); ++i)
        idx << (i ? ", " : "") << "\"" << src->manifest.backend.language[i] << "\"";
    idx << "],\n      \"versions\": [\n        {\n"
        << "          \"version\": \"" << version << "\",\n"
        << "          \"core_compat\": \"" << src->manifest.backend.coreCompat << "\",\n"
        << "          \"artifacts\": [\n            { \"platform\": \""
        << platformTriple << "\", \"path\": \"" << name << "/" << version << "/"
        << archiveName << "\", \"sha256\": \"" << sha << "\" }\n"
        << "          ]\n        }\n      ]\n    }\n  ]\n}\n";

    std::cout << "Packed " << name << "@" << version << " for " << platformTriple
              << " into " << o.outputDir << "\n"
              << "  archive: " << archivePath.string() << "\n"
              << "  install it offline with: topo backend install " << name
              << " --from " << o.outputDir << "\n";
    return kOk;
}

} // namespace

void printBackendUsage() {
    std::cerr <<
        "Usage: topo backend <subcommand> [args] [options]\n"
        "\n"
        "Manage on-demand backend installs in the local cache\n"
        "(~/.topo/backends/ or $TOPO_HOME/backends/).\n"
        "\n"
        "Subcommands:\n"
        "  list      [--installed] [--available]      List backends\n"
        "  install   <name>[@<ver>] --from <dir>      Install from an offline dir\n"
        "  remove    <name>[@<ver>] [--yes]           Remove a cached backend\n"
        "  default   <name>[@<ver>]                   Set the default version\n"
        "  update    [<name>]                         Check for newer versions\n"
        "  pack      <name>[@<ver>] -o <dir>          Build a portable offline package\n"
        "\n"
        "Options:\n"
        "  --from <dir>          Offline source directory (registry-leaf layout)\n"
        "  --platform <triple>   Target platform, e.g. linux-x86_64 (default: host)\n"
        "  --cache-root <dir>    Override the cache root\n"
        "  -o, --output <dir>    Output directory (pack)\n"
        "  --installed           list: show the local cache (default)\n"
        "  --available           list: show the registry index\n"
        "  --require-signature   install: refuse packages without a verified signature\n"
        "  -y, --yes             Skip confirmation prompts\n"
        "\n"
        "Note: network registry fetch and Ed25519 signature verification are not\n"
        "yet implemented; the offline pack/install path is fully functional.\n";
}

int runBackendCli(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h" ||
        args[0] == "help") {
        printBackendUsage();
        return args.empty() ? kUsageError : kOk;
    }

    const std::string& sub = args[0];
    Options o = parseOptions(args, /*startAt=*/1);
    if (!o.parseError.empty()) {
        std::cerr << "error: " << o.parseError << "\n";
        return kUsageError;
    }

    if (sub == "list") return cmdList(o);
    if (sub == "install") return cmdInstall(o);
    if (sub == "remove") return cmdRemove(o);
    if (sub == "default") return cmdDefault(o);
    if (sub == "update") return cmdUpdate(o);
    if (sub == "pack") return cmdPack(o);

    std::cerr << "error: unknown 'topo backend' subcommand '" << sub << "'\n";
    printBackendUsage();
    return kUsageError;
}

} // namespace topo::dist
