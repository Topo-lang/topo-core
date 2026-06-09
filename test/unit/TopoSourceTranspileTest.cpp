// Tests for the `.topo`-source transpile frontend (TopoSourceBuilder)
// and the adapter resolver (AdapterResolver).

#include "topo/AST/ASTNode.h"
#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Transpile/AdapterResolver.h"
#include "topo/Transpile/TopoSourceBuilder.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace topo;
using namespace topo::transpile;

namespace {

std::unique_ptr<TopoFile> parseTopo(const std::string& source) {
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    EXPECT_FALSE(diag.hasErrors()) << "unexpected parse errors in test fixture";
    return ast;
}

const TranspileFunction* findFn(const TranspileModule& m, const std::string& qn) {
    for (const auto& f : m.functions) {
        if (f.qualifiedName == qn) return &f;
    }
    return nullptr;
}

// A leaf-function `.topo` and a composite-function `.topo` reused across
// several tests. `run` is an operation-mode composite; `loadData` /
// `transform` / `save` are leaf functions.
constexpr const char* kOperationTopo = R"topo(
namespace data {
  public:
    void run();
    fn run {
      stage<1> loadData() -> raw;
      stage<2> transform(raw) -> result;
      stage<3> save(result);
    }
  protected:
    i64  loadData();
    i64  transform(i64 raw);
    void save(i64 out);
}
)topo";

} // namespace

// =====================================================================
// M2 — TopoSourceBuilder
// =====================================================================

TEST(TopoSourceBuilderM2, CompositeFunctionGetsBodyFromLogicBlock) {
    auto ast = parseTopo(kOperationTopo);
    ASSERT_NE(ast, nullptr);

    TopoSourceBuilder builder;
    BuildResult result = builder.build(*ast);
    ASSERT_TRUE(result.success);

    const TranspileFunction* run = findFn(result.module, "data::run");
    ASSERT_NE(run, nullptr);
    // The composite body must NOT carry the unresolved-leaf marker.
    EXPECT_FALSE(isUnresolvedLeaf(*run));
    // 3 operations → 3 statements (2 VarDecl from `-> binding`, 1 ExprStmt).
    ASSERT_EQ(run->body.size(), 3u);
    EXPECT_EQ(run->body[0]->kind(), Stmt::Kind::VarDecl);
    EXPECT_EQ(run->body[1]->kind(), Stmt::Kind::VarDecl);
    EXPECT_EQ(run->body[2]->kind(), Stmt::Kind::ExprStmt);

    // First op `loadData() -> raw` binds the call result to `raw`.
    auto* vd0 = static_cast<VarDeclStmt*>(run->body[0].get());
    EXPECT_EQ(vd0->name, "raw");
    ASSERT_NE(vd0->init, nullptr);
    EXPECT_EQ(vd0->init->kind(), Expr::Kind::Call);
    EXPECT_EQ(static_cast<transpile::CallExpr*>(vd0->init.get())->callee, "loadData");
}

TEST(TopoSourceBuilderM2, LeafFunctionMarkedUnresolved) {
    auto ast = parseTopo(kOperationTopo);
    ASSERT_NE(ast, nullptr);

    TopoSourceBuilder builder;
    BuildResult result = builder.build(*ast);
    ASSERT_TRUE(result.success);

    // loadData / transform / save are leaves: signature only, no fn block.
    for (const char* qn : {"data::loadData", "data::transform", "data::save"}) {
        const TranspileFunction* leaf = findFn(result.module, qn);
        ASSERT_NE(leaf, nullptr) << qn;
        EXPECT_TRUE(isUnresolvedLeaf(*leaf)) << qn;
        EXPECT_TRUE(leaf->body.empty()) << qn;
    }
}

TEST(TopoSourceBuilderM2, FunctionSignatureCarriedFromFnDecl) {
    auto ast = parseTopo(kOperationTopo);
    TopoSourceBuilder builder;
    BuildResult result = builder.build(*ast);

    const TranspileFunction* transform = findFn(result.module, "data::transform");
    ASSERT_NE(transform, nullptr);
    ASSERT_EQ(transform->params.size(), 1u);
    EXPECT_EQ(transform->params[0].name, "raw");
    // Return type i64 round-trips into nameParts.
    ASSERT_FALSE(transform->returnType.nameParts.empty());
    EXPECT_EQ(transform->returnType.nameParts.back(), "i64");
}

