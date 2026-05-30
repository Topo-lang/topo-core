#include "topo/Transpile/AdapterResolver.h"

#include "topo/Transpile/TopoSourceBuilder.h"
#include "topo/Transpile/TranspileModelJson.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace topo::transpile {

const char* adapterProvenanceName(AdapterProvenance p) {
    switch (p) {
    case AdapterProvenance::Builtin: return "builtin";
    case AdapterProvenance::TopoApp: return "topo-app";
    case AdapterProvenance::Tpm: return "tpm";
    }
    return "unknown";
}

namespace {

// Lower-case host-language name for diagnostics, matching the CLI `--to`
// spelling parsed by `parseHostLanguage`.
const char* langName(HostLanguage lang) {
    switch (lang) {
    case HostLanguage::Cpp: return "cpp";
    case HostLanguage::Rust: return "rust";
    case HostLanguage::Java: return "java";
    case HostLanguage::Python: return "python";
    case HostLanguage::TypeScript: return "typescript";
    case HostLanguage::Mixed: return "mixed";
    }
    return "unknown";
}

// Deep-clone a Stmt subtree via the existing JSON wire round-trip. The
// TranspileModelJson serializer/deserializer is exhaustive over every node
// kind, so this is a complete copy with no per-node clone code to drift.
StmtPtr cloneStmt(const Stmt& s) {
    return deserializeStmt(serializeStmt(s));
}

// --- Manifest TypeNode / Parameter parsing ---------------------------------
//
// The TranspileModelJson TypeNode/Parameter (de)serializers are file-local
// statics, so the manifest parser carries its own minimal TypeNode reader.
// It covers exactly the fields the signature-consistency check compares
// (nameParts, isConst, ownership, modifier, templateArgs).

TypeNode parseManifestTypeNode(const nlohmann::json& j) {
    TypeNode t;
    if (j.contains("nameParts") && j["nameParts"].is_array()) {
        for (const auto& p : j["nameParts"]) t.nameParts.push_back(p.get<std::string>());
    }
    if (j.value("isConst", false)) t.isConst = true;
    if (j.contains("ownership")) {
        std::string o = j["ownership"].get<std::string>();
        if (o == "owned") t.ownership = OwnershipKind::Owned;
        else if (o == "shared") t.ownership = OwnershipKind::Shared;
        else if (o == "weak") t.ownership = OwnershipKind::Weak;
    }
    if (j.contains("modifier")) {
        std::string m = j["modifier"].get<std::string>();
        if (m == "ref") t.modifier = TypeNode::Ref;
        else if (m == "ptr") t.modifier = TypeNode::Ptr;
    }
    if (j.contains("templateArgs") && j["templateArgs"].is_array()) {
        for (const auto& a : j["templateArgs"]) {
            t.templateArgs.push_back(parseManifestTypeNode(a));
        }
    }
    return t;
}

// Source override priority: tpm > topo-app > builtin. Higher = wins.
int provenanceRank(AdapterProvenance p) {
    switch (p) {
    case AdapterProvenance::Tpm: return 3;
    case AdapterProvenance::TopoApp: return 2;
    case AdapterProvenance::Builtin: return 1;
    }
    return 0;
}

// TypeNode structural equality for the signature-consistency check.
// Compares the observable shape (name parts, modifier, ownership, const,
// template args) — enough to reject a signature-drifted adapter.
bool typeEqual(const TypeNode& a, const TypeNode& b) {
    if (a.isConst != b.isConst) return false;
    if (a.ownership != b.ownership) return false;
    if (a.modifier != b.modifier) return false;
    if (a.nameParts != b.nameParts) return false;
    if (a.templateArgs.size() != b.templateArgs.size()) return false;
    for (size_t i = 0; i < a.templateArgs.size(); ++i) {
        if (!typeEqual(a.templateArgs[i], b.templateArgs[i])) return false;
    }
    return true;
}

// Signature-consistency check: the adapter's declared signature must match the
// leaf's FnDecl signature — return type + positional parameter types.
bool signatureMatches(const AdapterEntry& entry, const TranspileFunction& leaf) {
    if (!typeEqual(entry.signature.returnType, leaf.returnType)) return false;
    if (entry.signature.params.size() != leaf.params.size()) return false;
    for (size_t i = 0; i < leaf.params.size(); ++i) {
        if (!typeEqual(entry.signature.params[i].type, leaf.params[i].type)) {
            return false;
        }
    }
    return true;
}

// Replace the M2 unresolved marker on `fn` with a precise degradation
// description, then install a placeholder body (a single ExprStmt wrapping
// an UnsupportedExpr). Mirrors the existing unsupported / fidelity mechanism
// (the signature-consistency degradation path).
void degradeLeaf(TranspileFunction& fn, const std::string& reason) {
    fn.unsupported.erase(
        std::remove(fn.unsupported.begin(), fn.unsupported.end(),
                    std::string(kLeafUnresolvedMarker)),
        fn.unsupported.end());
    fn.unsupported.push_back(reason);

    auto unsupported = std::make_unique<UnsupportedExpr>();
    unsupported->description = reason;
    auto stmt = std::make_unique<ExprStmt>();
    stmt->expr = std::move(unsupported);
    fn.body.clear();
    fn.body.push_back(std::move(stmt));

    // A missing adapter is "source information unavailable", same class as a
    // source extractor dropping a feature — Inferred, never Recovered
    // (Recovered is decompile-only).
    if (fn.fidelity == Fidelity::Source) fn.fidelity = Fidelity::Inferred;
}

// Install a resolved adapter body onto a leaf, removing the M2 marker.
void installAdapter(TranspileFunction& fn, const AdapterEntry& entry) {
    fn.unsupported.erase(
        std::remove(fn.unsupported.begin(), fn.unsupported.end(),
                    std::string(kLeafUnresolvedMarker)),
        fn.unsupported.end());

    fn.body.clear();
    if (entry.hasOpaque) {
        // Opaque body: the resolved implementation is target-language source
        // that cannot pass through Stmt nodes. Carry it as an UnsupportedExpr
        // whose description is the verbatim source — emitters embed an
        // UnsupportedExpr's payload as-is. The imports it needs are recorded
        // so a later CLI/driver pass can surface them.
        auto raw = std::make_unique<UnsupportedExpr>();
        raw->description = entry.bodyOpaque.code;
        auto stmt = std::make_unique<ExprStmt>();
        stmt->expr = std::move(raw);
        fn.body.push_back(std::move(stmt));
    } else {
        for (const auto& s : entry.bodyModel) {
            fn.body.push_back(cloneStmt(*s));
        }
    }
    // The body is an explicitly declared concrete implementation — exact,
    // not a lossy reconstruction. Fidelity stays Source.
}

} // namespace

