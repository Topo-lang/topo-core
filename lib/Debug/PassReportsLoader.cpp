#include "topo/Debug/PassReportsLoader.h"

#include <fstream>
#include <system_error>

namespace topo::debug_meta {

namespace {

using nlohmann::json;
namespace fs = std::filesystem;

// Decode the on-disk `header { ... }` block into the typed struct.
// Missing keys default to the struct's zero-init values — the goal is
// best-effort visibility, not strict validation.
bool decodeHeader(const json& j, PassReportHeader& out) {
    if (!j.is_object()) return false;
    out.passName  = j.value("pass", std::string{});
    out.category  = j.value("category", std::string{});
    out.fired     = j.value("fired", false);
    out.firedCount = j.value("fired_count", 0);
    out.decision  = j.value("decision", std::string{});
    out.reason    = j.value("reason", std::string{});
    out.elapsedNs = j.value("elapsed_ns", static_cast<int64_t>(0));
    return true;
}

// Parse one sidecar file. Returns false on file/parse error; the caller
// (loadFromDirectory) silently skips such files so a single broken sidecar
// can't kill the whole load.
bool loadOne(const fs::path& path, PassReport& out) {
    std::ifstream in(path);
    if (!in) return false;
    json doc;
    try {
        in >> doc;
    } catch (const std::exception&) {
        return false;
    }
    if (!doc.is_object() || !doc.contains("header")) return false;
    if (!decodeHeader(doc["header"], out.header)) return false;
    // If the on-disk `pass` field is empty, fall back to the filename stem
    // so the registry still has something to index on.
    if (out.header.passName.empty()) {
        out.header.passName = path.stem().string();
    }
    // The detail = whole object minus the header. Keep it as a generic
    // json::object so consumers can grep through "candidates" / "entries" /
    // "stats" / etc. without topo-core needing to know each Pass's schema.
    out.detail = doc;
    out.detail.erase("header");
    return true;
}

} // namespace

std::optional<PassReportsRegistry>
PassReportsRegistry::loadFromDirectory(const fs::path& dir, std::string* err) {
    PassReportsRegistry reg;

    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        // Missing dir is fine — caller wants a best-effort registry.
        return reg;
    }
    if (!fs::is_directory(dir, ec)) {
        if (err) *err = "pass-reports path is not a directory: " + dir.string();
        return std::nullopt;
    }

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const auto& path = entry.path();
        if (path.extension() != ".json") continue;
        PassReport rpt;
        if (!loadOne(path, rpt)) continue;  // skip individually broken files
        reg.reports_[rpt.header.passName] = std::move(rpt);
    }
    return reg;
}

const PassReportHeader*
PassReportsRegistry::findHeader(const std::string& passName) const {
    auto it = reports_.find(passName);
    if (it == reports_.end()) return nullptr;
    return &it->second.header;
}

const nlohmann::json*
PassReportsRegistry::findDetail(const std::string& passName) const {
    auto it = reports_.find(passName);
    if (it == reports_.end()) return nullptr;
    return &it->second.detail;
}

const PassReport*
PassReportsRegistry::findReport(const std::string& passName) const {
    auto it = reports_.find(passName);
    if (it == reports_.end()) return nullptr;
    return &it->second;
}

} // namespace topo::debug_meta