TEST(TopoSourceBuilderM2, PipelineFlowGetsDagOrchestration) {
    // `flow` desugars to a pipeline-mode logic block.
    constexpr const char* kFlowTopo = R"topo(
namespace orders {
  public:
    handler parse(string raw) -> record<id: i64>;
    handler validate(record<id: i64> o) -> record<id: i64>;
    handler persist(record<id: i64> o) -> bool;

    flow pipeline {
      parse -> validate;
      validate -> persist;
      persist -> void;
    }
}
)topo";
    auto ast = parseTopo(kFlowTopo);
    ASSERT_NE(ast, nullptr);

    TopoSourceBuilder builder;
    BuildResult result = builder.build(*ast);
    ASSERT_TRUE(result.success);

    const TranspileFunction* flow = findFn(result.module, "orders::pipeline");
    ASSERT_NE(flow, nullptr);
    EXPECT_FALSE(isUnresolvedLeaf(*flow));
    // 3 distinct nodes → 3 VarDecl + 1 Return (terminal edge `persist -> void`).
    ASSERT_EQ(flow->body.size(), 4u);
    EXPECT_EQ(flow->body[0]->kind(), Stmt::Kind::VarDecl);
    EXPECT_EQ(flow->body[3]->kind(), Stmt::Kind::Return);

    // Each handler is a leaf.
    for (const char* qn : {"orders::parse", "orders::validate", "orders::persist"}) {
        const TranspileFunction* leaf = findFn(result.module, qn);
        ASSERT_NE(leaf, nullptr) << qn;
        EXPECT_TRUE(isUnresolvedLeaf(*leaf)) << qn;
    }
}

TEST(TopoSourceBuilderM2, AssignmentOperationLowersToVarDecl) {
    constexpr const char* kAssignTopo = R"topo(
namespace counter {
  public:
    void tick();
    fn tick {
      stage<1> reset();
      n = 1;
    }
  protected:
    void reset();
}
)topo";
    auto ast = parseTopo(kAssignTopo);
    ASSERT_NE(ast, nullptr);
    TopoSourceBuilder builder;
    BuildResult result = builder.build(*ast);
    ASSERT_TRUE(result.success);

    const TranspileFunction* tick = findFn(result.module, "counter::tick");
    ASSERT_NE(tick, nullptr);
    ASSERT_EQ(tick->body.size(), 2u);
    EXPECT_EQ(tick->body[0]->kind(), Stmt::Kind::ExprStmt); // reset();
    EXPECT_EQ(tick->body[1]->kind(), Stmt::Kind::VarDecl);  // n = 1;
    auto* vd = static_cast<VarDeclStmt*>(tick->body[1].get());
    EXPECT_EQ(vd->name, "n");
    // An integer-literal RHS infers a concrete scalar type, so the emitter
    // renders a typed local (`i64 n = 1;`) instead of a non-compilable
    // untyped declaration (` n = 1;`).
    ASSERT_FALSE(vd->type.nameParts.empty());
    EXPECT_EQ(vd->type.nameParts.front(), "i64");
}

// =====================================================================
// M3 — AdapterResolver
// =====================================================================

namespace {

// Build a one-leaf module: a single unresolved leaf `data::loadData`
// with return type i64, no params.
TranspileModule oneLeafModule(const std::string& qn = "data::loadData") {
    TranspileModule m;
    TranspileFunction fn;
    fn.qualifiedName = qn;
    fn.returnType.nameParts = {"i64"};
    fn.unsupported.push_back(kLeafUnresolvedMarker);
    m.functions.push_back(std::move(fn));
    return m;
}

// A bodyModel adapter entry: `return 0;`.
AdapterEntry returnZeroAdapter(const std::string& qn, HostLanguage lang,
                               AdapterProvenance prov) {
    AdapterEntry e;
    e.topoFunction = qn;
    e.targetLanguage = lang;
    e.signature.returnType.nameParts = {"i64"};
    e.provenance = prov;
    auto lit = std::make_unique<transpile::LiteralExpr>();
    lit->litKind = LiteralKind::Integer;
    lit->value = "0";
    auto ret = std::make_unique<ReturnStmt>();
    ret->value = std::move(lit);
    e.bodyModel.push_back(std::move(ret));
    return e;
}

} // namespace

TEST(AdapterResolverM3, ResolvesLeafFromBodyModelAdapter) {
    TranspileModule m = oneLeafModule();

    AdapterRegistry registry;
    registry.add(returnZeroAdapter("data::loadData", HostLanguage::Cpp,
                                   AdapterProvenance::Builtin));

    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);

    EXPECT_EQ(stats.leavesSeen, 1);
    EXPECT_EQ(stats.resolved, 1);
    EXPECT_EQ(stats.degraded, 0);

    const TranspileFunction* fn = findFn(m, "data::loadData");
    ASSERT_NE(fn, nullptr);
    EXPECT_FALSE(isUnresolvedLeaf(*fn));
    ASSERT_EQ(fn->body.size(), 1u);
    EXPECT_EQ(fn->body[0]->kind(), Stmt::Kind::Return);
    EXPECT_EQ(fn->fidelity, Fidelity::Source); // explicit concrete impl
}

