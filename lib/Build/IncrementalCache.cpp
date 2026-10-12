#include "topo/Build/IncrementalCache.h"
#include "topo/Build/SymbolTableJson.h"
#include "topo/Build/BuildConfig.h"
#include "topo/Basic/FNVHash.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cinttypes>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace topo::build {

// v3: bumped so pre-existing on-disk manifests (written before the build
// compared the config fingerprint on a cache hit) are invalidated once.
// Must match CacheManifest's default version (the writer's source of truth).
static constexpr int CACHE_VERSION = 3;

IncrementalCache::IncrementalCache(const fs::path& projectDir) : projectDir_(projectDir) {}

fs::path IncrementalCache::cacheDir() const {
    return projectDir_ / ".topo-cache";
}

fs::path IncrementalCache::irDir() const {
    return cacheDir() / "ir";
}

// ===================================================================
// Manifest
// ===================================================================

bool IncrementalCache::loadManifest(CacheManifest& out) const {
    fs::path manifestPath = cacheDir() / "manifest.json";
    if (!fs::exists(manifestPath)) return false;

    try {
        std::ifstream ifs(manifestPath);
        if (!ifs.is_open()) return false;
        json j = json::parse(ifs);

        int ver = j.value("version", 0);
        if (ver != CACHE_VERSION) return false;

        out.version = ver;
        out.configFingerprint = j.value("configFingerprint", "");
        out.topoTomlMtime = j.value("topoTomlMtime", int64_t(0));

        out.topoFileMtimes.clear();
        if (j.contains("topoFileMtimes")) {
            for (const auto& [key, val] : j["topoFileMtimes"].items())
                out.topoFileMtimes[key] = val.get<int64_t>();
        }

        return true;
    } catch (const json::exception&) {
        return false;
    }
}

bool IncrementalCache::saveManifest(const CacheManifest& m) const {
    json j;
    j["version"] = m.version;
    j["configFingerprint"] = m.configFingerprint;
    j["topoTomlMtime"] = m.topoTomlMtime;

    json mtimes = json::object();
    for (const auto& [path, mtime] : m.topoFileMtimes)
        mtimes[path] = mtime;
    j["topoFileMtimes"] = mtimes;

    fs::path manifestPath = cacheDir() / "manifest.json";
    std::ofstream ofs(manifestPath);
    // Surface write failures instead of leaving an inconsistent on-disk cache
    // silently — mirrors saveSymbolTable/saveVisibilityEntries. A half-written
    // manifest paired with valid symbols/visibility would be the dangerous
    // case (loadManifest rejects on parse error, so an unopened stream is the
    // benign miss; a stream that opens but fails mid-write is the one to catch).
    if (!ofs.is_open()) return false;
    ofs << j.dump(2);
    return ofs.good();
}

// ===================================================================
// Validity checks
// ===================================================================

bool IncrementalCache::isTopoFrontendValid(const CacheManifest& m, const std::vector<std::string>& topoPaths) const {
    // Check Topo.toml mtime
    fs::path tomlPath = projectDir_ / "Topo.toml";
    if (fs::exists(tomlPath)) {
        int64_t currentMtime = getFileMtime(tomlPath);
        if (currentMtime != m.topoTomlMtime) return false;
    }

    // Check that the set of .topo files matches
    if (topoPaths.size() != m.topoFileMtimes.size()) return false;

    for (const auto& path : topoPaths) {
        auto genericPath = fs::path(path).generic_string();
        auto it = m.topoFileMtimes.find(genericPath);
        if (it == m.topoFileMtimes.end()) return false;

        int64_t currentMtime = getFileMtime(path);
        if (currentMtime != it->second) return false;
    }

    return true;
}

bool IncrementalCache::isCompileConfigValid(const CacheManifest& m, const BuildConfig& cfg) const {
    return m.configFingerprint == computeConfigFingerprint(cfg);
}

// ===================================================================
// SymbolTable serialization
// ===================================================================

