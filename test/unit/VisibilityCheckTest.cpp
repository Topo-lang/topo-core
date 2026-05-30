// Unit tests for checkVisibilityConsistency free function.

#include "topo/Check/VisibilityCheck.h"
#include "topo/Sema/SymbolTable.h"
#include "topo/Sema/VisibilityCollector.h"

#include <gtest/gtest.h>

using namespace topo;
using namespace topo::check;

// Test 1: Empty inputs — no entries, no edges, should pass
TEST(VisibilityCheck, EmptyInputs) {
    SymbolTable symbols;
    std::vector<VisibilityEntry> visEntries;
    std::vector<CallEdge> edges;
    CheckResult result;
    checkVisibilityConsistency(symbols, visEntries, edges, result);
    EXPECT_TRUE(result.passed());
}

// Test 2: Public function called from anywhere — should pass
TEST(VisibilityCheck, PublicCallPasses) {
    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::pub_fn";
    f1.simpleName = "pub_fn";
    f1.visibility = Visibility::Public;
    symbols.addFunction(f1);

    FunctionSymbol f2;
    f2.qualifiedName = "other::caller";
    f2.simpleName = "caller";
    f2.visibility = Visibility::Public;
    symbols.addFunction(f2);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "ns::pub_fn";
    ve.visibility = Visibility::Public;
    visEntries.push_back(ve);

    std::vector<CallEdge> edges;
    edges.push_back({"other::caller", "ns::pub_fn", "test.cpp", 10});
    CheckResult result;
    checkVisibilityConsistency(symbols, visEntries, edges, result);
    EXPECT_TRUE(result.passed());
}

// Test 3: Private function called from same namespace — should pass
TEST(VisibilityCheck, PrivateSameNamespacePasses) {
    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::detail";
    f1.simpleName = "detail";
    f1.visibility = Visibility::Private;
    symbols.addFunction(f1);

    FunctionSymbol f2;
    f2.qualifiedName = "ns::caller";
    f2.simpleName = "caller";
    f2.visibility = Visibility::Public;
    symbols.addFunction(f2);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "ns::detail";
    ve.visibility = Visibility::Private;
    visEntries.push_back(ve);

    std::vector<CallEdge> edges;
    edges.push_back({"ns::caller", "ns::detail", "test.cpp", 10});
    CheckResult result;
    checkVisibilityConsistency(symbols, visEntries, edges, result);
    EXPECT_TRUE(result.passed());
}

// Test 4: Private function called from different namespace — should fail
TEST(VisibilityCheck, PrivateCrossNamespaceFails) {
    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::detail";
    f1.simpleName = "detail";
    f1.visibility = Visibility::Private;
    symbols.addFunction(f1);

    FunctionSymbol f2;
    f2.qualifiedName = "other::caller";
    f2.simpleName = "caller";
    f2.visibility = Visibility::Public;
    symbols.addFunction(f2);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "ns::detail";
    ve.visibility = Visibility::Private;
    visEntries.push_back(ve);

    std::vector<CallEdge> edges;
    edges.push_back({"other::caller", "ns::detail", "test.cpp", 10});
    CheckResult result;
    checkVisibilityConsistency(symbols, visEntries, edges, result);
    EXPECT_FALSE(result.passed());
    EXPECT_GT(result.errorCount, 0);
}

// Test 5: Internal function called from declared function — should pass
TEST(VisibilityCheck, InternalCalledByDeclaredPasses) {
    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::internal_fn";
    f1.simpleName = "internal_fn";
    f1.visibility = Visibility::Internal;
    symbols.addFunction(f1);

    FunctionSymbol f2;
    f2.qualifiedName = "ns::caller";
    f2.simpleName = "caller";
    f2.visibility = Visibility::Public;
    symbols.addFunction(f2);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve1;
    ve1.qualifiedName = "ns::internal_fn";
    ve1.visibility = Visibility::Internal;
    visEntries.push_back(ve1);

    VisibilityEntry ve2;
    ve2.qualifiedName = "ns::caller";
    ve2.visibility = Visibility::Public;
    visEntries.push_back(ve2);

    std::vector<CallEdge> edges;
    edges.push_back({"ns::caller", "ns::internal_fn", "test.cpp", 10});
    CheckResult result;
    checkVisibilityConsistency(symbols, visEntries, edges, result);
    EXPECT_TRUE(result.passed());
}

