#ifndef TOPO_TRANSPILE_ADAPTERRESOLVER_H
#define TOPO_TRANSPILE_ADAPTERRESOLVER_H

#include "topo/AST/ASTNode.h"
#include "topo/Basic/HostLanguage.h"
#include "topo/Transpile/TranspileModel.h"
#include <string>
#include <vector>

namespace topo::transpile {

// ---------------------------------------------------------------------------
// Adapter model — leaf-function filling for the `.topo`-source transpile path.
//
// An *adapter* is a binding from a declared *leaf* function (a `.topo` FnDecl
// with no `fn` logic block) to a concrete cross-language implementation. The
// upstream builder (`TopoSourceBuilder`) leaves every leaf function with an
// unresolved-body marker; the `AdapterResolver` here walks those leaves and
// fills the body from the adapter registry, or applies a traceable
// degradation when no adapter resolves.
//
// This is a `topo-transpile`-subsystem facility — NOT a `.topo` syntax
// extension. The registry is populated by `AdapterSource`s; the builtin
// source is always assembled, the topo-app source is loaded from a manifest
// passed via `--adapters`, and the tpm source is deferred.
// ---------------------------------------------------------------------------

// --- Adapter entry schema ---------------------------------------------------

// Source-provenance tag of an adapter entry. Drives the fixed override
// priority used to break a multi-candidate match.
enum class AdapterProvenance {
    Builtin,  // toolchain-bundled standard leaf implementations
    TopoApp,  // topo-app product-layer leaf implementations
    Tpm       // tpm package source (assembled from --tpm-adapters manifests;
              // carries provenancePackage for the `tpm:<pkg>` diagnostic label)
};

const char* adapterProvenanceName(AdapterProvenance p);

// Canonicalized leaf-function signature, used for the signature-consistency
// check the resolver runs before an adapter body is accepted.
struct AdapterSignature {
    TypeNode returnType;
    std::vector<Parameter> params;
};

// An opaque, single-target-language implementation body: target-language
// source for the function body (signature line excluded) plus the import /
// #include / use lines it needs. Used only when a language-agnostic Stmt
// body cannot express the implementation.
struct OpaqueBody {
    std::string code;
    std::vector<std::string> imports;
};

// One adapter entry: the minimal registration unit. Exactly one of
// `bodyModel` / `bodyOpaque` is populated (`hasOpaque` discriminates).
struct AdapterEntry {
    std::string topoFunction;         // adapted leaf's qualified name (e.g. "net::parseUrl")
    HostLanguage targetLanguage = HostLanguage::Cpp;
    AdapterSignature signature;       // for the resolve-time consistency check
    std::vector<StmtPtr> bodyModel;   // language-agnostic body (preferred form)
    OpaqueBody bodyOpaque;            // escape-hatch body (single target language)
    bool hasOpaque = false;           // true ⇒ use bodyOpaque, false ⇒ use bodyModel
    AdapterProvenance provenance = AdapterProvenance::Builtin;
    // For a tpm-sourced entry: the package name, so diagnostics read
    // `tpm:<pkg>` (the `provenance` enum alone only distinguishes the three
    // source kinds). Empty for builtin / topo-app entries.
    std::string provenancePackage;

    AdapterEntry() = default;
    AdapterEntry(AdapterEntry&&) = default;
    AdapterEntry& operator=(AdapterEntry&&) = default;
    AdapterEntry(const AdapterEntry&) = delete; // StmtPtr is move-only
    AdapterEntry& operator=(const AdapterEntry&) = delete;

    AdapterEntry clone() const;
};

// Human-readable provenance label for diagnostics. For a tpm entry that
// carries a package name this is `tpm:<pkg>`; otherwise the bare
// source-kind name (`builtin` / `topo-app` / `tpm`).
std::string adapterProvenanceLabel(const AdapterEntry& entry);

// --- Adapter source ---------------------------------------------------------

// A supplier of adapter entries. The three concrete source kinds (builtin /
// topo-app / tpm) each implement this; the resolver treats all three
// identically — only `provenance` and assembly priority differ.
class AdapterSource {
public:
    virtual ~AdapterSource() = default;
    // List every adapter entry this source can supply.
    virtual std::vector<AdapterEntry> enumerate() = 0;
    // Provenance written onto every entry this source supplies.
    virtual AdapterProvenance provenance() const = 0;
};

// --- Manifest-backed adapter source -----------------------------------------

// An AdapterSource backed by a JSON manifest: a top-level array of adapter-
// entry objects. `bodyModel` Stmt nodes reuse the TranspileModelJson wire
// format (no new wire format). The builtin / topo-app / tpm sources all
// load through this same parser; they differ only in `provenance` and in
// where the manifest file lives.
//
// Manifest entry object fields:
//   "topoFunction"   : string  (required)
//   "targetLanguage" : string  (required — cpp/rust/java/python/typescript)
//   "signature"      : { "returnType": <TypeNode>, "params": [<Parameter>] }
//   "bodyModel"      : [<Stmt>]   (exclusive with bodyOpaque)
//   "bodyOpaque"     : { "code": string, "imports": [string] }
class ManifestAdapterSource : public AdapterSource {
public:
    ManifestAdapterSource(AdapterProvenance prov, std::vector<AdapterEntry> entries)
        : provenance_(prov), entries_(std::move(entries)) {}

