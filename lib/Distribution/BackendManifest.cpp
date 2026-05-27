#include "topo/Distribution/BackendManifest.h"

// toml++ as header-only, no-exception mode (matches topo-build's Config.cpp;
// LLVM-linked tools are built with -fno-rtti, so exceptions stay off here too).
#define TOML_HEADER_ONLY 1
#define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

namespace topo::dist {

std::string PlatformTriple::str() const {
    return os + "-" + arch;
}

std::vector<std::string> BackendManifest::payloadPaths() const {
    std::set<std::string> paths;
    for (const auto& [tool, path] : binaries) {
        (void)tool;
        paths.insert(path);
    }
    for (const auto& group : runtime) {
        for (const auto& f : group.files) paths.insert(f);
    }
    return {paths.begin(), paths.end()};
}

namespace {

/// A package-relative path is valid only when it neither escapes the package
/// root nor is absolute (spec §1 `[binaries]`).
bool isContainedRelPath(const std::string& p) {
    if (p.empty()) return false;
    if (p.front() == '/' || p.front() == '\\') return false;
    // Windows drive letter.
    if (p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':')
        return false;
    // Any ".." path component.
    std::string component;
    auto flush = [&](void) -> bool {
        bool bad = (component == "..");
        component.clear();
        return !bad;
    };
    for (char c : p) {
        if (c == '/' || c == '\\') {
            if (!flush()) return false;
        } else {
            component.push_back(c);
        }
    }
    return flush();
}

bool isValidBackendName(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) return false;
    }
    return true;
}

ManifestParseResult fail(const std::string& src, const std::string& msg) {
    ManifestParseResult r;
    r.ok = false;
    r.error = src.empty() ? msg : (src + ": " + msg);
    return r;
}

} // namespace

