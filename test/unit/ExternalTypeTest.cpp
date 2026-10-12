#include "topo/Basic/Diagnostic.h"
#include "topo/Build/SymbolTableJson.h"
#include "topo/Check/CompletenessCheck.h"
#include "topo/Check/ContainmentCheck.h"
#include "topo/Check/PurityCheck.h"
#include "topo/Format/Formatter.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Sema/SemanticAnalyzer.h"
#include "topo/Transpile/ModelOptimizer.h"
#include "topo/Transpile/TopoSourceBuilder.h"
#include "topo/Transpile/TranspileModelJson.h"
#include <gtest/gtest.h>
#include <sstream>

using namespace topo;
using namespace topo::check;
namespace {
constexpr const char* externalSource = R"(
namespace app {
public:
  external type Log {
  public:
    Log();
    Log(i64 capacity);
    ~Log();
    void write();
    static i64 count;
    fn operator+(Log left, Log right) -> Log;
  private:
    void helper();
    type Sink { public: void emit(); }
  }
  type Other { public: void write(); }
  type Child : public Log { public: void write(); }
  void write();
}
)";
std::unique_ptr<TopoFile> parseExternal(const std::string& source, DiagnosticEngine& diag) {
    Lexer lexer(source, "external.topo", diag);
    Parser parser(lexer, diag);
    return parser.parseTopoFile();
}
SymbolTable analyzeExternal(const std::string& source = externalSource) {
    DiagnosticEngine diag;
    auto ast = parseExternal(source, diag);
    EXPECT_NE(ast, nullptr);
    SemanticAnalyzer sema(diag);
    auto table = ast ? sema.analyze(*ast) : SymbolTable{};
    std::ostringstream diagnostics;
    diag.print(diagnostics);
    EXPECT_FALSE(diag.hasErrors()) << diagnostics.str();
    return table;
}
DetectedCallSite site(const std::string& caller, const std::string& callee = "fopen") {
    DetectedCallSite s;
    s.callerQualifiedName = caller;
    s.calleePattern = callee;
    s.unsafeLevel = UnsafeLevel::System;
    s.file = "mixed.cpp";
    s.line = 10;
    return s;
}
}

TEST(ExternalType, OwnMembersSpecialEntriesAndNestedTypeInheritBoundary) {
    const auto symbols = analyzeExternal();
    const auto* cls = symbols.findClassSymbol("app::Log");
    ASSERT_NE(cls, nullptr);
    EXPECT_TRUE(cls->isExternal);
    EXPECT_TRUE(cls->explicitlyExternal);
    EXPECT_EQ(cls->externalOwner, "app::Log");
    for (const auto* name : {"app::Log::write", "app::Log::helper", "app::Log::Log/0",
                             "app::Log::Log/1", "app::Log::~Log", "app::Log::operator+",
                             "app::Log::Sink::emit"}) {
        const auto* member = symbols.findFunction(name);
        ASSERT_NE(member, nullptr) << name;
        EXPECT_TRUE(member->isExternal) << name;
        EXPECT_FALSE(member->explicitlyExternal) << name;
        EXPECT_EQ(member->externalOwner, "app::Log") << name;
    }
    ASSERT_EQ(cls->memberVars.size(), 1u);
    EXPECT_EQ(cls->memberVars[0].owningType, "app::Log");
    EXPECT_TRUE(cls->memberVars[0].isExternal);
    ASSERT_EQ(cls->operatorOverloads.size(), 1u);
    EXPECT_TRUE(cls->operatorOverloads[0].isExternal);
    const auto* nested = symbols.findClassSymbol("app::Log::Sink");
    ASSERT_NE(nested, nullptr);
    EXPECT_TRUE(nested->isExternal);
    EXPECT_FALSE(nested->explicitlyExternal);
    EXPECT_EQ(nested->enclosingType, "app::Log");
}

