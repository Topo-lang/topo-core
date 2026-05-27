// Unit tests for checkCompleteness free function.

#include "topo/Check/CompletenessCheck.h"
#include "topo/Sema/SymbolTable.h"
#include "topo/Sema/VisibilityCollector.h"

#include <gtest/gtest.h>

using namespace topo;
using namespace topo::check;

// Helper: create a HostSymbol for a function
static HostSymbol makeHostFunc(const std::string& qualified,
                               const std::string& simple,
                               const std::string& file = "test.cpp",
                               int line = 1) {
    HostSymbol hs;
    hs.qualifiedName = qualified;
    hs.simpleName = simple;
    hs.kind = HostSymbolKind::Function;
    hs.file = file;
    hs.line = line;
    return hs;
}

// Helper: create a HostSymbol for a class
static HostSymbol makeHostClass(const std::string& qualified,
                                const std::string& simple,
                                const std::string& file = "test.cpp",
                                int line = 1) {
    HostSymbol hs;
    hs.qualifiedName = qualified;
    hs.simpleName = simple;
    hs.kind = HostSymbolKind::Class;
    hs.file = file;
    hs.line = line;
    return hs;
}

// Test 1: All matched — everything declared in both host and .topo
TEST(CompletenessCheck, AllMatched) {
    std::vector<HostSymbol> hostSymbols = {
        makeHostFunc("ns::init", "init"),
        makeHostFunc("ns::process", "process"),
    };

    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::init";
    f1.simpleName = "init";
    f1.visibility = Visibility::Public;
    symbols.addFunction(f1);

    FunctionSymbol f2;
    f2.qualifiedName = "ns::process";
    f2.simpleName = "process";
    f2.visibility = Visibility::Public;
    symbols.addFunction(f2);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve1;
    ve1.qualifiedName = "ns::init";
    ve1.visibility = Visibility::Public;
    visEntries.push_back(ve1);
    VisibilityEntry ve2;
    ve2.qualifiedName = "ns::process";
    ve2.visibility = Visibility::Public;
    visEntries.push_back(ve2);

    CompletenessConfig config;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
    EXPECT_EQ(result.warningCount, 0);
}

// Test 2: Undeclared symbol — host has it, .topo doesn't
TEST(CompletenessCheck, UndeclaredSymbol) {
    std::vector<HostSymbol> hostSymbols = {
        makeHostFunc("ns::init", "init"),
        makeHostFunc("ns::secret", "secret", "test.cpp", 42),
    };

    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::init";
    f1.simpleName = "init";
    f1.visibility = Visibility::Public;
    symbols.addFunction(f1);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve1;
    ve1.qualifiedName = "ns::init";
    ve1.visibility = Visibility::Public;
    visEntries.push_back(ve1);

    CompletenessConfig config;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 1);
    EXPECT_EQ(result.diagnostics[0].check, "completeness");
    EXPECT_NE(result.diagnostics[0].message.find("ns::secret"), std::string::npos);
}

// Test 3: Dangling declaration — .topo has it, host doesn't
TEST(CompletenessCheck, DanglingDeclaration) {
    std::vector<HostSymbol> hostSymbols = {
        makeHostFunc("ns::init", "init"),
    };

    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::init";
    f1.simpleName = "init";
    f1.visibility = Visibility::Public;
    symbols.addFunction(f1);

    FunctionSymbol f2;
    f2.qualifiedName = "ns::phantom";
    f2.simpleName = "phantom";
    f2.visibility = Visibility::Public;
    f2.location.file = "main.topo";
    f2.location.line = 10;
    symbols.addFunction(f2);

    std::vector<VisibilityEntry> visEntries;

    CompletenessConfig config;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_TRUE(result.passed()); // warnings don't fail
    EXPECT_EQ(result.warningCount, 1);
    EXPECT_NE(result.diagnostics[0].message.find("ns::phantom"), std::string::npos);
}

// Test 4: Visibility mismatch — .topo public but host private
TEST(CompletenessCheck, VisibilityMismatchPublicVsPrivate) {
    HostSymbol hs = makeHostFunc("ns::func", "func");
    hs.hostVisibility = Visibility::Private;
    std::vector<HostSymbol> hostSymbols = {hs};

    SymbolTable symbols;
    FunctionSymbol f;
    f.qualifiedName = "ns::func";
    f.simpleName = "func";
    f.visibility = Visibility::Public;
    symbols.addFunction(f);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "ns::func";
    ve.visibility = Visibility::Public;
    visEntries.push_back(ve);

    CompletenessConfig config;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 1);
    EXPECT_NE(result.diagnostics[0].message.find("public"), std::string::npos);
    EXPECT_NE(result.diagnostics[0].message.find("private"), std::string::npos);
}