ManifestParseResult parseBackendManifest(const std::string& tomlText,
                                         const std::string& sourceLabel) {
    toml::parse_result parsed = toml::parse(tomlText);
    if (!parsed) {
        std::ostringstream os;
        os << "TOML parse error: " << parsed.error().description();
        return fail(sourceLabel, os.str());
    }
    const toml::table& tbl = parsed.table();
    ManifestParseResult result;
    BackendManifest& m = result.manifest;

    // ── [backend] ────────────────────────────────────────────────
    const auto* backendTbl = tbl["backend"].as_table();
    if (!backendTbl) return fail(sourceLabel, "missing required [backend] table");

    if (auto v = (*backendTbl)["name"].value<std::string>())
        m.backend.name = *v;
    else
        return fail(sourceLabel, "[backend].name is required");
    if (!isValidBackendName(m.backend.name))
        return fail(sourceLabel, "[backend].name '" + m.backend.name +
                                     "' must match [a-z0-9-]+");

    if (auto v = (*backendTbl)["version"].value<std::string>())
        m.backend.version = *v;
    else
        return fail(sourceLabel, "[backend].version is required");

    if (auto v = (*backendTbl)["core_compat"].value<std::string>())
        m.backend.coreCompat = *v;
    else
        return fail(sourceLabel, "[backend].core_compat is required");

    if (const auto* langArr = (*backendTbl)["language"].as_array()) {
        for (const auto& el : *langArr) {
            if (auto s = el.value<std::string>()) m.backend.language.push_back(*s);
        }
    }
    if (m.backend.language.empty())
        return fail(sourceLabel, "[backend].language must list at least one host language");

    if (auto v = (*backendTbl)["description"].value<std::string>())
        m.backend.description = *v;

    // ── [binaries] ───────────────────────────────────────────────
    if (const auto* binTbl = tbl["binaries"].as_table()) {
        for (const auto& [key, val] : *binTbl) {
            auto path = val.value<std::string>();
            if (!path)
                return fail(sourceLabel, "[binaries]." + std::string(key) +
                                             " must be a string path");
            if (!isContainedRelPath(*path))
                return fail(sourceLabel, "[binaries]." + std::string(key) +
                                             " path '" + *path +
                                             "' escapes the package root");
            m.binaries.emplace(std::string(key), *path);
        }
    }

    // ── [runtime.*] ──────────────────────────────────────────────
    if (const auto* rtTbl = tbl["runtime"].as_table()) {
        for (const auto& [groupName, groupVal] : *rtTbl) {
            const auto* groupTbl = groupVal.as_table();
            if (!groupTbl)
                return fail(sourceLabel, "[runtime." + std::string(groupName) +
                                             "] must be a table");
            RuntimeGroup g;
            g.name = std::string(groupName);

            if (auto v = (*groupTbl)["kind"].value<std::string>())
                g.kind = *v;
            else
                return fail(sourceLabel, "[runtime." + g.name + "].kind is required");
            static const std::set<std::string> kKinds = {"native-lib", "headers",
                                                          "jar", "toolchain"};
            if (kKinds.find(g.kind) == kKinds.end())
                return fail(sourceLabel, "[runtime." + g.name + "].kind '" + g.kind +
                                             "' must be one of native-lib|headers|jar|toolchain");

            if (auto v = (*groupTbl)["install"].value<std::string>())
                g.install = *v;
            else
                return fail(sourceLabel, "[runtime." + g.name + "].install is required");
            if (!isContainedRelPath(g.install))
                return fail(sourceLabel, "[runtime." + g.name + "].install path '" +
                                             g.install + "' escapes the package root");

            if (const auto* filesArr = (*groupTbl)["files"].as_array()) {
                for (const auto& el : *filesArr) {
                    auto f = el.value<std::string>();
                    if (!f)
                        return fail(sourceLabel, "[runtime." + g.name +
                                                     "].files entries must be strings");
                    if (!isContainedRelPath(*f))
                        return fail(sourceLabel, "[runtime." + g.name + "].files entry '" +
                                                     *f + "' escapes the package root");
                    g.files.push_back(*f);
                }
            }
            if (g.files.empty())
                return fail(sourceLabel, "[runtime." + g.name + "].files must be non-empty");

            if (const auto* envTbl = (*groupTbl)["env"].as_table()) {
                for (const auto& [ek, ev] : *envTbl) {
                    if (auto s = ev.value<std::string>())
                        g.env.emplace(std::string(ek), *s);
                }
            }
            m.runtime.push_back(std::move(g));
        }
    }

    // ── [platform] ───────────────────────────────────────────────
    const auto* platTbl = tbl["platform"].as_table();
    if (!platTbl) return fail(sourceLabel, "missing required [platform] table");
    if (auto v = (*platTbl)["os"].value<std::string>())
        m.platform.os = *v;
    else
        return fail(sourceLabel, "[platform].os is required");
    if (auto v = (*platTbl)["arch"].value<std::string>())
        m.platform.arch = *v;
    else
        return fail(sourceLabel, "[platform].arch is required");
    if (auto v = (*platTbl)["libc"].value<std::string>())
        m.platform.libc = *v;
    {
        static const std::set<std::string> kOs = {"linux", "macos", "windows"};
        static const std::set<std::string> kArch = {"x86_64", "aarch64"};
        if (kOs.find(m.platform.os) == kOs.end())
            return fail(sourceLabel, "[platform].os '" + m.platform.os +
                                         "' must be linux|macos|windows");
        if (kArch.find(m.platform.arch) == kArch.end())
            return fail(sourceLabel, "[platform].arch '" + m.platform.arch +
                                         "' must be x86_64|aarch64");
    }

    // ── [signature] ──────────────────────────────────────────────
    // Optional during parse — the install path decides whether a missing or
    // unverifiable signature is fatal. A present block is schema-validated.
    if (const auto* sigTbl = tbl["signature"].as_table()) {
        if (auto v = (*sigTbl)["algorithm"].value<std::string>())
            m.signature.algorithm = *v;
        if (auto v = (*sigTbl)["key_id"].value<std::string>())
            m.signature.keyId = *v;
        if (auto v = (*sigTbl)["value"].value<std::string>())
            m.signature.value = *v;
        if (!m.signature.algorithm.empty() && m.signature.algorithm != "ed25519")
            return fail(sourceLabel, "[signature].algorithm must be 'ed25519'");
    }

    result.ok = true;
    return result;
}

ManifestParseResult parseBackendManifestFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail(path, "cannot open manifest file");
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseBackendManifest(ss.str(), path);
}

} // namespace topo::dist
