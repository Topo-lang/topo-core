// Unit tests for checkContainment free function.

#include "topo/Check/ContainmentCheck.h"
#include "topo/Check/CapabilityCatalog.h"
#include "topo/Check/ContainmentTypes.h"
#include "topo/Sema/SymbolTable.h"

#include <gtest/gtest.h>
#include <set>

using namespace topo;
using namespace topo::check;

namespace {

// Helper: create a FunctionSymbol with location file set
FunctionSymbol makeFn(const std::string& qualified, const std::string& simple,
                      Visibility vis, bool external, const std::string& file) {
    FunctionSymbol fn;
    fn.qualifiedName = qualified;
    fn.simpleName = simple;
    fn.visibility = vis;
    fn.isExternal = external;
    fn.location.file = file;
    return fn;
}

// Helper: create a DetectedCallSite with System-level unsafe (matches real extractor output)
DetectedCallSite makeSite(const std::string& caller, const std::string& callee,
                          CapabilityKind cap, const std::string& file, int line,
                          UnsafeLevel level = UnsafeLevel::System) {
    DetectedCallSite s;
    s.callerQualifiedName = caller;
    s.calleePattern = callee;
    s.capability = cap;
    s.unsafeLevel = level;
    s.file = file;
    s.line = line;
    return s;
}

} // anonymous namespace

// Test 1: No external functions, no imports — clean pass
TEST(ContainmentCheck, NoExternalNoImports) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("ns::foo", "foo", Visibility::Public, false, "foo.cpp"));

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
    EXPECT_EQ(result.warningCount, 0);
}

// Test 2: File has external fn + restricted import — passes
TEST(ContainmentCheck, ExternalFn_WithImport) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("io::readFile", "readFile", Visibility::Public, true, "io.cpp"));

    std::vector<HostImport> imports;
    imports.push_back({"fstream", "io.cpp", 3});

    std::vector<DetectedCallSite> callSites;
    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
}

// Test 3: File has network import, no external fn, mode=Force — Error
TEST(ContainmentCheck, NonExtFile_WithNetworkImport_Force) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("net::connect", "connect_impl", Visibility::Public, false, "net.cpp"));

    std::vector<HostImport> imports;
    imports.push_back({"sys/socket.h", "net.cpp", 5});

    std::vector<DetectedCallSite> callSites;
    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_FALSE(result.passed());
    EXPECT_GE(result.errorCount, 1);
    EXPECT_EQ(result.diagnostics[0].check, "containment");
    EXPECT_EQ(result.diagnostics[0].severity, Severity::Error);
}

// Test 4: Same as 3 but mode=Auto — Warning (passes since warnings don't fail)
TEST(ContainmentCheck, NonExtFile_WithNetworkImport_Auto) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("net::connect", "connect_impl", Visibility::Public, false, "net.cpp"));

    std::vector<HostImport> imports;
    imports.push_back({"sys/socket.h", "net.cpp", 5});

    std::vector<DetectedCallSite> callSites;
    ContainmentConfig config;
    config.mode = FeatureMode::Auto;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed());  // warnings don't cause failure
    EXPECT_GE(result.warningCount, 1);
    EXPECT_EQ(result.diagnostics[0].severity, Severity::Warning);
}

// Test 5: Non-external fn calls system() — violation
TEST(ContainmentCheck, NonExtFn_CallsSystem) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("util::run", "run", Visibility::Public, false, "util.cpp"));

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    callSites.push_back(makeSite("util::run", "system", CapabilityKind::Process, "util.cpp", 15));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_FALSE(result.passed());
    EXPECT_GE(result.errorCount, 1);
    EXPECT_NE(result.diagnostics[0].message.find("system"), std::string::npos);
}

// Test 6: Non-external fn A calls external fn B — passes (boundary delegation)
TEST(ContainmentCheck, NonExtFn_CallsExternalFn) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("app::process", "process", Visibility::Public, false, "app.cpp"));
    symbols.addFunction(makeFn("io::writeFile", "writeFile", Visibility::Public, true, "io.cpp"));

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    // process() calls writeFile — callee is external, so it's boundary delegation
    callSites.push_back(makeSite("app::process", "writeFile", CapabilityKind::File, "app.cpp", 20));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
}