// Test 6: Internal function called from unknown (undeclared) function — should fail
TEST(VisibilityCheck, InternalCalledByUndeclaredFails) {
    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::internal_fn";
    f1.simpleName = "internal_fn";
    f1.visibility = Visibility::Internal;
    symbols.addFunction(f1);
    // "external::unknown" is NOT in the symbol table

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "ns::internal_fn";
    ve.visibility = Visibility::Internal;
    visEntries.push_back(ve);

    std::vector<CallEdge> edges;
    edges.push_back({"external::unknown", "ns::internal_fn", "test.cpp", 10});
    CheckResult result;
    checkVisibilityConsistency(symbols, visEntries, edges, result);
    EXPECT_FALSE(result.passed());
    EXPECT_GT(result.errorCount, 0);
}

// Test 7: Protected function — no cross-namespace check (protected is advisory)
TEST(VisibilityCheck, ProtectedNoCheck) {
    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::prot_fn";
    f1.simpleName = "prot_fn";
    f1.visibility = Visibility::Protected;
    symbols.addFunction(f1);

    FunctionSymbol f2;
    f2.qualifiedName = "other::caller";
    f2.simpleName = "caller";
    f2.visibility = Visibility::Public;
    symbols.addFunction(f2);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "ns::prot_fn";
    ve.visibility = Visibility::Protected;
    visEntries.push_back(ve);

    std::vector<CallEdge> edges;
    edges.push_back({"other::caller", "ns::prot_fn", "test.cpp", 10});
    CheckResult result;
    checkVisibilityConsistency(symbols, visEntries, edges, result);
    EXPECT_TRUE(result.passed());
}

// Test 8: Mixed visibility — multiple entries, one violation
TEST(VisibilityCheck, MixedVisibilityOneViolation) {
    SymbolTable symbols;
    FunctionSymbol f1;
    f1.qualifiedName = "ns::pub_fn";
    f1.simpleName = "pub_fn";
    f1.visibility = Visibility::Public;
    symbols.addFunction(f1);

    FunctionSymbol f2;
    f2.qualifiedName = "ns::priv_fn";
    f2.simpleName = "priv_fn";
    f2.visibility = Visibility::Private;
    symbols.addFunction(f2);

    FunctionSymbol f3;
    f3.qualifiedName = "other::caller";
    f3.simpleName = "caller";
    f3.visibility = Visibility::Public;
    symbols.addFunction(f3);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve1;
    ve1.qualifiedName = "ns::pub_fn";
    ve1.visibility = Visibility::Public;
    visEntries.push_back(ve1);

    VisibilityEntry ve2;
    ve2.qualifiedName = "ns::priv_fn";
    ve2.visibility = Visibility::Private;
    visEntries.push_back(ve2);

    std::vector<CallEdge> edges;
    edges.push_back({"other::caller", "ns::pub_fn", "test.cpp", 10});  // ok
    edges.push_back({"other::caller", "ns::priv_fn", "test.cpp", 20}); // violation
    CheckResult result;
    checkVisibilityConsistency(symbols, visEntries, edges, result);
    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 1);
}

