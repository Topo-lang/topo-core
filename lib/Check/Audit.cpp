#include "topo/Check/Audit.h"
#include "topo/Distribution/Sha256.h"
#include "topo/Platform/TempFile.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace topo::check {
namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

std::string digest(const json& j) { return dist::sha256Hex(j.dump()); }

fs::path auditCanonical(const fs::path& p) {
    std::error_code ec;
    auto result = platform::canonicalPath(p, ec);
    if (ec) throw std::runtime_error("cannot resolve audit input: " + p.string());
    return result;
}

bool auditRedirect(const fs::path& path) {
    std::error_code error;
    const bool redirect = platform::isPathRedirect(path, error);
    if (error) throw std::runtime_error("cannot inspect audit input: " + path.string());
    return redirect;
}

json snapshot(const fs::path& input) {
    auto target = auditCanonical(input);
    std::error_code ec;
    if (!fs::is_regular_file(target, ec) || ec)
        throw std::runtime_error("audit input is not a regular file: " + input.string());
    constexpr std::uintmax_t maxInputBytes=1024ULL*1024*1024;
    if (fs::file_size(target)>maxInputBytes) throw std::runtime_error("audit input exceeds 1 GiB: "+input.string());
    std::ifstream in(target, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open audit input: " + input.string());
    dist::Sha256 h;
    char buffer[65536];
    std::uintmax_t size = 0;
    while (in) {
        in.read(buffer, sizeof(buffer));
        auto n = in.gcount();
        if (n > 0) { h.update(buffer, static_cast<std::size_t>(n)); size += n;
            if (size>maxInputBytes) throw std::runtime_error("audit input grew beyond size limit: "+input.string()); }
    }
    if (in.bad() || (!in.eof() && in.fail()))
        throw std::runtime_error("cannot read complete audit input: " + input.string());
    return {{"path", fs::absolute(input).lexically_normal().generic_string()},
            {"resolvedPath", target.generic_string()}, {"size", size}, {"sha256", h.hexDigest()}};
}

json encodeIdentity(const AuditIdentity& i) {
    return {{"id", i.id}, {"name", i.name}, {"kind", i.kind}, {"signature", i.signature},
            {"owner", i.owner}, {"file", i.file}, {"line", i.line}, {"origin", i.origin},
            {"complete", i.complete}};
}

bool matchesOwner(const AuditIdentity& i, const AuditIdentity& type,
                  const std::map<std::string, const AuditIdentity*>& byId) {
    if (i.id == type.id) return true;
    std::set<std::string> seen;
    auto owner = i.owner;
    while (!owner.empty() && seen.insert(owner).second) {
        if (owner == type.id || owner == type.name) return true;
        auto it = byId.find(owner);
        if (it == byId.end()) return false;
        owner = it->second->owner;
    }
    return false;
}

json policyJson(const AuditPolicy& p) {
    json selectors = json::array();
    for (const auto& s : p.manual)
        selectors.push_back({{"kind", s.kind}, {"name", s.name}, {"file", s.file}});
    return {{"schemaVersion", p.schemaVersion}, {"dependencies", p.dependencies},
            {"strictDependencies", p.strictDependencies}, {"manual", selectors},
            {"expand", p.expand}, {"deny", p.deny}};
}
std::string dependencyKey(json identity) {
    identity.erase("path"); identity.erase("id");
    return digest(json{{"kind","topo.dependency-approval"},{"identity",identity}});
}
json dependencyJudgements(const json& report) {
    auto entries=report.at("dependencies");
    if (!entries.is_array() || entries.size()!=report.at("dependencyIdentities").size())
        throw std::runtime_error("audit dependency identity list mismatch");
    for (std::size_t i=0;i<entries.size();++i) {
        const auto& identity=report.at("dependencyIdentities").at(i);
        auto original=entries.at(i);
        original.erase("key"); original.erase("analysis"); original.erase("approval");
        if (original!=identity || entries[i].at("key")!=dependencyKey(identity))
            throw std::runtime_error("audit dependency fields differ from bound identity");
        std::string expected=identity.at("project").get<bool>()?"analyze":"dependency-opaque";
        for (const auto& expanded:report.at("policy").at("expand"))
            if (expanded==identity.at("id")) expected="unresolved";
        if (entries[i].at("analysis")!=expected) throw std::runtime_error("audit dependency analysis state mismatch");
        entries[i].erase("approval");
    }
    return entries;
}
} // namespace