// Test 7: External fn calls fopen — passes (external functions are not checked)
TEST(ContainmentCheck, ExternalFn_CallsAnything) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("io::readFile", "readFile", Visibility::Public, true, "io.cpp"));

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    callSites.push_back(makeSite("io::readFile", "fopen", CapabilityKind::File, "io.cpp", 10));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
}

// Test 8: Safe stdlib import only — passes
TEST(ContainmentCheck, SafeStdlib) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("util::sort", "sort", Visibility::Public, false, "util.cpp"));

    std::vector<HostImport> imports;
    imports.push_back({"vector", "util.cpp", 1});
    imports.push_back({"algorithm", "util.cpp", 2});
    imports.push_back({"string", "util.cpp", 3});

    std::vector<DetectedCallSite> callSites;
    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.diagnostics.size(), 0u);
}

// Test 9: Mode Off — has violations but no diagnostics
TEST(ContainmentCheck, ModeOff) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("net::send", "send_impl", Visibility::Public, false, "net.cpp"));

    std::vector<HostImport> imports;
    imports.push_back({"sys/socket.h", "net.cpp", 2});

    std::vector<DetectedCallSite> callSites;
    callSites.push_back(makeSite("net::send", "socket", CapabilityKind::Network, "net.cpp", 10));

    ContainmentConfig config;
    config.mode = FeatureMode::Off;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.diagnostics.size(), 0u);
}

// Test 10: Multiple import + call site violations — multiple diagnostics
TEST(ContainmentCheck, MultipleViolations) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("app::main", "main_fn", Visibility::Public, false, "app.cpp"));
    symbols.addFunction(makeFn("app::helper", "helper", Visibility::Public, false, "app.cpp"));

    // Import violations: file with restricted includes but no external fn
    std::vector<HostImport> imports;
    imports.push_back({"fstream", "app.cpp", 1});
    imports.push_back({"sys/socket.h", "app.cpp", 2});

    // Call site violations: non-external callers calling external APIs
    std::vector<DetectedCallSite> callSites;
    callSites.push_back(makeSite("app::main", "fopen", CapabilityKind::File, "app.cpp", 10));
    callSites.push_back(makeSite("app::helper", "socket", CapabilityKind::Network, "app.cpp", 20));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_FALSE(result.passed());
    // 2 import violations + 2 call site violations = 4 total
    EXPECT_GE(result.errorCount, 4);
    EXPECT_GE(result.diagnostics.size(), 4u);

    // All diagnostics should be "containment" check
    for (const auto& diag : result.diagnostics) {
        EXPECT_EQ(diag.check, "containment");
    }
}

// =====================================================
// Non-transitivity tests (CRITICAL design constraint)
// =====================================================

// Test 11: A(non-ext) calls B(ext), A also calls socket() directly
// A→B is allowed (delegation), but A→socket() is still a violation
// because external is NOT transitive
TEST(ContainmentCheck, NonTransitive_CallerStillChecked) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("app::dispatch", "dispatch", Visibility::Public, false, "app.cpp"));
    symbols.addFunction(makeFn("net::sendPacket", "sendPacket", Visibility::Public, true, "net.cpp"));

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    // dispatch() calls sendPacket (external) — allowed
    callSites.push_back(makeSite("app::dispatch", "sendPacket", CapabilityKind::Network, "app.cpp", 10));
    // dispatch() ALSO calls socket() directly — violation!
    callSites.push_back(makeSite("app::dispatch", "socket", CapabilityKind::Network, "app.cpp", 15));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 1); // only the socket() call is violation
    EXPECT_NE(result.diagnostics[0].message.find("socket"), std::string::npos);
    EXPECT_NE(result.diagnostics[0].message.find("dispatch"), std::string::npos);
}