TEST(AdapterResolverM3, MissingAdapterDegradesTraceably) {
    TranspileModule m = oneLeafModule();

    AdapterRegistry registry; // empty
    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Rust);

    EXPECT_EQ(stats.resolved, 0);
    EXPECT_EQ(stats.degraded, 1);
    ASSERT_FALSE(stats.warnings.empty());

    const TranspileFunction* fn = findFn(m, "data::loadData");
    ASSERT_NE(fn, nullptr);
    // M2 marker replaced with a precise diagnostic naming the leaf.
    EXPECT_FALSE(isUnresolvedLeaf(*fn));
    ASSERT_EQ(fn->unsupported.size(), 1u);
    EXPECT_NE(fn->unsupported[0].find("data::loadData"), std::string::npos);
    EXPECT_NE(fn->unsupported[0].find("rust"), std::string::npos);
    // Placeholder body: a single ExprStmt holding an UnsupportedExpr.
    ASSERT_EQ(fn->body.size(), 1u);
    EXPECT_EQ(fn->body[0]->kind(), Stmt::Kind::ExprStmt);
    EXPECT_EQ(fn->fidelity, Fidelity::Inferred);
}

TEST(AdapterResolverM3, WrongTargetLanguageDoesNotMatch) {
    TranspileModule m = oneLeafModule();
    AdapterRegistry registry;
    registry.add(returnZeroAdapter("data::loadData", HostLanguage::Cpp,
                                   AdapterProvenance::Builtin));
    AdapterResolver resolver(registry);
    // Resolving for Rust must NOT pick up the Cpp adapter.
    ResolveStats stats = resolver.resolve(m, HostLanguage::Rust);
    EXPECT_EQ(stats.resolved, 0);
    EXPECT_EQ(stats.degraded, 1);
}

TEST(AdapterResolverM3, SignatureMismatchRejectsAdapter) {
    TranspileModule m = oneLeafModule(); // leaf returns i64
    AdapterRegistry registry;
    AdapterEntry e = returnZeroAdapter("data::loadData", HostLanguage::Cpp,
                                       AdapterProvenance::Builtin);
    e.signature.returnType.nameParts = {"f64"}; // drift: f64 != i64
    registry.add(std::move(e));

    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    // Signature-drifted adapter is untrustworthy → degrade.
    EXPECT_EQ(stats.resolved, 0);
    EXPECT_EQ(stats.degraded, 1);
}

TEST(AdapterResolverM3, HigherPriorityProvenanceOverridesBuiltin) {
    TranspileModule m = oneLeafModule();
    AdapterRegistry registry;
    registry.add(returnZeroAdapter("data::loadData", HostLanguage::Cpp,
                                   AdapterProvenance::Builtin));
    registry.add(returnZeroAdapter("data::loadData", HostLanguage::Cpp,
                                   AdapterProvenance::TopoApp));

    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    // topo-app wins over builtin — resolved, with an override warning.
    EXPECT_EQ(stats.resolved, 1);
    EXPECT_EQ(stats.degraded, 0);
    bool sawOverride = false;
    for (const auto& w : stats.warnings) {
        if (w.find("overridden") != std::string::npos) sawOverride = true;
    }
    EXPECT_TRUE(sawOverride);
}

TEST(AdapterResolverM3, TruePeerAmbiguityDegrades) {
    TranspileModule m = oneLeafModule();
    AdapterRegistry registry;
    // Two candidates at the SAME (highest) provenance → true ambiguity.
    registry.add(returnZeroAdapter("data::loadData", HostLanguage::Cpp,
                                   AdapterProvenance::TopoApp));
    registry.add(returnZeroAdapter("data::loadData", HostLanguage::Cpp,
                                   AdapterProvenance::TopoApp));

    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    EXPECT_EQ(stats.resolved, 0);
    EXPECT_EQ(stats.degraded, 1);

    const TranspileFunction* fn = findFn(m, "data::loadData");
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->unsupported.size(), 1u);
    EXPECT_NE(fn->unsupported[0].find("ambiguous"), std::string::npos);
}

