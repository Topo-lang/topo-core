#include "topo/Distribution/RegistryIndex.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace topo::dist {

const IndexBackend* RegistryIndex::findBackend(const std::string& name) const {
    for (const auto& b : backends) {
        if (b.name == name) return &b;
    }
    return nullptr;
}

namespace {

IndexParseResult fail(const std::string& src, const std::string& msg) {
    IndexParseResult r;
    r.ok = false;
    r.error = src.empty() ? msg : (src + ": " + msg);
    return r;
}

} // namespace

IndexParseResult parseRegistryIndex(const std::string& jsonText,
                                    const std::string& sourceLabel) {
    nlohmann::json j = nlohmann::json::parse(jsonText, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) return fail(sourceLabel, "malformed JSON in registry index");
    if (!j.is_object()) return fail(sourceLabel, "registry index must be a JSON object");

    IndexParseResult result;
    RegistryIndex& idx = result.index;

    // Type-mismatched fields (e.g. a numeric "name") make nlohmann value()/
    // get<>() throw type_error; catch and fail closed rather than letting the
    // exception escape and crash the caller.
    try {
    idx.schema = j.value("schema", 0);
    if (idx.schema != 1)
        return fail(sourceLabel, "unsupported registry index schema (expected 1)");

    auto backendsIt = j.find("backends");
    if (backendsIt == j.end() || !backendsIt->is_array())
        return fail(sourceLabel, "registry index missing 'backends' array");

    for (const auto& bj : *backendsIt) {
        if (!bj.is_object())
            return fail(sourceLabel, "each backend entry must be a JSON object");
        IndexBackend b;
        b.name = bj.value("name", std::string{});
        if (b.name.empty())
            return fail(sourceLabel, "a backend entry is missing 'name'");
        if (auto lit = bj.find("language"); lit != bj.end() && lit->is_array()) {
            for (const auto& l : *lit)
                if (l.is_string()) b.language.push_back(l.get<std::string>());
        }
        auto vit = bj.find("versions");
        if (vit == bj.end() || !vit->is_array())
            return fail(sourceLabel, "backend '" + b.name + "' missing 'versions' array");
        for (const auto& vj : *vit) {
            if (!vj.is_object())
                return fail(sourceLabel, "each version entry must be a JSON object");
            IndexVersion v;
            v.version = vj.value("version", std::string{});
            if (v.version.empty())
                return fail(sourceLabel, "a version entry of '" + b.name +
                                             "' is missing 'version'");
            v.coreCompat = vj.value("core_compat", std::string{});
            auto ait = vj.find("artifacts");
            if (ait == vj.end() || !ait->is_array())
                return fail(sourceLabel, b.name + "@" + v.version +
                                             " missing 'artifacts' array");
            for (const auto& aj : *ait) {
                if (!aj.is_object())
                    return fail(sourceLabel, "each artifact entry must be a JSON object");
                IndexArtifact a;
                a.platform = aj.value("platform", std::string{});
                a.path = aj.value("path", std::string{});
                a.sha256 = aj.value("sha256", std::string{});
                if (a.platform.empty() || a.path.empty())
                    return fail(sourceLabel, b.name + "@" + v.version +
                                                 " artifact missing 'platform' or 'path'");
                v.artifacts.push_back(std::move(a));
            }
            b.versions.push_back(std::move(v));
        }
        idx.backends.push_back(std::move(b));
    }
    } catch (const nlohmann::json::exception& e) {
        return fail(sourceLabel, std::string("type mismatch in registry index: ") + e.what());
    }

    result.ok = true;
    return result;
}

IndexParseResult parseRegistryIndexFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail(path, "cannot open registry index file");
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseRegistryIndex(ss.str(), path);
}

} // namespace topo::dist