// Test 5: Visibility mismatch — .topo private but host public -> warning
TEST(CompletenessCheck, VisibilityMismatchPrivateVsPublic) {
    HostSymbol hs = makeHostFunc("ns::func", "func");
    hs.hostVisibility = Visibility::Public;
    std::vector<HostSymbol> hostSymbols = {hs};

    SymbolTable symbols;
    FunctionSymbol f;
    f.qualifiedName = "ns::func";
    f.simpleName = "func";
    f.visibility = Visibility::Private;
    symbols.addFunction(f);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "ns::func";
    ve.visibility = Visibility::Private;
    visEntries.push_back(ve);

    CompletenessConfig config;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_TRUE(result.passed()); // warnings don't fail
    EXPECT_EQ(result.warningCount, 1);
}

// Test 6: ignorePatterns filters out matched symbols
TEST(CompletenessCheck, IgnorePatternsWork) {
    std::vector<HostSymbol> hostSymbols = {
        makeHostFunc("test::helper", "helper"),
        makeHostFunc("ns::real", "real"),
    };

    SymbolTable symbols;
    FunctionSymbol f;
    f.qualifiedName = "ns::real";
    f.simpleName = "real";
    f.visibility = Visibility::Public;
    symbols.addFunction(f);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "ns::real";
    ve.visibility = Visibility::Public;
    visEntries.push_back(ve);

    CompletenessConfig config;
    config.ignorePatterns = {"test::*"};
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
}

// Test 7: ignoreMain filters out main function
TEST(CompletenessCheck, IgnoreMainWorks) {
    std::vector<HostSymbol> hostSymbols = {
        makeHostFunc("main", "main"),
        makeHostFunc("ns::init", "init"),
    };

    SymbolTable symbols;
    FunctionSymbol f;
    f.qualifiedName = "ns::init";
    f.simpleName = "init";
    f.visibility = Visibility::Public;
    symbols.addFunction(f);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "ns::init";
    ve.visibility = Visibility::Public;
    visEntries.push_back(ve);

    CompletenessConfig config;
    config.ignoreMain = true;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
}

// Test 8: ignoreMain = false reports main as undeclared
TEST(CompletenessCheck, IgnoreMainFalseReportsMain) {
    std::vector<HostSymbol> hostSymbols = {
        makeHostFunc("main", "main"),
    };

    SymbolTable symbols;
    std::vector<VisibilityEntry> visEntries;

    CompletenessConfig config;
    config.ignoreMain = false;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 1);
}

// Test 9: Binding target symbols are skipped in Pass 2
TEST(CompletenessCheck, BindingTargetSkipped) {
    std::vector<HostSymbol> hostSymbols; // empty host

    SymbolTable symbols;
    FunctionSymbol f;
    f.qualifiedName = "ns::sort";
    f.simpleName = "sort";
    f.visibility = Visibility::Public;
    f.bindingTarget = "std::sort";
    symbols.addFunction(f);

    std::vector<VisibilityEntry> visEntries;

    CompletenessConfig config;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.warningCount, 0); // binding target not flagged
}

// Test 10: Visibility::Ignore symbols are skipped everywhere
TEST(CompletenessCheck, IgnoreVisibilitySkipped) {
    // Host has a symbol that's in .topo as ignore: — should not report
    std::vector<HostSymbol> hostSymbols = {
        makeHostFunc("ns::thirdPartyCallback", "thirdPartyCallback"),
    };

    SymbolTable symbols;
    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "ns::thirdPartyCallback";
    ve.visibility = Visibility::Ignore;
    visEntries.push_back(ve);

    CompletenessConfig config;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
    EXPECT_EQ(result.warningCount, 0);
}

// Test 11: Ignore visibility — .topo has ignore symbol not in host (no warning)
TEST(CompletenessCheck, IgnoreVisibilityNoDanglingWarning) {
    std::vector<HostSymbol> hostSymbols;

    SymbolTable symbols;
    // This function exists in symbol table but has Ignore visibility
    FunctionSymbol f;
    f.qualifiedName = "ns::generated";
    f.simpleName = "generated";
    f.visibility = Visibility::Ignore;
    symbols.addFunction(f);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "ns::generated";
    ve.visibility = Visibility::Ignore;
    visEntries.push_back(ve);

    CompletenessConfig config;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.warningCount, 0);
}