// Test 9 (M3 E.2 adversarial): nested namespace — `a::b::c::detail` private
// called from `a::b::c::caller`. Same fully-qualified containing namespace,
// so the call must be allowed regardless of how deep the nesting is.
TEST(VisibilityCheck, NestedNamespacePrivate_SameNamespacePasses) {
    SymbolTable symbols;
    FunctionSymbol detail;
    detail.qualifiedName = "a::b::c::detail";
    detail.simpleName = "detail";
    detail.visibility = Visibility::Private;
    symbols.addFunction(detail);

    FunctionSymbol caller;
    caller.qualifiedName = "a::b::c::caller";
    caller.simpleName = "caller";
    caller.visibility = Visibility::Public;
    symbols.addFunction(caller);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "a::b::c::detail";
    ve.visibility = Visibility::Private;
    visEntries.push_back(ve);

    std::vector<CallEdge> edges;
    edges.push_back({"a::b::c::caller", "a::b::c::detail", "test.cpp", 10});
    CheckResult result;
    checkVisibilityConsistency(symbols, visEntries, edges, result);
    EXPECT_TRUE(result.passed());
}

// Test 10 (M3 E.2 adversarial): nested namespace divergence — caller is one
// namespace level shallower than the private callee's namespace. Must fail
// because they are not in the same containing namespace.
TEST(VisibilityCheck, NestedNamespacePrivate_OuterCallerFails) {
    SymbolTable symbols;
    FunctionSymbol detail;
    detail.qualifiedName = "a::b::c::detail";
    detail.simpleName = "detail";
    detail.visibility = Visibility::Private;
    symbols.addFunction(detail);

    FunctionSymbol caller;
    caller.qualifiedName = "a::b::caller"; // one level shallower
    caller.simpleName = "caller";
    caller.visibility = Visibility::Public;
    symbols.addFunction(caller);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "a::b::c::detail";
    ve.visibility = Visibility::Private;
    visEntries.push_back(ve);

    std::vector<CallEdge> edges;
    edges.push_back({"a::b::caller", "a::b::c::detail", "test.cpp", 10});
    CheckResult result;
    checkVisibilityConsistency(symbols, visEntries, edges, result);
    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 1);
}

// Test 11 (M3 E.2 adversarial): global-scope private — qualifiedName has no
// `::`. The "containing namespace" of a global-scope symbol is the empty
// namespace. Another global-scope caller shares that empty namespace, so
// the private call must be allowed.
TEST(VisibilityCheck, GlobalScopePrivate_GlobalCallerPasses) {
    SymbolTable symbols;
    FunctionSymbol detail;
    detail.qualifiedName = "detail"; // global scope
    detail.simpleName = "detail";
    detail.visibility = Visibility::Private;
    symbols.addFunction(detail);

    FunctionSymbol caller;
    caller.qualifiedName = "main"; // global scope
    caller.simpleName = "main";
    caller.visibility = Visibility::Public;
    symbols.addFunction(caller);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "detail";
    ve.visibility = Visibility::Private;
    visEntries.push_back(ve);

    std::vector<CallEdge> edges;
    edges.push_back({"main", "detail", "test.cpp", 10});
    CheckResult result;
    checkVisibilityConsistency(symbols, visEntries, edges, result);
    EXPECT_TRUE(result.passed());
}

// =============================================================================
// Cross-module / cross-TU adversarial fixtures
// (close a gap: the visibility checker lacked cross-module escape coverage).
//
// The current checkVisibilityConsistency API operates on a *flat* CallEdge list
// and a single SymbolTable — there is no first-class "module" or TU identifier.
// The caller/callee are qualified names, and CallEdge only carries file+line
// for the diagnostic location, not for scope decisions. Cross-module escape is
// therefore simulated by:
//   1. The caller living in a different fully-qualified namespace than the
//      callee (matching a different TU in practice), which triggers the
//      Private "outside its namespace" path.
//   2. The caller being absent from the SymbolTable (i.e. not a declared .topo
//      function), which triggers the Internal "called from external" path.
//   3. The CallEdge.file field pointing to a separate source file, which
//      encodes the TU boundary in the diagnostic even though the check logic
//      itself does not read it.
// Scenarios a real multi-TU harness would add (e.g. tracking a per-TU view of
// the symbol table, or a per-TU visibility override) are intentionally NOT
// synthesized — doing so would require extending CallEdge or the checker API
// and would therefore fall outside "add tests only". Those gaps are recorded
// in the issue's Resolution notes so follow-up work is visible.
// =============================================================================