std::string auditReviewKey(const nlohmann::json& report) {
    return digest(json{{"kind", "topo.audit-approval"}, {"schemaVersion", 1},
        {"project", report.at("project")}, {"policy", report.at("policy")},
        {"provider", report.at("provider")}, {"buildVariant", report.at("buildVariant")},
        {"inputs", report.at("inputs")}, {"directories", report.at("directories")},
        {"manual", report.at("manual")}, {"identities", report.at("identities")},
        {"references", report.at("references")},
        {"dependencies", report.at("dependencyIdentities")},
        {"nativeBindings", report.at("nativeBindings")}, {"dependencyJudgements",dependencyJudgements(report)},
        {"supported",report.at("supported")},{"analysisComplete",report.at("analysisComplete")},
        {"analysisIsolation",report.at("analysisIsolation")},{"evidenceLevel",report.at("evidenceLevel")},
        {"unresolved",report.at("unresolved")},{"sourceFiles",report.at("sourceFiles")}});
}

nlohmann::json makeAuditReport(const std::string& project, const AuditFacts& facts,
                               const AuditPolicy& policy,
                               const std::vector<std::string>& externalTypes,
                               const std::vector<std::string>& sourceFiles,
                               const std::vector<std::string>& additionalInputs) {
    auto root = auditCanonical(project);
    if (policy.schemaVersion != 1 || policy.dependencies != "opaque")
        throw std::runtime_error("unsupported audit policy version or dependency default");
    json out = {{"kind", "topo.audit-report"}, {"schemaVersion", 1},
                {"project", root.generic_string()}, {"provider", facts.provider},
                {"buildVariant", facts.buildVariant}, {"policy", policyJson(policy)},
                {"evidenceLevel", "model"}, {"analysisIsolation", facts.analysisIsolation}, {"supported", facts.supported},
                {"analysisComplete", facts.complete}, {"inputs", json::array()},
                {"manual", json::array()}, {"dependencyIdentities", json::array()},
                {"dependencies", json::array()}, {"nativeBindings", json::array()},
                {"unresolved", facts.unresolved}, {"sourceFiles", sourceFiles},
                {"references", json::array()}, {"identities", json::array()}, {"directories", json::array()}};
    auto unresolved = [&](const std::string& reason) { out["unresolved"].push_back(reason); };
    if (!facts.supported) unresolved("provider has no semantic audit capability");
    if (facts.schemaVersion != 1) unresolved("unsupported provider audit schema");
    if (!facts.complete) unresolved("provider analysis coverage is incomplete");
    std::set<std::string> inputs;
    for (const auto& list : {facts.inputs, sourceFiles, additionalInputs})
        for (const auto& file : list) if (!file.empty()) inputs.insert(file);
    if (!facts.modelPath.empty()) inputs.insert(facts.modelPath);
    std::map<std::string, const AuditIdentity*> byId;
    std::map<std::string, int> nameCounts;
    for (const auto& i : facts.identities) ++nameCounts[i.name];
    for (const auto& i : facts.identities) {
        if (i.id.empty() || !byId.emplace(i.id, &i).second)
            unresolved("empty or duplicate semantic identity: " + i.id);
        if (nameCounts[i.name] == 1) byId.emplace(i.name, &i);
        out["identities"].push_back(encodeIdentity(i));
        if (!i.complete) unresolved("incomplete semantic identity: " + i.name);
        if (!i.file.empty()) inputs.insert(i.file);
    }
    std::vector<AuditSelector> selectors = policy.manual;
    for (const auto& type : externalTypes) selectors.push_back({"class", type, ""});
    std::set<std::string> selected;
    std::set<std::string> manualCallers;
    std::set<std::string> manualPaths;
    for (const auto& s : selectors) {
        if (s.kind != "class" && s.kind != "file" && s.kind != "function")
            throw std::runtime_error("audit selector kind must be class, function or file");
        std::string file;
        if (!s.file.empty()) file = auditCanonical(root / s.file).generic_string();
        if (s.kind == "file") {
            if (file.empty()) throw std::runtime_error("file selector requires file");
            bool found = false;
            for (const auto& source : sourceFiles) if (auditCanonical(source).generic_string() == file) found = true;
            if (!found) { unresolved("manual file is not in the selected source model: " + file); continue; }
            if (selected.insert("file:" + file).second) {
                manualPaths.insert(file);
                json members = json::array();
                for (const auto& i : facts.identities)
                    if (!i.file.empty() && auditCanonical(i.file).generic_string() == file) {
                        members.push_back(encodeIdentity(i)); manualCallers.insert(i.id); manualCallers.insert(i.name);
                    }
                out["manual"].push_back({{"kind", "file"}, {"name", file}, {"file", file}, {"members", members}});
            }
            continue;
        }
        std::vector<const AuditIdentity*> matches;
        for (const auto& i : facts.identities) {
            if (i.name != s.name) continue;
            if (s.kind == "class" && (i.kind != "class" && i.kind != "type" && i.kind != "interface" && i.kind != "enum" && i.kind != "record")) continue;
            if (s.kind == "function" && i.kind != "method" && i.kind != "function" && i.kind != "constructor") continue;
            if (!file.empty() && (i.file.empty() || auditCanonical(i.file).generic_string() != file)) continue;
            matches.push_back(&i);
        }
        if (matches.size() != 1) { unresolved("manual class must resolve to exactly one definition: " + s.name); continue; }
        const auto& type = *matches.front();
        if (selected.insert(type.id).second) {
            json members = json::array();
            for (const auto& i : facts.identities) if (matchesOwner(i, type, byId)) {
                members.push_back(encodeIdentity(i)); manualCallers.insert(i.id); manualCallers.insert(i.name);
            }
            out["manual"].push_back({{"kind", s.kind}, {"name", type.name}, {"identity", type.id}, {"file", type.file}, {"members", members}});
        }
    }
    for (const auto& dep : facts.dependencies) {
        if (dep.ownership=="unknown") unresolved("dependency ownership is unknown: "+dep.id);
        json identity = {{"id", dep.id}, {"path", dep.path}, {"version", dep.version}, {"project", dep.project},
                         {"ownership",dep.ownership}, {"coordinate",dep.coordinate}};
        if (!dep.path.empty()) {
            auto path = auditCanonical(dep.path);
            if (fs::is_regular_file(path)) { inputs.insert(dep.path); identity["sha256"] = snapshot(dep.path).at("sha256"); }
            else if (fs::is_directory(path)) {
                json files = json::array();
                json paths = json::array();
                std::vector<fs::path> entries;
                std::size_t visited=0;
                for (auto it = fs::recursive_directory_iterator(path);
                     it != fs::recursive_directory_iterator(); ++it) {
                    const auto& e = *it;
                    if (++visited>100000) throw std::runtime_error("dependency directory exceeds 100000 entries");
                    if (auditRedirect(e.path())) {
                        it.disable_recursion_pending();
                        unresolved("dependency directory contains an unmodeled symlink or reparse point: " + e.path().string());
                    } else if (e.is_regular_file()) entries.push_back(e.path());
                }
                std::sort(entries.begin(), entries.end());
                for (const auto& e : entries) {
                    auto item = snapshot(e);
                    auto entry = fs::relative(e,path).generic_string();
                    files.push_back({{"entry",entry},{"size",item.at("size")},{"sha256",item.at("sha256")}});
                    paths.push_back(entry);
                    inputs.insert(e.string());
                }
                out["directories"].push_back({{"path",path.generic_string()},{"entries",paths}});
                identity["sha256"] = digest(files);
            } else unresolved("unsupported dependency object: " + dep.path);
        } else if (dep.digest.empty()) unresolved("dependency has no concrete origin: " + dep.id);
        else identity["sha256"] = dep.digest;
        out["dependencyIdentities"].push_back(identity);
        json item = identity;
        item["key"] = dependencyKey(identity);
        item["analysis"] = dep.project ? "analyze" : "dependency-opaque";
        item["approval"] = policy.strictDependencies && !dep.project ? "pending" : "not-required";
        if (std::find(policy.deny.begin(), policy.deny.end(), dep.id) != policy.deny.end()) item["approval"] = "revoked";
        if (std::find(policy.expand.begin(), policy.expand.end(), dep.id) != policy.expand.end()) {
            item["analysis"] = "unresolved";
            unresolved("dependency expansion requires a project analysis model: " + dep.id);
        }
        out["dependencies"].push_back(item);
    }
    for (const auto& ref : facts.references) {
        out["references"].push_back({{"caller", ref.caller}, {"target", ref.target}, {"file", ref.file}, {"line", ref.line},
            {"resolved", ref.resolved}, {"dispatch", ref.dispatch}, {"runtimeTargetsComplete", ref.runtimeTargetsComplete}, {"dependency",ref.dependency}});
        bool manual = manualCallers.count(ref.caller) != 0;
        if (!manual && !ref.file.empty()) manual = manualPaths.count(auditCanonical(ref.file).generic_string()) != 0;
        const bool knownDependency = !ref.dependency.empty() && std::any_of(
            facts.dependencies.begin(), facts.dependencies.end(), [&](const auto& d) { return d.id == ref.dependency; });
        if (!ref.dependency.empty() && !knownDependency) unresolved("reference has unknown dependency: " + ref.dependency);
        if (!manual && (!ref.resolved || !ref.runtimeTargetsComplete))
            unresolved("unverified call target at " + ref.file + ":" + std::to_string(ref.line));
    }
    for (const auto& n : facts.nativeBindings) {
        out["nativeBindings"].push_back({{"caller", n.caller}, {"file", n.file}, {"line", n.line},
            {"operation", n.operation}, {"library", n.library}, {"symbol", n.symbol}, {"descriptor", n.descriptor},
            {"resolved", n.resolved}, {"callback", n.callback}, {"lookup", n.lookup}, {"unresolvedReason", n.unresolvedReason},
            {"staticBinding", n.staticBinding}, {"runtimeMapping", n.runtimeMapping}, {"provenanceStatus", n.provenanceStatus},
            {"evidenceRecords", n.evidenceRecords}, {"diagnostics", n.diagnostics},
            {"buildProvenance", "unverified"}});
        // Library selection is an analysis fact. A source provenance claim
        // requires a separately verified artifact statement.
    }
    if (inputs.size()>100000) throw std::runtime_error("audit exceeds 100000 input files");
    for (const auto& file : inputs) out["inputs"].push_back(snapshot(fs::path(file).is_absolute() ? fs::path(file) : root / file));
    for (const auto& [file, expected] : facts.inputDigests)
        if (snapshot(file).at("sha256") != expected) unresolved("input changed during semantic analysis: " + file);
    out["reviewKey"] = auditReviewKey(out);
    out["manualApproval"] = out["manual"].empty() ? "not-required" : "pending";
    applyAuditTrust(out, json::object());
    return out;
}