TEST(ExternalType, ClassOwnershipDoesNotSpreadToInheritanceOrSameNames) {
    const auto symbols = analyzeExternal();
    for (const auto* name : {"app::write", "app::Other::write", "app::Child::write"}) {
        ASSERT_NE(symbols.findFunction(name), nullptr);
        EXPECT_FALSE(symbols.findFunction(name)->isExternal) << name;
        EXPECT_EQ(symbols.externalTypeForMember(name), nullptr) << name;
    }
    EXPECT_NE(symbols.externalTypeForMember("app::Log::<static-initializer>"), nullptr);
    EXPECT_NE(symbols.externalTypeForMember("app::Log::undeclared_private_helper"), nullptr);
    EXPECT_EQ(symbols.externalTypeForMember("fake::Log::write"), nullptr);
    EXPECT_EQ(symbols.externalTypeForMember("app::LogFake::write"), nullptr);
    EXPECT_EQ(symbols.externalTypeForMember("app::Log::Unknown::write"), nullptr);
}

TEST(ExternalType, EffectiveExternalConflictsWithComptime) {
    for (const auto* source : {
        "namespace app { public: external comptime type Log { public: void write(); } }",
        "namespace app { public: external type Log { public: comptime void write(); } }",
        "namespace app { public: external type Log { public: type Inner { public: comptime void write(); } } }"
    }) {
        DiagnosticEngine diag;
        auto ast = parseExternal(source, diag);
        ASSERT_NE(ast, nullptr);
        ASSERT_FALSE(diag.hasErrors());
        SemanticAnalyzer sema(diag);
        sema.analyze(*ast);
        ASSERT_TRUE(diag.hasErrors());
        std::ostringstream messages;
        diag.print(messages);
        EXPECT_NE(messages.str().find("mutually exclusive"), std::string::npos);
    }
}

TEST(ExternalType, FunctionOnlyBoundaryAndExplicitMemberRemainCompatible) {
    const auto symbols = analyzeExternal(
        "namespace app { public: external void free_io(); type Local { public: external void write(); } }");
    EXPECT_TRUE(symbols.findFunction("app::free_io")->explicitlyExternal);
    EXPECT_TRUE(symbols.findFunction("app::Local::write")->isExternal);
    EXPECT_TRUE(symbols.findFunction("app::Local::write")->explicitlyExternal);
    EXPECT_FALSE(symbols.findClassSymbol("app::Local")->isExternal);
}

TEST(ExternalType, FormatterPreservesBoundaryAndNestedTypes) {
    DiagnosticEngine diag;
    auto ast = parseExternal(externalSource, diag);
    ASSERT_FALSE(diag.hasErrors());
    format::Formatter formatter;
    auto output = formatter.format(*ast, externalSource, {});
    EXPECT_NE(output.find("external type Log"), std::string::npos);
    EXPECT_NE(output.find("type Sink"), std::string::npos);
    auto table = analyzeExternal(output);
    EXPECT_TRUE(table.findFunction("app::Log::Sink::emit")->isExternal);
}

TEST(ExternalType, TemplateBoundaryModifierSurvivesParser) {
    const std::string source = "namespace app { public: external template<typename T> type Log { public: void write(T value); } }";
    DiagnosticEngine diag;
    auto ast = parseExternal(source, diag);
    ASSERT_FALSE(diag.hasErrors());
    format::Formatter formatter;
    auto formatted = formatter.format(*ast, source, {});
    EXPECT_NE(formatted.find("external template<typename T> type Log"), std::string::npos);
    const auto symbols = analyzeExternal(formatted);
    ASSERT_NE(symbols.findClassSymbol("app::Log"), nullptr);
    EXPECT_TRUE(symbols.findClassSymbol("app::Log")->isExternal);
    EXPECT_TRUE(symbols.findFunction("app::Log::write")->isExternal);
}