// PrivateSymbolLeakViaFriend_Reports:
//   Module A declares `a::Vault::unlock` private. Module B's free function
//   `b::unsealer` declares itself a C++ friend of a::Vault — at the Topo
//   check layer that friend grant is invisible; the call still crosses
//   namespaces, so the private-escape rule must fire. This pins the behavior
//   "host-language friend does NOT relax a .topo private boundary".
TEST(VisibilityCheck, PrivateSymbolLeakViaFriend_Reports) {
    SymbolTable symbols;

    // Module A: private member-ish free function.
    FunctionSymbol vaultUnlock;
    vaultUnlock.qualifiedName = "a::Vault::unlock";
    vaultUnlock.simpleName = "unlock";
    vaultUnlock.visibility = Visibility::Private;
    symbols.addFunction(vaultUnlock);

    // Module B: free function that the host source marked `friend` of Vault.
    // From the checker's perspective it is just another declared function.
    FunctionSymbol unsealer;
    unsealer.qualifiedName = "b::unsealer";
    unsealer.simpleName = "unsealer";
    unsealer.visibility = Visibility::Public;
    symbols.addFunction(unsealer);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "a::Vault::unlock";
    ve.visibility = Visibility::Private;
    visEntries.push_back(ve);

    // Cross-TU edge — file path reflects B's TU, not A's.
    std::vector<CallEdge> edges;
    edges.push_back({"b::unsealer", "a::Vault::unlock", "b/unsealer.cpp", 17});

    CheckResult result;
    checkVisibilityConsistency(symbols, visEntries, edges, result);

    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 1);
    ASSERT_EQ(result.diagnostics.size(), 1u);
    const auto& diag = result.diagnostics[0];
    EXPECT_EQ(diag.check, "visibility");
    EXPECT_EQ(diag.severity, Severity::Error);
    EXPECT_NE(diag.message.find("a::Vault::unlock"), std::string::npos);
    EXPECT_NE(diag.message.find("b::unsealer"), std::string::npos);
    EXPECT_NE(diag.message.find("private"), std::string::npos);
    // Diagnostic carries the B-side location where the escape was observed.
    EXPECT_EQ(diag.file, "b/unsealer.cpp");
    EXPECT_EQ(diag.line, 17);
}

// PrivateSymbolEscapesViaTemplateInstantiation_Reports:
//   Module A declares `a::detail::_Secret` as a private helper type — modeled
//   here as a private function entry because SymbolTable's visibility map keys
//   on qualified-name symbols, and the visibility rule is symmetric for
//   functions and types. Module B's `b::makeContainer` instantiates
//   `Container<a::detail::_Secret>` which, after the host compiler expands
//   the template, produces a direct reference from `b::makeContainer` to
//   the private symbol. The checker must flag the cross-namespace access.
TEST(VisibilityCheck, PrivateSymbolEscapesViaTemplateInstantiation_Reports) {
    SymbolTable symbols;

    // Private helper in module A.
    FunctionSymbol secret;
    secret.qualifiedName = "a::detail::_Secret";
    secret.simpleName = "_Secret";
    secret.visibility = Visibility::Private;
    symbols.addFunction(secret);

    // Public template-instantiating function in module B.
    FunctionSymbol maker;
    maker.qualifiedName = "b::makeContainer";
    maker.simpleName = "makeContainer";
    maker.visibility = Visibility::Public;
    symbols.addFunction(maker);

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "a::detail::_Secret";
    ve.visibility = Visibility::Private;
    visEntries.push_back(ve);

    // Post-instantiation, a reference edge points from b::makeContainer to
    // a::detail::_Secret. The file is B's TU.
    std::vector<CallEdge> edges;
    edges.push_back({"b::makeContainer", "a::detail::_Secret",
                     "b/make_container.cpp", 33});

    CheckResult result;
    checkVisibilityConsistency(symbols, visEntries, edges, result);

    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 1);
    ASSERT_EQ(result.diagnostics.size(), 1u);
    const auto& diag = result.diagnostics[0];
    EXPECT_EQ(diag.check, "visibility");
    EXPECT_EQ(diag.severity, Severity::Error);
    EXPECT_NE(diag.message.find("a::detail::_Secret"), std::string::npos);
    EXPECT_NE(diag.message.find("b::makeContainer"), std::string::npos);
    EXPECT_NE(diag.message.find("private"), std::string::npos);
    EXPECT_EQ(diag.file, "b/make_container.cpp");
    EXPECT_EQ(diag.line, 33);
}