// Test 12: Chain A(non-ext) → B(ext), but only via simpleName match
// Verifies that the external function set includes both qualified and simple names
TEST(ContainmentCheck, NonTransitive_SimpleNameMatch) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("app::handler", "handler", Visibility::Public, false, "app.cpp"));
    symbols.addFunction(makeFn("io::deep::writeFile", "writeFile", Visibility::Public, true, "io.cpp"));

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    // handler() calls "writeFile" by simple name — should match external fn
    callSites.push_back(makeSite("app::handler", "writeFile", CapabilityKind::File, "app.cpp", 20));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
}

// =====================================================
// Cross-file isolation
// =====================================================

// Test 13: File A has external fn + restricted import (OK), File B has restricted import but no external fn (FAIL)
// Proves import check is per-file, not global
TEST(ContainmentCheck, CrossFile_ExternalInOneFile_ViolationInAnother) {
    SymbolTable symbols;
    // File A: has an external fn
    symbols.addFunction(makeFn("io::read", "read", Visibility::Public, true, "io.cpp"));
    // File B: only non-external fns
    symbols.addFunction(makeFn("render::draw", "draw", Visibility::Public, false, "render.cpp"));

    std::vector<HostImport> imports;
    imports.push_back({"fstream", "io.cpp", 1});          // File A: OK (has external fn)
    imports.push_back({"sys/socket.h", "render.cpp", 2}); // File B: VIOLATION (no external fn)

    std::vector<DetectedCallSite> callSites;
    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 1); // only render.cpp violation
    EXPECT_NE(result.diagnostics[0].message.find("render.cpp"), std::string::npos);
}

// Test 14: Same file has both safe and restricted imports, but has external fn
// Only restricted imports matter; safe ones are ignored
TEST(ContainmentCheck, MixedSafeAndRestrictedImports_WithExternal) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("io::loadData", "loadData", Visibility::Public, true, "data.cpp"));

    std::vector<HostImport> imports;
    imports.push_back({"vector", "data.cpp", 1});
    imports.push_back({"string", "data.cpp", 2});
    imports.push_back({"fstream", "data.cpp", 3});    // restricted but file has external fn
    imports.push_back({"algorithm", "data.cpp", 4});

    std::vector<DetectedCallSite> callSites;
    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed());
}

// Test 15: Same file has both safe and restricted imports, NO external fn
// Only restricted imports generate violations, safe ones don't
TEST(ContainmentCheck, MixedSafeAndRestrictedImports_NoExternal) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("util::process", "process", Visibility::Public, false, "util.cpp"));

    std::vector<HostImport> imports;
    imports.push_back({"vector", "util.cpp", 1});      // safe
    imports.push_back({"algorithm", "util.cpp", 2});   // safe
    imports.push_back({"fstream", "util.cpp", 3});     // restricted → violation
    imports.push_back({"cstdio", "util.cpp", 4});      // restricted → violation

    std::vector<DetectedCallSite> callSites;
    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 2); // fstream + cstdio
}

// =====================================================
// Callee name collision (external fn name = API name)
// =====================================================

// Test 16: External fn named "send" — same as network API
// Non-external fn calling "send" should match the external fn (pass)
TEST(ContainmentCheck, NameCollision_ExternalFnSameAsApiName) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("app::handler", "handler", Visibility::Public, false, "app.cpp"));
    symbols.addFunction(makeFn("net::send", "send", Visibility::Public, true, "net.cpp"));

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    // handler() calls "send" — matches external fn name, should be boundary delegation
    callSites.push_back(makeSite("app::handler", "send", CapabilityKind::Network, "app.cpp", 25));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
}

// Test 17: External fn matched by simpleName when callee is bare name
// CppCallSiteExtractor always produces bare names — match via simpleName
TEST(ContainmentCheck, ExternalFnMatch_ViaSimpleName) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("app::logic", "logic", Visibility::Public, false, "app.cpp"));
    symbols.addFunction(makeFn("io::deep::readConfig", "readConfig", Visibility::Public, true, "io.cpp"));

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    // Callee is bare name (as produced by extractor regex)
    callSites.push_back(makeSite("app::logic", "readConfig", CapabilityKind::File, "app.cpp", 30));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed());
}