TEST(AdapterResolverM3, FallsBackToLowerPriorityWhenHigherMismatchedSignature) {
    // Contract: "among compatible candidates, pick
    // highest priority". A signature-drifted higher-priority candidate must
    // NOT block a signature-matching lower-priority one — the resolver
    // skips the bad candidate (with a diagnostic) and installs the good one.
    TranspileModule m = oneLeafModule(); // leaf returns i64
    AdapterRegistry registry;

    // tpm candidate with a drifted signature (f64 != i64).
    AdapterEntry bad = returnZeroAdapter("data::loadData", HostLanguage::Cpp,
                                         AdapterProvenance::Tpm);
    bad.signature.returnType.nameParts = {"f64"};
    registry.add(std::move(bad));

    // builtin candidate with the correct signature.
    registry.add(returnZeroAdapter("data::loadData", HostLanguage::Cpp,
                                   AdapterProvenance::Builtin));

    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    EXPECT_EQ(stats.resolved, 1);
    EXPECT_EQ(stats.degraded, 0);

    // The skip-due-to-mismatch must be diagnosed so the adapter author sees
    // their tpm package's regression.
    bool sawSkip = false;
    for (const auto& w : stats.warnings) {
        if (w.find("from 'tpm'") != std::string::npos &&
            w.find("signature mismatch") != std::string::npos) {
            sawSkip = true;
        }
    }
    EXPECT_TRUE(sawSkip) << "expected skip diagnostic for tpm candidate";
}

TEST(AdapterResolverM3, AllCandidatesMismatchedDegrades) {
    // Every candidate's signature mismatches → no compatible adapter at any
    // rank, so degrade. Each skip is still diagnosed individually.
    TranspileModule m = oneLeafModule(); // leaf returns i64
    AdapterRegistry registry;

    AdapterEntry tpmBad = returnZeroAdapter("data::loadData", HostLanguage::Cpp,
                                            AdapterProvenance::Tpm);
    tpmBad.signature.returnType.nameParts = {"f64"};
    registry.add(std::move(tpmBad));

    AdapterEntry builtinBad = returnZeroAdapter("data::loadData",
                                                HostLanguage::Cpp,
                                                AdapterProvenance::Builtin);
    builtinBad.signature.returnType.nameParts = {"f32"};
    registry.add(std::move(builtinBad));

    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    EXPECT_EQ(stats.resolved, 0);
    EXPECT_EQ(stats.degraded, 1);

    int skipCount = 0;
    for (const auto& w : stats.warnings) {
        if (w.find("signature mismatch") != std::string::npos) ++skipCount;
    }
    // Two per-candidate skip warnings + one "no compatible adapter" summary.
    EXPECT_GE(skipCount, 2);
}

TEST(AdapterResolverM3, OpaqueBodyIsInstalled) {
    TranspileModule m = oneLeafModule();
    AdapterRegistry registry;
    AdapterEntry e;
    e.topoFunction = "data::loadData";
    e.targetLanguage = HostLanguage::Python;
    e.signature.returnType.nameParts = {"i64"};
    e.hasOpaque = true;
    e.bodyOpaque.code = "return 7";
    e.provenance = AdapterProvenance::Builtin;
    registry.add(std::move(e));

    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Python);
    EXPECT_EQ(stats.resolved, 1);

    const TranspileFunction* fn = findFn(m, "data::loadData");
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->body.size(), 1u);
    EXPECT_EQ(fn->body[0]->kind(), Stmt::Kind::ExprStmt);
}

TEST(AdapterResolverM3, CompositeFunctionUntouchedByResolver) {
    // A composite function never carries the leaf marker → resolver skips it.
    TranspileModule m;
    TranspileFunction composite;
    composite.qualifiedName = "data::run";
    auto stmt = std::make_unique<ExprStmt>();
    stmt->expr = std::make_unique<VarRefExpr>();
    composite.body.push_back(std::move(stmt));
    m.functions.push_back(std::move(composite));

    AdapterRegistry registry;
    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    EXPECT_EQ(stats.leavesSeen, 0);
    EXPECT_EQ(stats.resolved, 0);
    EXPECT_EQ(stats.degraded, 0);
}

// --- Manifest source --------------------------------------------------

TEST(AdapterResolverM3, ManifestSourceLoadsBodyModelEntry) {
    const std::string manifest = R"json(
[
  {
    "topoFunction": "data::loadData",
    "targetLanguage": "cpp",
    "signature": { "returnType": { "nameParts": ["i64"] }, "params": [] },
    "bodyModel": [
      { "kind": "return", "fidelity": "source",
        "value": { "kind": "literal", "fidelity": "source",
                   "litKind": "integer", "value": "0" } }
    ]
  }
]
)json";
    std::string err;
    auto source = ManifestAdapterSource::fromJson(AdapterProvenance::Builtin,
                                                  manifest, err);
    EXPECT_TRUE(err.empty()) << err;

    AdapterRegistry registry;
    registry.addSource(source);
    EXPECT_EQ(registry.size(), 1u);

    TranspileModule m = oneLeafModule();
    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    EXPECT_EQ(stats.resolved, 1);

    const TranspileFunction* fn = findFn(m, "data::loadData");
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->body.size(), 1u);
    EXPECT_EQ(fn->body[0]->kind(), Stmt::Kind::Return);
}