// InternalSymbolCalledViaFunctionPointer_Reports:
//   Module A exposes `a::_internal_tick` as internal. Module B takes its
//   address and invokes it from a function that is NOT declared in any .topo
//   (e.g. a callback registered with a third-party library, or an
//   auto-generated trampoline). The checker's Internal rule fires when the
//   caller is absent from the SymbolTable, so this cleanly models "internal
//   symbol escapes into a TU that Topo does not manage".
TEST(VisibilityCheck, InternalSymbolCalledViaFunctionPointer_Reports) {
    SymbolTable symbols;

    FunctionSymbol tick;
    tick.qualifiedName = "a::_internal_tick";
    tick.simpleName = "_internal_tick";
    tick.visibility = Visibility::Internal;
    symbols.addFunction(tick);
    // Intentionally do NOT add the caller — it represents code in another
    // TU that Topo did not analyze (e.g. third-party callback registration).

    std::vector<VisibilityEntry> visEntries;
    VisibilityEntry ve;
    ve.qualifiedName = "a::_internal_tick";
    ve.visibility = Visibility::Internal;
    visEntries.push_back(ve);

    std::vector<CallEdge> edges;
    edges.push_back({"thirdparty::dispatch_callback", "a::_internal_tick",
                     "external/vendor_glue.cpp", 88});

    CheckResult result;
    checkVisibilityConsistency(symbols, visEntries, edges, result);

    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 1);
    ASSERT_EQ(result.diagnostics.size(), 1u);
    const auto& diag = result.diagnostics[0];
    EXPECT_EQ(diag.check, "visibility");
    EXPECT_EQ(diag.severity, Severity::Error);
    EXPECT_NE(diag.message.find("a::_internal_tick"), std::string::npos);
    EXPECT_NE(diag.message.find("thirdparty::dispatch_callback"),
              std::string::npos);
    EXPECT_NE(diag.message.find("internal"), std::string::npos);
    EXPECT_EQ(diag.file, "external/vendor_glue.cpp");
    EXPECT_EQ(diag.line, 88);
}

// Test 12 (M3 E.2 adversarial): multi-violation count. Three private callees
// all called from a foreign namespace produce three separate violations.
TEST(VisibilityCheck, MultiplePrivateViolationsCounted) {
    SymbolTable symbols;
    for (const auto& name : {"detail_a", "detail_b", "detail_c"}) {
        FunctionSymbol fn;
        fn.qualifiedName = std::string("ns::") + name;
        fn.simpleName = name;
        fn.visibility = Visibility::Private;
        symbols.addFunction(fn);
    }
    FunctionSymbol caller;
    caller.qualifiedName = "other::caller";
    caller.simpleName = "caller";
    caller.visibility = Visibility::Public;
    symbols.addFunction(caller);

    std::vector<VisibilityEntry> visEntries;
    for (const auto& name : {"detail_a", "detail_b", "detail_c"}) {
        VisibilityEntry ve;
        ve.qualifiedName = std::string("ns::") + name;
        ve.visibility = Visibility::Private;
        visEntries.push_back(ve);
    }

    std::vector<CallEdge> edges;
    edges.push_back({"other::caller", "ns::detail_a", "t.cpp", 1});
    edges.push_back({"other::caller", "ns::detail_b", "t.cpp", 2});
    edges.push_back({"other::caller", "ns::detail_c", "t.cpp", 3});
    CheckResult result;
    checkVisibilityConsistency(symbols, visEntries, edges, result);
    EXPECT_FALSE(result.passed());
    EXPECT_EQ(result.errorCount, 3);
}