// =====================================================
// All four CapabilityKind types in one check
// =====================================================

// Test 18: Four different import types across four files, none with external fn
TEST(ContainmentCheck, AllFourCapabilities_AllViolate) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("a::fn", "fn_a", Visibility::Public, false, "a.cpp"));
    symbols.addFunction(makeFn("b::fn", "fn_b", Visibility::Public, false, "b.cpp"));
    symbols.addFunction(makeFn("c::fn", "fn_c", Visibility::Public, false, "c.cpp"));
    symbols.addFunction(makeFn("d::fn", "fn_d", Visibility::Public, false, "d.cpp"));

    std::vector<HostImport> imports;
    imports.push_back({"fstream", "a.cpp", 1});         // File
    imports.push_back({"sys/socket.h", "b.cpp", 1});    // Network
    imports.push_back({"cstdlib", "c.cpp", 1});          // Process
    imports.push_back({"dlfcn.h", "d.cpp", 1});          // DynamicLoad

    std::vector<DetectedCallSite> callSites;
    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 4);

    // Verify all four capabilities are represented
    std::set<std::string> capabilities;
    for (const auto& d : result.diagnostics) {
        // Extract capability from message: "... (file)", "... (network)", etc.
        auto openParen = d.message.rfind('(');
        auto closeParen = d.message.rfind(')');
        if (openParen != std::string::npos && closeParen != std::string::npos) {
            capabilities.insert(d.message.substr(openParen + 1, closeParen - openParen - 1));
        }
    }
    EXPECT_EQ(capabilities.size(), 4u);
}

// =====================================================
// Visibility and access level edge cases
// =====================================================

// Test 19: Private external function still counts
TEST(ContainmentCheck, PrivateExternalFn_StillCounts) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("impl::loadSecret", "loadSecret", Visibility::Private, true, "impl.cpp"));

    std::vector<HostImport> imports;
    imports.push_back({"fstream", "impl.cpp", 1});

    std::vector<DetectedCallSite> callSites;
    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed()); // private external still exempts the file
}

// Test 20: Internal external function still counts
TEST(ContainmentCheck, InternalExternalFn_StillCounts) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("detail::connect", "connect", Visibility::Internal, true, "detail.cpp"));

    std::vector<HostImport> imports;
    imports.push_back({"sys/socket.h", "detail.cpp", 1});

    std::vector<DetectedCallSite> callSites;
    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed());
}

// =====================================================
// Empty / degenerate inputs
// =====================================================

// Test 21: Empty symbol table — imports exist but no functions declared at all
TEST(ContainmentCheck, EmptySymbolTable_WithImports) {
    SymbolTable symbols; // empty — no functions

    std::vector<HostImport> imports;
    imports.push_back({"sys/socket.h", "orphan.cpp", 1});

    std::vector<DetectedCallSite> callSites;
    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    // File has restricted import but no external fn in that file
    EXPECT_FALSE(result.passed());
    EXPECT_GE(result.errorCount, 1);
}

// Test 22: Empty everything — degenerate but should not crash
TEST(ContainmentCheck, EmptyEverything) {
    SymbolTable symbols;
    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.diagnostics.size(), 0u);
}

// =====================================================
// Diagnostic message precision
// =====================================================

// Test 23: Verify import violation diagnostic has correct file, line, capability
TEST(ContainmentCheck, DiagnosticPrecision_Import) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("x::fn", "fn", Visibility::Public, false, "/project/src/render.cpp"));

    std::vector<HostImport> imports;
    imports.push_back({"netdb.h", "/project/src/render.cpp", 42});

    std::vector<DetectedCallSite> callSites;
    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    ASSERT_EQ(result.diagnostics.size(), 1u);
    const auto& d = result.diagnostics[0];
    EXPECT_EQ(d.check, "containment");
    EXPECT_EQ(d.file, "/project/src/render.cpp");
    EXPECT_EQ(d.line, 42);
    EXPECT_NE(d.message.find("netdb.h"), std::string::npos);
    EXPECT_NE(d.message.find("network"), std::string::npos);
    // Short filename in message (not full path)
    EXPECT_NE(d.message.find("render.cpp"), std::string::npos);
}