// Test 12: Class symbols matched
TEST(CompletenessCheck, ClassSymbolMatched) {
    std::vector<HostSymbol> hostSymbols = {
        makeHostClass("ns::Vector", "Vector"),
    };

    SymbolTable symbols;
    ClassSymbol cls;
    cls.qualifiedName = "ns::Vector";
    cls.simpleName = "Vector";
    cls.visibility = Visibility::Public;
    symbols.addClassSymbol(cls);

    std::vector<VisibilityEntry> visEntries;

    CompletenessConfig config;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
}

// Test 13: ignoreConstructors filters constructors
TEST(CompletenessCheck, IgnoreConstructors) {
    HostSymbol hs;
    hs.qualifiedName = "ns::Foo::Foo";
    hs.simpleName = "Foo";
    hs.kind = HostSymbolKind::Constructor;
    hs.file = "test.cpp";
    hs.line = 1;
    std::vector<HostSymbol> hostSymbols = {hs};

    SymbolTable symbols;
    std::vector<VisibilityEntry> visEntries;

    CompletenessConfig config;
    config.ignoreConstructors = true;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
}

// Test 14: ignoreDestructors filters destructors
TEST(CompletenessCheck, IgnoreDestructors) {
    HostSymbol hs;
    hs.qualifiedName = "ns::Foo::~Foo";
    hs.simpleName = "~Foo";
    hs.kind = HostSymbolKind::Destructor;
    hs.file = "test.cpp";
    hs.line = 1;
    std::vector<HostSymbol> hostSymbols = {hs};

    SymbolTable symbols;
    std::vector<VisibilityEntry> visEntries;

    CompletenessConfig config;
    config.ignoreDestructors = true;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
}

// Test 15: Empty inputs — no symbols anywhere
TEST(CompletenessCheck, EmptyInputs) {
    std::vector<HostSymbol> hostSymbols;
    SymbolTable symbols;
    std::vector<VisibilityEntry> visEntries;

    CompletenessConfig config;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
    EXPECT_EQ(result.warningCount, 0);
}

// =============================================================================
// Adversarial / negative fixtures (issue: checker-completeness-no-negative-fixtures).
// The existing AllMatched / BindingTargetSkipped / IgnoreVisibilitySkipped fixtures
// establish the happy-path contract. The three tests below pin the *error* side
// of that contract — if a regression turns checkCompleteness into a silent no-op,
// these must break so the regression is caught.
// =============================================================================

// Adversarial: Dangling declaration for a *class* symbol, not a function.
// Pass 2 has a separate branch for classSymbols() — cover it with a fixture
// that exercises the classSymbols path specifically. Regression: if the class
// branch is accidentally skipped or its hostSimpleNameSet lookup short-circuits,
// this warning disappears.
TEST(CompletenessCheck, DanglingDeclaration_Reports) {
    std::vector<HostSymbol> hostSymbols; // host code defines nothing

    SymbolTable symbols;
    // .topo declares a class that host code does not define.
    ClassSymbol cls;
    cls.qualifiedName = "engine::Renderer";
    cls.simpleName = "Renderer";
    cls.visibility = Visibility::Public;
    cls.location.file = "engine.topo";
    cls.location.line = 7;
    symbols.addClassSymbol(cls);

    // Also declare a dangling function so we exercise both branches in one run.
    FunctionSymbol f;
    f.qualifiedName = "engine::ghostFn";
    f.simpleName = "ghostFn";
    f.visibility = Visibility::Public;
    f.location.file = "engine.topo";
    f.location.line = 12;
    symbols.addFunction(f);

    std::vector<VisibilityEntry> visEntries; // intentionally empty

    CompletenessConfig config;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    // Dangling declarations produce warnings, not errors; the run still "passes"
    // by CheckResult semantics but the warning count must be non-zero.
    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
    EXPECT_EQ(result.warningCount, 2);

    // Both the class and the function must surface with explicit names and
    // the "completeness" check tag.
    bool sawClassDangling = false;
    bool sawFnDangling = false;
    for (const auto& diag : result.diagnostics) {
        EXPECT_EQ(diag.check, "completeness");
        EXPECT_EQ(diag.severity, Severity::Warning);
        if (diag.message.find("engine::Renderer") != std::string::npos) {
            sawClassDangling = true;
            EXPECT_NE(diag.message.find("class"), std::string::npos);
        }
        if (diag.message.find("engine::ghostFn") != std::string::npos) {
            sawFnDangling = true;
            EXPECT_NE(diag.message.find("function"), std::string::npos);
        }
    }
    EXPECT_TRUE(sawClassDangling);
    EXPECT_TRUE(sawFnDangling);
}