// --- AdapterEntry::clone ----------------------------------------------------

AdapterEntry AdapterEntry::clone() const {
    AdapterEntry c;
    c.topoFunction = topoFunction;
    c.targetLanguage = targetLanguage;
    c.signature = signature;
    c.bodyOpaque = bodyOpaque;
    c.hasOpaque = hasOpaque;
    c.provenance = provenance;
    for (const auto& s : bodyModel) {
        c.bodyModel.push_back(cloneStmt(*s));
    }
    return c;
}

// --- AdapterRegistry --------------------------------------------------------

void AdapterRegistry::add(AdapterEntry entry) {
    entries_.push_back(std::move(entry));
}

void AdapterRegistry::addSource(AdapterSource& source) {
    AdapterProvenance prov = source.provenance();
    for (auto& entry : source.enumerate()) {
        entry.provenance = prov;
        entries_.push_back(std::move(entry));
    }
}

// --- ManifestAdapterSource --------------------------------------------------

ManifestAdapterSource ManifestAdapterSource::fromJson(AdapterProvenance prov,
                                                      const std::string& json,
                                                      std::string& outError) {
    std::vector<AdapterEntry> entries;
    try {
        nlohmann::json root = nlohmann::json::parse(json);
        if (!root.is_array()) {
            outError = "adapter manifest must be a top-level JSON array";
            return ManifestAdapterSource(prov, {});
        }
        for (const auto& obj : root) {
            AdapterEntry e;
            e.topoFunction = obj.at("topoFunction").get<std::string>();
            e.targetLanguage =
                parseHostLanguage(obj.at("targetLanguage").get<std::string>());
            if (obj.contains("signature")) {
                const auto& sig = obj["signature"];
                if (sig.contains("returnType")) {
                    e.signature.returnType = parseManifestTypeNode(sig["returnType"]);
                }
                if (sig.contains("params") && sig["params"].is_array()) {
                    for (const auto& p : sig["params"]) {
                        Parameter param;
                        param.name = p.value("name", std::string());
                        if (p.contains("type")) {
                            param.type = parseManifestTypeNode(p["type"]);
                        }
                        e.signature.params.push_back(std::move(param));
                    }
                }
            }
            bool hasModel = obj.contains("bodyModel");
            bool hasOpaqueKey = obj.contains("bodyOpaque");
            if (hasModel == hasOpaqueKey) {
                outError = "adapter entry for '" + e.topoFunction +
                           "' must carry exactly one of bodyModel / bodyOpaque";
                return ManifestAdapterSource(prov, {});
            }
            if (hasOpaqueKey) {
                e.hasOpaque = true;
                const auto& op = obj["bodyOpaque"];
                e.bodyOpaque.code = op.value("code", std::string());
                if (op.contains("imports") && op["imports"].is_array()) {
                    for (const auto& imp : op["imports"]) {
                        e.bodyOpaque.imports.push_back(imp.get<std::string>());
                    }
                }
            } else {
                for (const auto& s : obj["bodyModel"]) {
                    e.bodyModel.push_back(deserializeStmt(s));
                }
            }
            e.provenance = prov;
            entries.push_back(std::move(e));
        }
    } catch (const nlohmann::json::exception& ex) {
        outError = std::string("adapter manifest parse error: ") + ex.what();
        return ManifestAdapterSource(prov, {});
    }
    outError.clear();
    return ManifestAdapterSource(prov, std::move(entries));
}