TEST(AdapterResolverM3, ManifestRejectsBothBodyForms) {
    const std::string manifest = R"json(
[
  { "topoFunction": "x::y", "targetLanguage": "cpp",
    "bodyModel": [], "bodyOpaque": { "code": "" } }
]
)json";
    std::string err;
    ManifestAdapterSource::fromJson(AdapterProvenance::Builtin, manifest, err);
    EXPECT_FALSE(err.empty());
}

TEST(AdapterResolverM3, ManifestRejectsNonArray) {
    std::string err;
    ManifestAdapterSource::fromJson(AdapterProvenance::Builtin, "{}", err);
    EXPECT_FALSE(err.empty());
}

// =====================================================================
// M5 — builtin adapter source + registry assembly
// =====================================================================

TEST(BuiltinAdapterSourceM5, SuppliesEntriesForEveryTargetLanguage) {
    BuiltinAdapterSource source;
    EXPECT_EQ(source.provenance(), AdapterProvenance::Builtin);
    auto entries = source.enumerate();
    // topo::identity + topo::zero, one per 5 target languages.
    EXPECT_EQ(entries.size(), 10u);

    // Every entry is stamped builtin and names one of the two builtin leaves.
    for (const auto& e : entries) {
        EXPECT_EQ(e.provenance, AdapterProvenance::Builtin);
        EXPECT_TRUE(e.topoFunction == "topo::identity" ||
                    e.topoFunction == "topo::zero")
            << e.topoFunction;
        EXPECT_FALSE(e.bodyModel.empty());
    }
}

TEST(BuiltinAdapterSourceM5, BuiltinZeroResolvesAnUnresolvedLeaf) {
    // A leaf `topo::zero` returning i64 — exactly what the builtin supplies.
    TranspileModule m;
    TranspileFunction fn;
    fn.qualifiedName = "topo::zero";
    fn.returnType.nameParts = {"i64"};
    fn.unsupported.push_back(kLeafUnresolvedMarker);
    m.functions.push_back(std::move(fn));

    std::vector<std::string> warnings;
    AdapterRegistry registry = assembleAdapterRegistry("", {}, warnings);
    EXPECT_TRUE(warnings.empty());

    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    EXPECT_EQ(stats.resolved, 1);
    EXPECT_EQ(stats.degraded, 0);

    const TranspileFunction* fn2 = findFn(m, "topo::zero");
    ASSERT_NE(fn2, nullptr);
    EXPECT_FALSE(isUnresolvedLeaf(*fn2));
    ASSERT_EQ(fn2->body.size(), 1u);
    EXPECT_EQ(fn2->body[0]->kind(), Stmt::Kind::Return);
}

TEST(AssembleAdapterRegistryM5, LoadsTopoAppManifestAndOverridesBuiltin) {
    // A topo-app manifest re-supplying `topo::zero` for Cpp. topo-app
    // provenance outranks builtin, so it wins with an override warning.
    const std::string manifest = R"json(
[
  { "topoFunction": "topo::zero", "targetLanguage": "cpp",
    "signature": { "returnType": { "nameParts": ["i64"] }, "params": [] },
    "bodyModel": [
      { "kind": "return", "fidelity": "source",
        "value": { "kind": "literal", "fidelity": "source",
                   "litKind": "integer", "value": "99" } }
    ] }
]
)json";
    auto tmp = std::filesystem::temp_directory_path() /
               "topo-m5-assemble-manifest.json";
    { std::ofstream(tmp) << manifest; }

    std::vector<std::string> warnings;
    AdapterRegistry registry =
        assembleAdapterRegistry(tmp.string(), {}, warnings);
    EXPECT_TRUE(warnings.empty());

    auto candidates = registry.lookup("topo::zero", HostLanguage::Cpp);
    // builtin + topo-app both present for this key.
    EXPECT_EQ(candidates.size(), 2u);

    TranspileModule m;
    TranspileFunction fn;
    fn.qualifiedName = "topo::zero";
    fn.returnType.nameParts = {"i64"};
    fn.unsupported.push_back(kLeafUnresolvedMarker);
    m.functions.push_back(std::move(fn));

    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    EXPECT_EQ(stats.resolved, 1);
    bool sawOverride = false;
    for (const auto& w : stats.warnings) {
        if (w.find("overridden") != std::string::npos) sawOverride = true;
    }
    EXPECT_TRUE(sawOverride) << "topo-app source should override builtin";

    std::filesystem::remove(tmp);
}