// Test 24: Verify call site violation diagnostic has correct caller, callee, file, line
TEST(ContainmentCheck, DiagnosticPrecision_CallSite) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("engine::core::render", "render", Visibility::Public, false, "render.cpp"));

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    callSites.push_back(makeSite("engine::core::render", "dlopen", CapabilityKind::DynamicLoad, "render.cpp", 99));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    ASSERT_EQ(result.diagnostics.size(), 1u);
    const auto& d = result.diagnostics[0];
    EXPECT_EQ(d.check, "containment");
    EXPECT_EQ(d.file, "render.cpp");
    EXPECT_EQ(d.line, 99);
    EXPECT_NE(d.message.find("engine::core::render"), std::string::npos);
    EXPECT_NE(d.message.find("dlopen"), std::string::npos);
    EXPECT_NE(d.message.find("dynamic-load"), std::string::npos);
}

// =====================================================
// Complex multi-file realistic scenario
// =====================================================

// Test 25: Realistic project with mixed external/non-external across multiple files
// io.cpp: external readFile(), external writeFile() + fstream import → OK
// net.cpp: external connect() + socket.h import → OK
// engine.cpp: non-external process(), render() + no restricted imports → OK
// plugin.cpp: non-external loadPlugin() + dlfcn.h import → VIOLATION
// plugin.cpp: non-external loadPlugin() calls dlopen → VIOLATION
// engine.cpp: non-external render() calls readFile (external) → OK (delegation)
TEST(ContainmentCheck, RealisticProject_MixedFiles) {
    SymbolTable symbols;
    // io.cpp
    symbols.addFunction(makeFn("io::readFile", "readFile", Visibility::Public, true, "io.cpp"));
    symbols.addFunction(makeFn("io::writeFile", "writeFile", Visibility::Public, true, "io.cpp"));
    // net.cpp
    symbols.addFunction(makeFn("net::connect", "connect", Visibility::Public, true, "net.cpp"));
    // engine.cpp
    symbols.addFunction(makeFn("engine::process", "process", Visibility::Public, false, "engine.cpp"));
    symbols.addFunction(makeFn("engine::render", "render", Visibility::Public, false, "engine.cpp"));
    // plugin.cpp
    symbols.addFunction(makeFn("plugin::loadPlugin", "loadPlugin", Visibility::Public, false, "plugin.cpp"));

    std::vector<HostImport> imports;
    imports.push_back({"fstream", "io.cpp", 1});
    imports.push_back({"sys/socket.h", "net.cpp", 1});
    imports.push_back({"vector", "engine.cpp", 1});      // safe
    imports.push_back({"algorithm", "engine.cpp", 2});   // safe
    imports.push_back({"dlfcn.h", "plugin.cpp", 1});     // restricted, no external fn → violation

    std::vector<DetectedCallSite> callSites;
    // engine::render() calls readFile (external) → boundary delegation (OK)
    callSites.push_back(makeSite("engine::render", "readFile", CapabilityKind::File, "engine.cpp", 50));
    // plugin::loadPlugin() calls dlopen directly → violation
    callSites.push_back(makeSite("plugin::loadPlugin", "dlopen", CapabilityKind::DynamicLoad, "plugin.cpp", 20));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 2); // 1 import violation (plugin.cpp/dlfcn.h) + 1 call site violation (dlopen)

    // Verify all violations are from plugin.cpp
    for (const auto& d : result.diagnostics) {
        EXPECT_NE(d.message.find("plugin"), std::string::npos);
    }
}

// =====================================================
// Severity boundary: both violations in same check, different types
// =====================================================