std::vector<AdapterEntry> ManifestAdapterSource::enumerate() {
    std::vector<AdapterEntry> out;
    for (const auto& e : entries_) {
        out.push_back(e.clone());
    }
    return out;
}

// --- BuiltinAdapterSource (M5) ----------------------------------------------

namespace {

// One builtin entry for every target language: a leaf whose body is the same
// language-agnostic Stmt sequence regardless of `--to`. The toolchain ships
// these compiled-in (no manifest file).

// `topo::identity(x) -> T` — returns its single parameter unchanged.
AdapterEntry builtinIdentity(HostLanguage lang) {
    AdapterEntry e;
    e.topoFunction = "topo::identity";
    e.targetLanguage = lang;
    e.provenance = AdapterProvenance::Builtin;
    // signature: T identity(T x). The resolver's signature-consistency check compares the leaf
    // FnDecl signature against this; identity is generic over `T`, so the
    // builtin declares a `T` type name that matches a leaf declared the same
    // way. (A leaf with a concrete type still matches only if it too uses the
    // exact spelling — the builtin set is intentionally narrow.)
    e.signature.returnType.nameParts = {"T"};
    Parameter p;
    p.name = "x";
    p.type.nameParts = {"T"};
    e.signature.params.push_back(std::move(p));
    auto ref = std::make_unique<VarRefExpr>();
    ref->name = "x";
    auto ret = std::make_unique<ReturnStmt>();
    ret->value = std::move(ref);
    e.bodyModel.push_back(std::move(ret));
    return e;
}

// `topo::zero() -> i64` — returns the integer literal 0.
AdapterEntry builtinZero(HostLanguage lang) {
    AdapterEntry e;
    e.topoFunction = "topo::zero";
    e.targetLanguage = lang;
    e.provenance = AdapterProvenance::Builtin;
    e.signature.returnType.nameParts = {"i64"};
    auto lit = std::make_unique<LiteralExpr>();
    lit->litKind = LiteralKind::Integer;
    lit->value = "0";
    auto ret = std::make_unique<ReturnStmt>();
    ret->value = std::move(lit);
    e.bodyModel.push_back(std::move(ret));
    return e;
}

} // namespace

std::vector<AdapterEntry> BuiltinAdapterSource::enumerate() {
    std::vector<AdapterEntry> out;
    for (HostLanguage lang : {HostLanguage::Cpp, HostLanguage::Rust,
                              HostLanguage::Java, HostLanguage::Python,
                              HostLanguage::TypeScript}) {
        out.push_back(builtinIdentity(lang));
        out.push_back(builtinZero(lang));
    }
    return out;
}

// --- Registry assembly (resolution behavior, step 2) ------------------------

AdapterRegistry assembleAdapterRegistry(const std::string& topoAppManifestPath,
                                        std::vector<std::string>& outWarnings) {
    AdapterRegistry registry;

    // Builtin source — always assembled.
    BuiltinAdapterSource builtin;
    registry.addSource(builtin);

    // topo-app source — assembled only when a manifest path was supplied.
    // A parse error is non-fatal: the builtin source still stands.
    if (!topoAppManifestPath.empty()) {
        std::ifstream file(topoAppManifestPath);
        if (!file) {
            outWarnings.push_back("topo-app adapter manifest not readable: " +
                                  topoAppManifestPath);
        } else {
            std::ostringstream ss;
            ss << file.rdbuf();
            std::string err;
            ManifestAdapterSource source = ManifestAdapterSource::fromJson(
                AdapterProvenance::TopoApp, ss.str(), err);
            if (!err.empty()) {
                outWarnings.push_back("topo-app adapter manifest '" +
                                      topoAppManifestPath + "': " + err);
            } else {
                registry.addSource(source);
            }
        }
    }

    // tpm package source — deliberately not assembled (deferred).
    return registry;
}

// --- AdapterRegistry lookup -------------------------------------------------