TEST(AssembleAdapterRegistryM5, BadManifestPathWarnsButKeepsBuiltin) {
    std::vector<std::string> warnings;
    AdapterRegistry registry =
        assembleAdapterRegistry("/no/such/adapter/manifest.json", {}, warnings);
    // Non-fatal: a warning is recorded and the builtin source still stands.
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(registry.lookup("topo::zero", HostLanguage::Cpp).size(), 0u);
}

// =====================================================================
// M6 — tpm adapter source (the previously-deferred third source)
//
// A tpm package supplies leaf adapters via a JSON manifest (same schema as
// the topo-app source) under its `adapters/` directory. topo-core never
// discovers tpm packages itself; the caller hands assembleAdapterRegistry
// a (package, manifest-path) list. tpm outranks topo-app and builtin, and
// every diagnostic naming a tpm entry reads `tpm:<package>`.
// =====================================================================

namespace {

// The integer literal returned by a single-statement `return <int>;` leaf
// body, or a sentinel when the body is not that shape. Lets a test prove
// *which* source's body was installed (each source returns a distinct int).
std::string returnedIntLiteral(const TranspileFunction& fn) {
    // Qualify ReturnStmt / LiteralExpr: both names also exist as AST nodes in
    // `topo::` (ASTNode.h), so the unqualified forms are ambiguous here.
    if (fn.body.size() != 1) return "<not-single-stmt>";
    if (fn.body[0]->kind() != Stmt::Kind::Return) return "<not-return>";
    const auto* ret =
        static_cast<const topo::transpile::ReturnStmt*>(fn.body[0].get());
    if (!ret->value || ret->value->kind() != Expr::Kind::Literal)
        return "<not-literal>";
    return static_cast<const topo::transpile::LiteralExpr*>(ret->value.get())
        ->value;
}

// A one-entry adapter manifest for a leaf returning the integer `val`, with
// declared return type `retType` (use a non-matching type to force a
// signature mismatch). Same JSON schema the builtin/topo-app sources use.
std::string leafManifest(const std::string& qn, const std::string& lang,
                         const std::string& retType, const std::string& val) {
    return std::string("[{")
        + "\"topoFunction\":\"" + qn + "\","
        + "\"targetLanguage\":\"" + lang + "\","
        + "\"signature\":{\"returnType\":{\"nameParts\":[\"" + retType +
          "\"]},\"params\":[]},"
        + "\"bodyModel\":[{\"kind\":\"return\",\"fidelity\":\"source\","
        +   "\"value\":{\"kind\":\"literal\",\"fidelity\":\"source\","
        +   "\"litKind\":\"integer\",\"value\":\"" + val + "\"}}]"
        + "}]";
}

std::filesystem::path writeTempManifest(const std::string& name,
                                        const std::string& json) {
    auto p = std::filesystem::temp_directory_path() / name;
    { std::ofstream(p) << json; }
    return p;
}

bool anyWarningContains(const std::vector<std::string>& ws,
                        const std::string& needle) {
    for (const auto& w : ws)
        if (w.find(needle) != std::string::npos) return true;
    return false;
}

} // namespace

TEST(TpmAdapterSourceM6, ProvenanceLabelReadsTpmColonPackage) {
    AdapterEntry tpmPkg;
    tpmPkg.provenance = AdapterProvenance::Tpm;
    tpmPkg.provenancePackage = "my-org/asio-bridge";
    EXPECT_EQ(adapterProvenanceLabel(tpmPkg), "tpm:my-org/asio-bridge");

    AdapterEntry builtin; // default provenance, no package
    EXPECT_EQ(adapterProvenanceLabel(builtin), "builtin");

    AdapterEntry tpmNoPkg; // tpm but unlabeled → bare source-kind name
    tpmNoPkg.provenance = AdapterProvenance::Tpm;
    EXPECT_EQ(adapterProvenanceLabel(tpmNoPkg), "tpm");
}

TEST(TpmAdapterSourceM6, ResolvesLeafAndStampsPackageLabel) {
    auto path = writeTempManifest("topo-m6-tpm-basic.json",
                                  leafManifest("data::loadData", "cpp", "i64", "7"));
    std::vector<std::string> warnings;
    AdapterRegistry registry = assembleAdapterRegistry(
        "", {{"my-org/asio-bridge", path.string()}}, warnings);
    EXPECT_TRUE(warnings.empty())
        << (warnings.empty() ? std::string() : warnings.front());

    // data::loadData is not a builtin leaf → only the tpm candidate exists,
    // stamped with provenance Tpm and the package name.
    auto cands = registry.lookup("data::loadData", HostLanguage::Cpp);
    ASSERT_EQ(cands.size(), 1u);
    EXPECT_EQ(cands[0]->provenance, AdapterProvenance::Tpm);
    EXPECT_EQ(cands[0]->provenancePackage, "my-org/asio-bridge");

    TranspileModule m = oneLeafModule(); // data::loadData, i64
    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    EXPECT_EQ(stats.resolved, 1);
    EXPECT_EQ(stats.degraded, 0);
    const TranspileFunction* fn = findFn(m, "data::loadData");
    ASSERT_NE(fn, nullptr);
    EXPECT_FALSE(isUnresolvedLeaf(*fn));
    EXPECT_EQ(returnedIntLiteral(*fn), "7");

    std::filesystem::remove(path);
}