// Test 26: Auto mode — both import and call site violations produce warnings, not errors
TEST(ContainmentCheck, AutoMode_AllWarnings) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("x::fn", "fn", Visibility::Public, false, "x.cpp"));

    std::vector<HostImport> imports;
    imports.push_back({"dlfcn.h", "x.cpp", 1});

    std::vector<DetectedCallSite> callSites;
    callSites.push_back(makeSite("x::fn", "fork", CapabilityKind::Process, "x.cpp", 10));

    ContainmentConfig config;
    config.mode = FeatureMode::Auto;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed()); // auto → warnings → passed
    EXPECT_EQ(result.errorCount, 0);
    EXPECT_EQ(result.warningCount, 2); // 1 import + 1 call site
    for (const auto& d : result.diagnostics) {
        EXPECT_EQ(d.severity, Severity::Warning);
    }
}

// Test 27: External fn caller check skips by QUALIFIED name too
TEST(ContainmentCheck, ExternalCallerSkip_ByQualifiedName) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("io::net::send", "send", Visibility::Public, true, "net.cpp"));

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    // Caller is the qualified name of the external function
    callSites.push_back(makeSite("io::net::send", "socket", CapabilityKind::Network, "net.cpp", 15));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed());
}

// Test 28: Regression — non-external fn named same as external fn's simpleName
// The non-external "app::send" must NOT be confused with external "net::send"
TEST(ContainmentCheck, Regression_SimpleNameCollision_CallerNotSkipped) {
    SymbolTable symbols;
    // External function in net module
    symbols.addFunction(makeFn("net::send", "send", Visibility::Public, true, "net.cpp"));
    // Non-external function in app module — same simpleName "send" is coincidental
    symbols.addFunction(makeFn("app::send", "send", Visibility::Public, false, "app.cpp"));

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    // app::send calls socket() — this IS a violation, must not be skipped
    callSites.push_back(makeSite("app::send", "socket", CapabilityKind::Network, "app.cpp", 10));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    // app::send is NOT external (even though it shares simpleName with net::send)
    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 1);
}

// Test 29: Regression — class member external function should be recognized
TEST(ContainmentCheck, Regression_ClassMember_ExternalRecognized) {
    SymbolTable symbols;
    FunctionSymbol classFn;
    classFn.qualifiedName = "net::Client::connect";
    classFn.simpleName = "connect";
    classFn.visibility = Visibility::Public;
    classFn.isExternal = true;  // This is what Bug 1 fix enables
    classFn.location.file = "client.cpp";
    symbols.addFunction(classFn);

    std::vector<HostImport> imports;
    imports.push_back({"sys/socket.h", "client.cpp", 1});

    std::vector<DetectedCallSite> callSites;
    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    // File has external class member function — should pass
    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
}

// Test 30: Dead code removal regression — callee match still works via simpleName
TEST(ContainmentCheck, CalleeMatch_ViaSimpleName_AfterDeadCodeRemoval) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("app::main", "main", Visibility::Public, false, "app.cpp"));
    symbols.addFunction(makeFn("io::readFile", "readFile", Visibility::Public, true, "io.cpp"));

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    // Callee "readFile" matches external fn's simpleName
    callSites.push_back(makeSite("app::main", "readFile", CapabilityKind::File, "app.cpp", 10));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_TRUE(result.passed()); // readFile is external — boundary delegation
}