std::vector<const AdapterEntry*>
AdapterRegistry::lookup(const std::string& topoFunction,
                        HostLanguage targetLanguage) const {
    std::vector<const AdapterEntry*> hits;
    for (const auto& e : entries_) {
        if (e.topoFunction == topoFunction && e.targetLanguage == targetLanguage) {
            hits.push_back(&e);
        }
    }
    return hits;
}

// --- AdapterResolver --------------------------------------------------------

ResolveStats AdapterResolver::resolve(TranspileModule& module,
                                      HostLanguage targetLanguage) {
    ResolveStats stats;

    for (auto& fn : module.functions) {
        if (!isUnresolvedLeaf(fn)) continue;
        ++stats.leavesSeen;

        // Resolution step 1: query the registry.
        auto candidates = registry_.lookup(fn.qualifiedName, targetLanguage);

        // Resolution step 2: zero candidates → missing.
        if (candidates.empty()) {
            std::string reason = "no adapter for leaf '" + fn.qualifiedName +
                                 "' targeting " + langName(targetLanguage);
            degradeLeaf(fn, reason);
            stats.warnings.push_back(reason);
            ++stats.degraded;
            continue;
        }

        // Resolution step 3+4 combined: select the highest-priority candidate that
        // ALSO satisfies the signature-consistency check. The contract
        // is "among compatible candidates, pick
        // highest priority" — so a higher-priority candidate whose signature
        // does not match must not block a lower-priority compatible one. Walk
        // rank groups top-down; on each group, drop signature-mismatched
        // entries and warn (the adapter author needs to see the regression),
        // then evaluate the remaining set: 1 → install; 0 → fall through to
        // the next-lower rank; >1 → true peer ambiguity, degrade.
        std::vector<int> ranks;
        for (const auto* c : candidates) {
            int r = provenanceRank(c->provenance);
            if (std::find(ranks.begin(), ranks.end(), r) == ranks.end()) {
                ranks.push_back(r);
            }
        }
        std::sort(ranks.begin(), ranks.end(), std::greater<int>());

        const AdapterEntry* chosen = nullptr;
        bool ambiguous = false;
        std::string ambiguityReason;

        for (int rank : ranks) {
            std::vector<const AdapterEntry*> group;
            for (const auto* c : candidates) {
                if (provenanceRank(c->provenance) == rank) group.push_back(c);
            }
            std::vector<const AdapterEntry*> compatible;
            for (const auto* c : group) {
                if (signatureMatches(*c, fn)) {
                    compatible.push_back(c);
                } else {
                    // Higher-priority candidate skipped due to signature
                    // mismatch — diagnose so the adapter author sees it.
                    stats.warnings.push_back(
                        "adapter for leaf '" + fn.qualifiedName +
                        "' from '" + adapterProvenanceName(c->provenance) +
                        "' skipped: signature mismatch");
                }
            }
            if (compatible.size() == 1) {
                chosen = compatible.front();
                break;
            }
            if (compatible.size() > 1) {
                // True ambiguity — multiple signature-matching candidates at
                // the same priority. No objective tie-break.
                ambiguous = true;
                ambiguityReason =
                    "ambiguous adapter for leaf '" + fn.qualifiedName +
                    "' targeting " + langName(targetLanguage) + " (" +
                    std::to_string(compatible.size()) +
                    " candidates at provenance '" +
                    adapterProvenanceName(compatible.front()->provenance) +
                    "')";
                break;
            }
            // compatible.empty() → fall through to the next-lower rank.
        }

        if (ambiguous) {
            degradeLeaf(fn, ambiguityReason);
            stats.warnings.push_back(ambiguityReason);
            ++stats.degraded;
            continue;
        }

        if (!chosen) {
            // Either every candidate's signature mismatched, or no candidate
            // remained after the rank walk. Surface this as a single
            // "no compatible adapter" degradation — the per-candidate skip
            // warnings above already explain why each was rejected.
            std::string reason = "no adapter for leaf '" + fn.qualifiedName +
                                 "' targeting " + langName(targetLanguage) +
                                 " (candidate signature mismatch)";
            degradeLeaf(fn, reason);
            stats.warnings.push_back(reason);
            ++stats.degraded;
            continue;
        }

        // Any candidate at a strictly lower rank than the chosen one is an
        // override (the contract surface that callers already test).
        int chosenRank = provenanceRank(chosen->provenance);
        for (const auto* c : candidates) {
            if (c != chosen && provenanceRank(c->provenance) < chosenRank &&
                signatureMatches(*c, fn)) {
                stats.warnings.push_back(
                    "adapter for leaf '" + fn.qualifiedName +
                    "' from '" + adapterProvenanceName(c->provenance) +
                    "' overridden by '" +
                    adapterProvenanceName(chosen->provenance) + "'");
            }
        }

        installAdapter(fn, *chosen);
        ++stats.resolved;
    }

    return stats;
}

} // namespace topo::transpile