TEST(TpmAdapterSourceM6, OverridesBuiltin) {
    // tpm re-supplies the builtin leaf `topo::zero` with a distinct body (7).
    auto path = writeTempManifest("topo-m6-tpm-over-builtin.json",
                                  leafManifest("topo::zero", "cpp", "i64", "7"));
    std::vector<std::string> warnings;
    AdapterRegistry registry = assembleAdapterRegistry(
        "", {{"my-org/asio-bridge", path.string()}}, warnings);
    EXPECT_TRUE(warnings.empty());

    auto cands = registry.lookup("topo::zero", HostLanguage::Cpp);
    EXPECT_EQ(cands.size(), 2u); // builtin + tpm

    TranspileModule m = oneLeafModule("topo::zero");
    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    EXPECT_EQ(stats.resolved, 1);
    const TranspileFunction* fn = findFn(m, "topo::zero");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(returnedIntLiteral(*fn), "7"); // tpm body, not builtin's 0
    EXPECT_TRUE(anyWarningContains(
        stats.warnings, "overridden by 'tpm:my-org/asio-bridge'"))
        << "tpm overriding builtin must be diagnosed by package name";

    std::filesystem::remove(path);
}

TEST(TpmAdapterSourceM6, OverridesTopoAppAndBuiltin) {
    // All three sources supply topo::zero with distinct bodies; tpm wins.
    auto topoApp = writeTempManifest("topo-m6-topoapp.json",
                                     leafManifest("topo::zero", "cpp", "i64", "99"));
    auto tpm = writeTempManifest("topo-m6-tpm-top.json",
                                 leafManifest("topo::zero", "cpp", "i64", "7"));
    std::vector<std::string> warnings;
    AdapterRegistry registry = assembleAdapterRegistry(
        topoApp.string(), {{"my-org/asio-bridge", tpm.string()}}, warnings);
    EXPECT_TRUE(warnings.empty());

    auto cands = registry.lookup("topo::zero", HostLanguage::Cpp);
    EXPECT_EQ(cands.size(), 3u); // builtin + topo-app + tpm

    TranspileModule m = oneLeafModule("topo::zero");
    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    EXPECT_EQ(stats.resolved, 1);
    const TranspileFunction* fn = findFn(m, "topo::zero");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(returnedIntLiteral(*fn), "7"); // tpm over topo-app(99) and builtin(0)

    int overridesByTpm = 0;
    for (const auto& w : stats.warnings)
        if (w.find("overridden by 'tpm:my-org/asio-bridge'") != std::string::npos)
            ++overridesByTpm;
    EXPECT_EQ(overridesByTpm, 2) << "tpm overrides both topo-app and builtin";

    std::filesystem::remove(topoApp);
    std::filesystem::remove(tpm);
}

TEST(TpmAdapterSourceM6, SignatureMismatchSkippedAndDiagnosed) {
    // tpm supplies topo::zero with a mismatched signature (f64 return);
    // topo-app supplies a matching one. tpm must be skipped (and diagnosed
    // by name), the matching topo-app candidate used — never silently.
    auto topoApp = writeTempManifest("topo-m6-mismatch-topoapp.json",
                                     leafManifest("topo::zero", "cpp", "i64", "99"));
    auto tpm = writeTempManifest("topo-m6-mismatch-tpm.json",
                                 leafManifest("topo::zero", "cpp", "f64", "7"));
    std::vector<std::string> warnings;
    AdapterRegistry registry = assembleAdapterRegistry(
        topoApp.string(), {{"my-org/asio-bridge", tpm.string()}}, warnings);
    EXPECT_TRUE(warnings.empty());

    TranspileModule m = oneLeafModule("topo::zero"); // i64 return
    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    EXPECT_EQ(stats.resolved, 1);
    EXPECT_EQ(stats.degraded, 0);
    const TranspileFunction* fn = findFn(m, "topo::zero");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(returnedIntLiteral(*fn), "99"); // topo-app, since tpm mismatched
    EXPECT_TRUE(anyWarningContains(
        stats.warnings,
        "from 'tpm:my-org/asio-bridge' skipped: signature mismatch"))
        << "a signature-drifted tpm adapter must be diagnosed, never silent";

    std::filesystem::remove(topoApp);
    std::filesystem::remove(tpm);
}