// Adversarial: host code defines a *public* class plus a public free function
// that .topo does not list at all. Pass 1 must emit one Error per undeclared
// symbol. Regression: a faulty short-circuit (e.g. returning "declared" when
// the symbol table is merely non-empty) would silence these.
TEST(CompletenessCheck, UndeclaredPublicSymbol_Reports) {
    HostSymbol klass = makeHostClass("app::PublicWidget", "PublicWidget",
                                     "widget.cpp", 5);
    klass.hostVisibility = Visibility::Public;

    HostSymbol freeFn = makeHostFunc("app::exportedApi", "exportedApi",
                                     "api.cpp", 42);
    freeFn.hostVisibility = Visibility::Public;

    std::vector<HostSymbol> hostSymbols = {klass, freeFn};

    SymbolTable symbols; // .topo declares nothing
    std::vector<VisibilityEntry> visEntries;

    CompletenessConfig config;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 2);
    EXPECT_EQ(result.warningCount, 0);

    bool sawClass = false;
    bool sawFn = false;
    for (const auto& diag : result.diagnostics) {
        EXPECT_EQ(diag.check, "completeness");
        EXPECT_EQ(diag.severity, Severity::Error);
        // message must call out the missing declaration explicitly.
        EXPECT_NE(diag.message.find("not declared in .topo"), std::string::npos);
        if (diag.message.find("app::PublicWidget") != std::string::npos) {
            sawClass = true;
            EXPECT_EQ(diag.file, "widget.cpp");
            EXPECT_EQ(diag.line, 5);
        }
        if (diag.message.find("app::exportedApi") != std::string::npos) {
            sawFn = true;
            EXPECT_EQ(diag.file, "api.cpp");
            EXPECT_EQ(diag.line, 42);
        }
    }
    EXPECT_TRUE(sawClass);
    EXPECT_TRUE(sawFn);
}

// Adversarial: .topo declares a symbol `public` but host code marks it
// `protected`. This is the second arm of Pass 3 (Public vs Protected) and is
// not exercised by VisibilityMismatchPublicVsPrivate (which uses Private).
// Also covers a simulated anonymous-namespace / static case: host code marks
// a separate symbol Private while .topo declares it Public. Two independent
// violations in one run pin both the Private and Protected arms.
TEST(CompletenessCheck, VisibilityMismatch_Reports) {
    // Arm A: .topo public, host protected.
    HostSymbol hsProtected = makeHostFunc("ns::apiA", "apiA", "a.cpp", 11);
    hsProtected.hostVisibility = Visibility::Protected;

    // Arm B: .topo public, host private (simulates static / anonymous-ns).
    HostSymbol hsPrivate = makeHostFunc("ns::apiB", "apiB", "b.cpp", 22);
    hsPrivate.hostVisibility = Visibility::Private;

    std::vector<HostSymbol> hostSymbols = {hsProtected, hsPrivate};

    SymbolTable symbols;
    for (const char* name : {"apiA", "apiB"}) {
        FunctionSymbol f;
        f.qualifiedName = std::string("ns::") + name;
        f.simpleName = name;
        f.visibility = Visibility::Public;
        symbols.addFunction(f);
    }

    std::vector<VisibilityEntry> visEntries;
    for (const char* name : {"apiA", "apiB"}) {
        VisibilityEntry ve;
        ve.qualifiedName = std::string("ns::") + name;
        ve.visibility = Visibility::Public;
        visEntries.push_back(ve);
    }

    CompletenessConfig config;
    CheckResult result;
    checkCompleteness(hostSymbols, symbols, visEntries, config, result);

    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 2);
    EXPECT_EQ(result.warningCount, 0);

    bool sawProtected = false;
    bool sawPrivate = false;
    for (const auto& diag : result.diagnostics) {
        EXPECT_EQ(diag.check, "completeness");
        EXPECT_EQ(diag.severity, Severity::Error);
        EXPECT_NE(diag.message.find("public"), std::string::npos);
        if (diag.message.find("ns::apiA") != std::string::npos) {
            sawProtected = true;
            EXPECT_NE(diag.message.find("protected"), std::string::npos);
        }
        if (diag.message.find("ns::apiB") != std::string::npos) {
            sawPrivate = true;
            EXPECT_NE(diag.message.find("private"), std::string::npos);
        }
    }
    EXPECT_TRUE(sawProtected);
    EXPECT_TRUE(sawPrivate);
}