bool verifyAuditSnapshot(const nlohmann::json& report, std::string& error) {
    try {
        if (report.at("kind") != "topo.audit-report" || report.at("schemaVersion") != 1)
            throw std::runtime_error("unsupported audit report");
        if (report.at("reviewKey") != auditReviewKey(report))
            throw std::runtime_error("audit report identity mismatch");
        for (const auto& input : report.at("inputs"))
            if (snapshot(input.at("path").get<std::string>()) != input)
                throw std::runtime_error("audit input changed: " + input.at("path").get<std::string>());
        for (const auto& directory : report.at("directories")) {
            std::vector<std::string> entries;
            fs::path root(directory.at("path").get<std::string>());
            std::size_t visited=0;
            for (const auto& e : fs::recursive_directory_iterator(root)) {
                if (++visited>100000) throw std::runtime_error("dependency directory exceeds 100000 entries");
                if (auditRedirect(e.path())) throw std::runtime_error("new dependency symlink or reparse point: " + e.path().string());
                if (e.is_regular_file()) entries.push_back(fs::relative(e.path(),root).generic_string());
            }
            std::sort(entries.begin(), entries.end());
            if (json(entries) != directory.at("entries")) throw std::runtime_error("dependency directory entries changed: " + root.string());
        }
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

void applyAuditTrust(nlohmann::json& report, const nlohmann::json& trust) {
    try {
        if (report.at("reviewKey")!=auditReviewKey(report)) throw std::runtime_error("audit report identity mismatch");
    } catch (const std::exception& e) {
        report["passed"]=false; report["manualApproval"]="invalid";
        report["unresolved"].push_back(e.what()); return;
    }
    const auto reviewKey = report.at("reviewKey").get<std::string>();
    bool accepted = false;
    if (trust.contains("reviews") && trust["reviews"].is_object() && trust["reviews"].contains(reviewKey)) {
        auto entry = trust["reviews"][reviewKey];
        accepted = entry.value("project", "") == report.at("project").get<std::string>() && entry.value("accepted", false);
    }
    report["manualApproval"] = report["manual"].empty() ? "not-required" : accepted ? "accepted" : "pending";
    bool dependenciesAccepted = true;
    for (auto& dep : report["dependencies"]) {
        dep["approval"] = report.at("policy").value("strictDependencies", false) && !dep.value("project",false) ? "pending" : "not-required";
        for (const auto& denied : report.at("policy").at("deny"))
            if (denied == dep.at("id")) dep["approval"] = "revoked";
        if (dep["approval"] == "pending" && trust.contains("dependencies") && trust["dependencies"].is_object()) {
            const auto key = dep["key"].get<std::string>();
            if (trust["dependencies"].contains(key)) {
                const auto& accepted = trust["dependencies"][key];
                const auto scope = accepted.value("project", "");
                if (accepted.value("accepted", false) && (scope.empty() || scope == report.at("project")))
                    dep["approval"] = "accepted";
            }
        }
        if (dep["approval"] == "pending" || dep["approval"] == "revoked") dependenciesAccepted = false;
    }
    report["passed"] = report.value("supported", false) && report.value("analysisComplete", false) &&
        report["unresolved"].empty() && report["manualApproval"] != "pending" && dependenciesAccepted;
}
} // namespace topo::check