// Test 31: Mutation guard — caller-skip must use qualifiedNames, not simpleNames
// A global-scope function "send" (qualifiedName = "send") must NOT be skipped
// even though an external function also has simpleName "send"
TEST(ContainmentCheck, MutationGuard_CallerSkipUsesQualifiedName) {
    SymbolTable symbols;
    // External function in net module: qualifiedName="net::send", simpleName="send"
    symbols.addFunction(makeFn("net::send", "send", Visibility::Public, true, "net.cpp"));
    // Non-external function at global scope: qualifiedName="send", simpleName="send"
    // This shares simpleName with the external fn, but is NOT external itself
    FunctionSymbol globalFn;
    globalFn.qualifiedName = "send";  // global scope — no namespace prefix
    globalFn.simpleName = "send";
    globalFn.visibility = Visibility::Public;
    globalFn.isExternal = false;
    globalFn.location.file = "global.cpp";
    symbols.addFunction(globalFn);

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    // The global "send" calls fork() — should be a violation
    // If caller-skip incorrectly uses externalSimpleNames, "send" would match
    // and this violation would be suppressed
    callSites.push_back(makeSite("send", "fork", CapabilityKind::Process, "global.cpp", 5));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    // "send" at global scope is NOT external — must detect the violation
    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 1);
    EXPECT_NE(result.diagnostics[0].message.find("fork"), std::string::npos);
}

// Test 32: Callee simple-name collision regression (issue checker-containment-callee-simplename-collision).
// External `io::read` and a non-external `fs::Reader::read` coexist. A non-external
// caller invoking the user method must NOT have its containment violation skipped
// just because the callee simple-name "read" matches the external function.
TEST(ContainmentCheck, CalleeSimpleNameCollision_NotSkipped) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("io::read", "read", Visibility::Public, true, "io.cpp"));
    symbols.addFunction(makeFn("fs::Reader::read", "read", Visibility::Public, false, "fs.cpp"));
    symbols.addFunction(makeFn("app::main", "main", Visibility::Public, false, "app.cpp"));

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    // app::main calls "read" — but it's the user's Reader::read, and Reader::read
    // itself wraps a system call. The callsite is recorded as System-level unsafe.
    callSites.push_back(makeSite("app::main", "read", CapabilityKind::File, "app.cpp", 12));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    // The callee simple-name "read" is ambiguous because a non-external function
    // shares the simple name. The match must not be granted boundary delegation.
    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 1);
    EXPECT_NE(result.diagnostics[0].message.find("read"), std::string::npos);
}

// Test 33: Callee simple-name match remains valid when no collision exists.
// This guards the existing "boundary delegation via simple name" behavior:
// when the only function with simpleName == calleePattern is external, the
// L1-extracted call site is unambiguously a boundary delegation.
TEST(ContainmentCheck, CalleeSimpleNameMatch_NoCollision_StillSkipped) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("io::writeFile", "writeFile", Visibility::Public, true, "io.cpp"));
    symbols.addFunction(makeFn("app::main", "main", Visibility::Public, false, "app.cpp"));

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    // app::main calls "writeFile" — the only fn with this simple name is external.
    callSites.push_back(makeSite("app::main", "writeFile", CapabilityKind::File, "app.cpp", 8));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    // No collision → callee really is the external function → boundary delegation.
    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.errorCount, 0);
}

// Test 34: Adversarial — a non-external method named identically to multiple
// external API names does not benefit from any of them. Ensures the fix is not
// just guarding the first match but every match is checked for collision.
TEST(ContainmentCheck, CalleeSimpleNameCollision_MultipleExternalsDoNotHelp) {
    SymbolTable symbols;
    symbols.addFunction(makeFn("os::open", "open", Visibility::Public, true, "os.cpp"));
    symbols.addFunction(makeFn("net::open", "open", Visibility::Public, true, "net.cpp"));
    symbols.addFunction(makeFn("app::FileLoader::open", "open", Visibility::Public, false, "loader.cpp"));
    symbols.addFunction(makeFn("app::run", "run", Visibility::Public, false, "app.cpp"));

    std::vector<HostImport> imports;
    std::vector<DetectedCallSite> callSites;
    // app::run calls "open" — must be flagged because FileLoader::open
    // is also a non-external "open", making the simple-name match ambiguous.
    callSites.push_back(makeSite("app::run", "open", CapabilityKind::File, "app.cpp", 17));

    ContainmentConfig config;
    config.mode = FeatureMode::Force;

    CheckResult result;
    checkContainment(symbols, imports, callSites, config, result);
    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 1);
}