TEST(TpmAdapterSourceM6, TwoTpmPackagesAreTrueAmbiguityNeverSilent) {
    // Two equal-priority tpm packages supply the same leaf → true ambiguity:
    // degrade (never silently pick one, never fall through to builtin), and
    // name both colliding packages.
    auto a = writeTempManifest("topo-m6-amb-a.json",
                               leafManifest("topo::zero", "cpp", "i64", "7"));
    auto b = writeTempManifest("topo-m6-amb-b.json",
                               leafManifest("topo::zero", "cpp", "i64", "8"));
    std::vector<std::string> warnings;
    AdapterRegistry registry = assembleAdapterRegistry(
        "", {{"org/a", a.string()}, {"org/b", b.string()}}, warnings);
    EXPECT_TRUE(warnings.empty());

    TranspileModule m = oneLeafModule("topo::zero");
    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    EXPECT_EQ(stats.resolved, 0);
    EXPECT_EQ(stats.degraded, 1);
    const TranspileFunction* fn = findFn(m, "topo::zero");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->fidelity, Fidelity::Inferred);

    bool ambiguous = false, namesA = false, namesB = false;
    for (const auto& w : stats.warnings) {
        if (w.find("ambiguous adapter for leaf 'topo::zero'") != std::string::npos) {
            ambiguous = true;
            if (w.find("tpm:org/a") != std::string::npos) namesA = true;
            if (w.find("tpm:org/b") != std::string::npos) namesB = true;
        }
    }
    EXPECT_TRUE(ambiguous);
    EXPECT_TRUE(namesA && namesB)
        << "ambiguity diagnostic must name both colliding tpm packages";

    std::filesystem::remove(a);
    std::filesystem::remove(b);
}

TEST(TpmAdapterSourceM6, ParseErrorIsNonFatalAndKeepsOtherSources) {
    auto bad = writeTempManifest("topo-m6-badjson.json", "{ not valid json ]");
    std::vector<std::string> warnings;
    AdapterRegistry registry = assembleAdapterRegistry(
        "", {{"org/broken", bad.string()}}, warnings);
    ASSERT_FALSE(warnings.empty());
    EXPECT_TRUE(anyWarningContains(warnings, "org/broken"))
        << "the warning must name the offending package";

    // builtin still resolves topo::zero — a broken tpm manifest is non-fatal.
    TranspileModule m = oneLeafModule("topo::zero");
    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    EXPECT_EQ(stats.resolved, 1);
    const TranspileFunction* fn = findFn(m, "topo::zero");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(returnedIntLiteral(*fn), "0"); // builtin

    std::filesystem::remove(bad);
}

TEST(TpmAdapterSourceM6, UnreadablePathIsNonFatalAndKeepsOtherSources) {
    std::vector<std::string> warnings;
    AdapterRegistry registry = assembleAdapterRegistry(
        "", {{"org/missing", "/no/such/tpm/adapter/manifest.json"}}, warnings);
    ASSERT_FALSE(warnings.empty());
    EXPECT_TRUE(anyWarningContains(warnings, "org/missing"));
    EXPECT_NE(registry.lookup("topo::zero", HostLanguage::Cpp).size(), 0u);
}

TEST(TpmAdapterSourceM6, EmptyTpmListLeavesBuiltinAndTopoAppUnchanged) {
    // Regression: with no tpm manifests, behaviour is byte-for-byte the
    // legacy builtin + topo-app path (topo-app over builtin, no tpm entries).
    auto topoApp = writeTempManifest("topo-m6-regress-topoapp.json",
                                     leafManifest("topo::zero", "cpp", "i64", "99"));
    std::vector<std::string> warnings;
    AdapterRegistry registry =
        assembleAdapterRegistry(topoApp.string(), {}, warnings);
    EXPECT_TRUE(warnings.empty());

    auto cands = registry.lookup("topo::zero", HostLanguage::Cpp);
    EXPECT_EQ(cands.size(), 2u); // builtin + topo-app, no tpm
    for (const auto* c : cands)
        EXPECT_NE(c->provenance, AdapterProvenance::Tpm);

    TranspileModule m = oneLeafModule("topo::zero");
    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(m, HostLanguage::Cpp);
    EXPECT_EQ(stats.resolved, 1);
    const TranspileFunction* fn = findFn(m, "topo::zero");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(returnedIntLiteral(*fn), "99"); // topo-app over builtin, as before

    std::filesystem::remove(topoApp);
}