bool IncrementalCache::saveSymbolTable(const SymbolTable& symbols) const {
    try {
        json j = topo::serializeSymbolTable(symbols);
        fs::path path = cacheDir() / "symbols.json";
        std::ofstream ofs(path);
        if (!ofs.is_open()) return false;
        ofs << j.dump(2);
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

bool IncrementalCache::loadSymbolTable(SymbolTable& symbols) const {
    fs::path path = cacheDir() / "symbols.json";
    if (!fs::exists(path)) return false;

    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return false;
        json j = json::parse(ifs);
        return topo::deserializeSymbolTable(j, symbols);
    } catch (const json::exception&) {
        return false;
    }
}

bool IncrementalCache::saveVisibilityEntries(const std::vector<VisibilityEntry>& v) const {
    try {
        json j = v;
        fs::path path = cacheDir() / "visibility.json";
        std::ofstream ofs(path);
        if (!ofs.is_open()) return false;
        ofs << j.dump(2);
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

bool IncrementalCache::loadVisibilityEntries(std::vector<VisibilityEntry>& v) const {
    fs::path path = cacheDir() / "visibility.json";
    if (!fs::exists(path)) return false;

    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return false;
        json j = json::parse(ifs);
        v = j.get<std::vector<VisibilityEntry>>();
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

bool IncrementalCache::saveEvidenceManifest(const evidence::Manifest& manifest, std::string& error) const {
    return evidence::writeManifest(cacheDir() / "evidence.json", manifest, error);
}

bool IncrementalCache::loadEvidenceManifest(evidence::Manifest& manifest, std::string& error) const {
    return evidence::readManifest(cacheDir() / "evidence.json", manifest, error);
}

evidence::VerificationResult IncrementalCache::verifyEvidence(const evidence::Action& expected,
                                                            const evidence::RootMap& roots) const {
    evidence::Manifest manifest;
    std::string error;
    if (!loadEvidenceManifest(manifest, error)) {
        evidence::VerificationResult result;
        result.diagnostics.push_back("cache evidence unavailable: " + error);
        return result;
    }
    auto result = evidence::verifyManifest(manifest, roots);
    if (!result.integrity) return result;
    if (manifest.roots.size() != 1) {
        result.integrity = false;
        result.diagnostics.push_back("cache evidence requires exactly one action root");
        return result;
    }
    for (const auto& node : manifest.nodes) {
        if (node.id != manifest.roots.front()) continue;
        // verifyManifest already checked this node's digest and every file.
        // Compare the expected action even when coverage remains incomplete;
        // an unrelated valid manifest does not establish cache integrity.
        const auto expectedDigest = evidence::actionDigest(expected);
        if (expectedDigest.empty() || evidence::actionDigest(node.action) != expectedDigest) {
            result.integrity = false;
            result.diagnostics.push_back("cache action identity mismatch");
        }
        if ((node.kind != "cache" && node.kind != "build-stage" && node.kind != "audit") ||
            node.outputs.empty()) {
            result.integrity = false;
            result.diagnostics.push_back("cache root is not an action with recorded outputs");
        }
        return result;
    }
    result.integrity = false;
    result.diagnostics.push_back("cache action root is missing");
    return result;
}

// ===================================================================
// Config fingerprint
// ===================================================================

std::string IncrementalCache::computeConfigFingerprint(const BuildConfig& cfg) {
    // FNV-1a on a concatenated string of compile-affecting fields.
    // Deterministic and portable — unlike std::hash which varies across
    // platforms and is not guaranteed stable across program invocations.
    std::ostringstream oss;

    oss << "compiler:" << cfg.hostCompilerPath << "\n";
    oss << "standard:" << cfg.standard << "\n";

    // Include dirs (sorted for stability)
    {
        auto sorted = cfg.includeDirs;
        std::sort(sorted.begin(), sorted.end());
        for (const auto& dir : sorted)
            oss << "I:" << dir << "\n";
    }

    // Sources (sorted for stability)
    {
        auto sorted = cfg.sources;
        std::sort(sorted.begin(), sorted.end());
        for (const auto& src : sorted)
            oss << "src:" << fs::path(src).generic_string() << "\n";
    }

    // Extra clang++ flags (order-preserving — flag order is semantic, so no
    // sort; a reorder must invalidate the fingerprint like a value change)
    for (const auto& flag : cfg.cppFlags)
        oss << "cppflag:" << flag << "\n";

    oss << "embedIR:" << cfg.embedIR << "\n";
    oss << "adaptive:" << cfg.adaptiveCfg.isEnabled() << "\n";
    oss << "outputType:" << static_cast<int>(cfg.outputType) << "\n";

    // Hash the concatenated string
    std::string data = oss.str();
    uint64_t hash = fnv1aHash(data);

    // Convert to fixed-width hex string
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016" PRIx64, hash);
    return std::string(buf);
}

// ===================================================================
// Directory management
// ===================================================================

void IncrementalCache::clean() const {
    std::error_code ec;
    fs::remove_all(cacheDir(), ec);
}

void IncrementalCache::ensureDirectories() const {
    std::error_code ec;
    fs::create_directories(irDir(), ec);
}

int64_t IncrementalCache::getFileMtime(const fs::path& path) {
    std::error_code ec;
    auto ftime = fs::last_write_time(path, ec);
    if (ec) return 0;
    return ftime.time_since_epoch().count();
}

} // namespace topo::build