    // Parse a manifest JSON string. On a parse error `outError` is set and
    // an empty source is returned (the caller decides whether that is fatal).
    // `packageName`, when non-empty, is stamped onto every entry's
    // `provenancePackage` (used by tpm sources so diagnostics read
    // `tpm:<pkg>`); builtin / topo-app sources leave it empty.
    static ManifestAdapterSource fromJson(AdapterProvenance prov,
                                          const std::string& json,
                                          std::string& outError,
                                          const std::string& packageName = "");

    std::vector<AdapterEntry> enumerate() override;
    AdapterProvenance provenance() const override { return provenance_; }

private:
    AdapterProvenance provenance_;
    std::vector<AdapterEntry> entries_;
};

// --- Adapter registry -------------------------------------------------------

// In-memory query face the resolver consults. Built once per transpile run
// from the assembled sources, then read-only. A key may carry several
// candidates (one per source); the resolver's fixed override priority +
// ambiguity rules pick one.
class AdapterRegistry {
public:
    // Ingest every entry a source enumerates, stamping it with the source's
    // provenance.
    void addSource(AdapterSource& source);

    // Directly register one entry (used by builtin sources and by tests).
    void add(AdapterEntry entry);

    // All candidates for `(topoFunction, targetLanguage)`, in insertion
    // order. Empty when nothing matches.
    std::vector<const AdapterEntry*> lookup(const std::string& topoFunction,
                                            HostLanguage targetLanguage) const;

    size_t size() const { return entries_.size(); }

private:
    std::vector<AdapterEntry> entries_;
};

// --- Builtin adapter source -------------------------------------------------

// The toolchain-bundled adapter source. Unlike the manifest-backed sources,
// the builtin set is compiled directly into the `topo-transpile` subsystem
// — linked and shipped with the tool — so it has no external file dependency
// and is always assembled.
//
// It supplies a small set of standard leaf implementations whose bodies are
// language-agnostic Stmt sequences, so the resolved fidelity stays Source and
// every one of the 5 emitters can render them. The set is deliberately
// minimal — this is a `product`-layer convenience, not a cross-language
// standard library.
class BuiltinAdapterSource : public AdapterSource {
public:
    std::vector<AdapterEntry> enumerate() override;
    AdapterProvenance provenance() const override { return AdapterProvenance::Builtin; }
};

// One tpm adapter manifest to assemble: the package it came from (for the
// `tpm:<pkg>` provenance label) plus the path to its JSON adapter manifest
// (typically a file under the package's `adapters/` directory). topo-core
// does NOT discover tpm packages itself — it never depends on topo-tpm; the
// caller (the CLI, or a future `topo-build` integration) resolves installed
// packages and hands their adapter-manifest paths in here.
struct TpmAdapterManifestRef {
    std::string package;  // package name, e.g. "my-org/asio-bridge"
    std::string path;     // path to a JSON adapter manifest
};

// Assemble the standard adapter registry for a `.topo`-source transpile run.
// This is the second step of the adapter lifecycle within a transpile run
// (registry assembly between the source pass and the resolver walk).
//
//   - The builtin source is always ingested.
//   - When `topoAppManifestPath` is non-empty, it is loaded as the topo-app
//     adapter source (an `AdapterSource` of provenance `TopoApp`). A topo-app
//     project emits this manifest; `topo-transpile` receives the path via the
//     `--adapters` CLI flag. A parse error is reported through `outWarnings`
//     (non-fatal — the builtin source still stands).
//   - Each entry in `tpmManifests` is loaded as a tpm adapter source
//     (provenance `Tpm`, stamped with the package name so diagnostics read
//     `tpm:<pkg>`). tpm outranks topo-app and builtin (§3.4). An unreadable
//     path or a parse error is reported through `outWarnings` and skipped
//     (non-fatal — the other sources still stand).
//
// Returns the assembled registry by value.
AdapterRegistry assembleAdapterRegistry(
    const std::string& topoAppManifestPath,
    const std::vector<TpmAdapterManifestRef>& tpmManifests,
    std::vector<std::string>& outWarnings);

// --- Resolver ---------------------------------------------------------------

struct ResolveStats {
    int leavesSeen = 0;
    int resolved = 0;        // leaves filled from an adapter
    int degraded = 0;        // leaves left with a placeholder body
    // Override / degradation diagnostics. Bare messages — the CLI / driver
    // prefixes "warning:" once when surfacing them.
    std::vector<std::string> warnings;
};

class AdapterResolver {
public:
    explicit AdapterResolver(const AdapterRegistry& registry) : registry_(registry) {}

    // Walk every unresolved leaf in `module` (per `isUnresolvedLeaf`) and
    // resolve it against the registry for `targetLanguage`:
    //   - exactly one usable candidate ⇒ body filled, marker removed,
    //     fidelity stays Source;
    //   - zero / signature-mismatched / truly-ambiguous ⇒ placeholder body +
    //     a precise `unsupported` entry (the unresolved-leaf marker is
    //     replaced), the fidelity is downgraded to Inferred.
    // Returns aggregate counts + diagnostics.
    ResolveStats resolve(TranspileModule& module, HostLanguage targetLanguage);

private:
    const AdapterRegistry& registry_;
};

} // namespace topo::transpile

#endif // TOPO_TRANSPILE_ADAPTERRESOLVER_H