TEST(ExternalType, SerializedAndImportedBoundaryRetainsOwnership) {
    auto symbols = analyzeExternal();
    auto payload = serializeSymbolTable(symbols);
    SymbolTable restored;
    ASSERT_TRUE(deserializeSymbolTable(payload, restored));
    EXPECT_TRUE(restored.findClassSymbol("app::Log")->explicitlyExternal);
    EXPECT_TRUE(restored.findFunction("app::Log::Log/1")->isExternal);
    EXPECT_EQ(restored.findFunction("app::Log::Log/1")->kind, FunctionSymbol::Kind::Constructor);
    EXPECT_EQ(restored.findFunction("app::Log::Sink::emit")->externalOwner, "app::Log");
    EXPECT_EQ(restored.findClassSymbol("app::Log::Sink")->enclosingType, "app::Log");
    ASSERT_EQ(restored.findClassSymbol("app::Log")->operatorOverloads.size(), 1u);
    EXPECT_TRUE(restored.findClassSymbol("app::Log")->operatorOverloads[0].isExternal);
    SymbolTable selected;
    EXPECT_TRUE(selected.mergeSelected(restored, {"Log"}).empty());
    ASSERT_NE(selected.findFunction("app::Log::Sink::emit"), nullptr);
    EXPECT_TRUE(selected.findFunction("app::Log::Sink::emit")->isExternal);
    EXPECT_EQ(selected.findClassSymbol("app::Child"), nullptr);
}

TEST(ExternalType, LegacyPayloadDoesNotInventTypeAuthorization) {
    const auto symbols = analyzeExternal("namespace app { public: external void io(); type Plain { public: void work(); } }");
    auto payload = serializeSymbolTable(symbols);
    payload["functions"]["app::io"].erase("explicitlyExternal");
    SymbolTable restored;
    ASSERT_TRUE(deserializeSymbolTable(payload, restored));
    EXPECT_TRUE(restored.findFunction("app::io")->isExternal);
    EXPECT_TRUE(restored.findFunction("app::io")->explicitlyExternal);
    EXPECT_FALSE(restored.findClassSymbol("app::Plain")->isExternal);
}

TEST(ExternalType, ContainmentKeepsClassOutsideBehaviorVisible) {
    const auto symbols = analyzeExternal();
    ContainmentConfig config;
    config.mode = FeatureMode::Force;
    CheckResult result;
    checkContainment(symbols, {}, {
        site("app::Log::write"), site("app::Log::private_impl"), site("app::Log::~Log"),
        site("app::Log::<static-initializer>"), site("app::Log::Sink::emit"),
        site("app::Other::write"), site("app::Child::write"), site("app::write")}, config, result);
    EXPECT_EQ(result.errorCount, 3);
    CheckResult callee;
    checkContainment(symbols, {}, {site("app::write", "app::Log::write"),
                                   site("app::write", "write")}, config, callee);
    EXPECT_EQ(callee.errorCount, 1);
}

TEST(ExternalType, OpaquePrivateMembersDoNotHideUndeclaredPublicInterface) {
    const auto symbols = analyzeExternal("namespace app { public: external type Log { public: void write(); } void collision(); }");
    auto host = [](const std::string& name, Visibility visibility) {
        HostSymbol hs;
        hs.kind = HostSymbolKind::Method;
        hs.qualifiedName = "app::Log::" + name;
        hs.simpleName = name;
        hs.enclosingClass = "app::Log";
        hs.hostVisibility = visibility;
        return hs;
    };
    CheckResult result;
    checkCompleteness({host("private_impl", Visibility::Private),
                       host("new_public", Visibility::Public),
                       host("new_protected", Visibility::Protected),
                       host("collision", Visibility::Public)}, symbols, {}, {}, result);
    EXPECT_EQ(result.errorCount, 3);
}

TEST(ExternalType, ParallelBoundaryIsNotAssumedPureAfterSkippedExtraction) {
    auto symbols = analyzeExternal();
    LogicBlockEntry block;
    block.qualifiedName = "app::run";
    block.calledFunctions = {"app::Log::write", "app::write"};
    block.stages = {1, 1};
    symbols.addLogicBlock(block);
    CheckResult result;
    checkPurity(symbols, {}, result);
    EXPECT_EQ(result.errorCount, 1);
}

TEST(ExternalType, TranspileJsonAndOptimizerPreserveOpaqueBoundary) {
    using namespace topo::transpile;
    TranspileModule module;
    TranspileType type;
    type.qualifiedName = "app::Log";
    type.isExternal = true;
    type.externalOwner = "app::Log";
    module.types.push_back(type);
    TranspileFunction function;
    function.qualifiedName = "app::Log::write";
    function.isExternal = true;
    function.owningType = "app::Log";
    function.externalOwner = "app::Log";
    function.returnType.nameParts = {"void"};
    module.functions.push_back(std::move(function));
    auto payload = serializeModule(module);
    auto restored = deserializeModule(payload);
    ASSERT_EQ(restored.functions.size(), 1u);
    EXPECT_TRUE(restored.functions[0].isExternal);
    EXPECT_EQ(restored.functions[0].owningType, "app::Log");
    EXPECT_TRUE(restored.types[0].isExternal);
    struct CountingPass : ModelPass {
        explicit CountingPass(int& count) : count(count) {}
        void run(TranspileFunction&) override { ++count; }
        int& count;
    };
    int count = 0;
    ModelOptimizer optimizer;
    optimizer.addPass(std::make_unique<CountingPass>(count));
    optimizer.optimize(restored);
    EXPECT_EQ(count, 0);
}

TEST(ExternalType, TopoSourceBuilderExplicitlyRejectsUnavailableTypeLowering) {
    DiagnosticEngine diag;
    auto ast = parseExternal(externalSource, diag);
    ASSERT_FALSE(diag.hasErrors());
    transpile::TopoSourceBuilder builder;
    auto result = builder.build(*ast);
    EXPECT_FALSE(result.success);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors.front().find("external type"), std::string::npos);
}

TEST(ExternalType, ExactPolicyBoundaryPropagatesWithoutChangingDeclarationOrigin) {
    auto symbols = analyzeExternal("namespace app { public: type Log { public: void write(); type Sink { public: void emit(); } } type Other { public: void write(); } }");
    EXPECT_FALSE(symbols.markExternalType("Log"));
    ASSERT_TRUE(symbols.markExternalType("app::Log"));
    EXPECT_TRUE(symbols.findFunction("app::Log::Sink::emit")->isExternal);
    EXPECT_FALSE(symbols.findClassSymbol("app::Log")->explicitlyExternal);
    EXPECT_FALSE(symbols.findFunction("app::Other::write")->isExternal);
    auto conflicting = analyzeExternal("namespace app { public: type Log { public: comptime void write(); } }");
    EXPECT_FALSE(conflicting.markExternalType("app::Log"));
    EXPECT_FALSE(conflicting.findClassSymbol("app::Log")->isExternal);
}

TEST(ExternalType, StrictContainmentRequiresExactStandaloneFunctionIdentity) {
    auto symbols = analyzeExternal("namespace app { public: external void io(); }");
    ContainmentConfig config;
    config.mode = FeatureMode::Force;
    config.strictIdentity = true;
    CheckResult result;
    checkContainment(symbols, {}, {site("app::io"), site("other::io"), site("caller", "io"), site("caller", "app::io")}, config, result);
    EXPECT_EQ(result.errorCount, 2);
}

TEST(ExternalType, ManualFileRetainsReverseLookupAndUsesExactPath) {
    auto symbols = analyzeExternal("namespace app { public: void write(); }");
    HostSymbol declared;
    declared.qualifiedName = "app::write";
    declared.simpleName = "write";
    declared.file = "/project/log.cpp";
    HostSymbol hidden = declared;
    hidden.qualifiedName = "app::helper";
    hidden.simpleName = "helper";
    HostSymbol outside = hidden;
    outside.file = "/other/log.cpp";
    CompletenessConfig config;
    config.manualFiles = {declared.file};
    CheckResult result;
    checkCompleteness({declared, hidden, outside}, symbols, {}, config, result);
    EXPECT_EQ(result.errorCount, 1);
    EXPECT_EQ(result.warningCount, 0);
}
