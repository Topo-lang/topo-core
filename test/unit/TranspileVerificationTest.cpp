// Post-transpile verification: structured summary + configurable gate.
//
// These tests exercise the pure, subprocess-free core that
// TranspileDriver::run() uses: verifyModule() (aggregate) and
// applyVerificationGate() (the exact "success=false iff N>k + precise
// error" decision). Driving the real extractor subprocess end-to-end is a
// separate ctest concern; the gate logic itself is fully deterministic and
// is unit-tested here.

#include "topo/Transpile/TranspileModel.h"

#include <gtest/gtest.h>
#include <optional>

using namespace topo::transpile;

namespace {

TranspileFunction makeFn(std::string name,
                         std::vector<std::string> unsupported,
                         Fidelity fid) {
    TranspileFunction fn;
    fn.qualifiedName = std::move(name);
    fn.unsupported = std::move(unsupported);
    fn.fidelity = fid;
    return fn;
}

} // namespace

// --- verifyModule: aggregate counts ---------------------------------------

TEST(TranspileVerification, EmptyModuleIsClean) {
    TranspileModule m;
    auto v = verifyModule(m);
    EXPECT_EQ(v.totalUnsupported, 0);
    EXPECT_TRUE(v.perFunction.empty());
    EXPECT_TRUE(v.clean());
    EXPECT_EQ(v.fidelity.total(), 0);
}

TEST(TranspileVerification, TotalUnsupportedSumsAcrossFunctions) {
    TranspileModule m;
    m.functions.push_back(makeFn("a", {"async", "decorator"}, Fidelity::Recovered));
    m.functions.push_back(makeFn("b", {}, Fidelity::Source));
    m.functions.push_back(makeFn("c", {"union"}, Fidelity::Inferred));

    auto v = verifyModule(m);
    EXPECT_EQ(v.totalUnsupported, 3);
    EXPECT_FALSE(v.clean());
    // Only functions with >=1 unsupported are listed.
    ASSERT_EQ(v.perFunction.size(), 2u);
    EXPECT_EQ(v.perFunction[0].qualifiedName, "a");
    EXPECT_EQ(v.perFunction[0].constructs.size(), 2u);
    EXPECT_EQ(v.perFunction[1].qualifiedName, "c");
    EXPECT_EQ(v.perFunction[1].constructs.size(), 1u);
}

TEST(TranspileVerification, FidelityBreakdownCountsFunctionsTypesAndFields) {
    TranspileModule m;
    m.functions.push_back(makeFn("a", {}, Fidelity::Source));
    m.functions.push_back(makeFn("b", {}, Fidelity::Recovered));
    m.functions.push_back(makeFn("c", {}, Fidelity::Inferred));

    TranspileType ty;
    ty.qualifiedName = "T";
    ty.fidelity = Fidelity::Recovered;
    TranspileField f1;
    f1.name = "x";
    f1.fidelity = Fidelity::Source;
    TranspileField f2;
    f2.name = "y";
    f2.fidelity = Fidelity::Inferred;
    ty.fields = {f1, f2};
    m.types.push_back(ty);

    auto v = verifyModule(m);
    // functions: 1S 1R 1I; type: 1R; fields: 1S 1I
    EXPECT_EQ(v.fidelity.source, 2);
    EXPECT_EQ(v.fidelity.recovered, 2);
    EXPECT_EQ(v.fidelity.inferred, 2);
    EXPECT_EQ(v.fidelity.total(), 6);
}

// --- applyVerificationGate: configurable gate -----------------------------

TEST(TranspileVerificationGate, NoGateNeverFails) {
    TranspileModule m;
    m.functions.push_back(makeFn("a", {"async", "union"}, Fidelity::Inferred));
    auto v = verifyModule(m);

    auto d = applyVerificationGate(v, std::nullopt);
    EXPECT_FALSE(d.failed);
    EXPECT_TRUE(d.error.empty());
}

TEST(TranspileVerificationGate, StrictZeroFailsOnAnyUnsupported) {
    TranspileModule m;
    m.functions.push_back(makeFn("a", {"async"}, Fidelity::Recovered));
    auto v = verifyModule(m);

    auto d = applyVerificationGate(v, /*verifyMaxUnsupported=*/0);
    EXPECT_TRUE(d.failed);
    EXPECT_NE(d.error.find("post-transpile verification failed"),
              std::string::npos);
    EXPECT_NE(d.error.find("limit of 0"), std::string::npos);
}

TEST(TranspileVerificationGate, StrictZeroPassesWhenClean) {
    TranspileModule m;
    m.functions.push_back(makeFn("a", {}, Fidelity::Source));
    auto v = verifyModule(m);

    auto d = applyVerificationGate(v, 0);
    EXPECT_FALSE(d.failed);
}

TEST(TranspileVerificationGate, ToleranceBoundaryNEqualsKPasses) {
    // N == k must pass; only N > k fails.
    TranspileModule m;
    m.functions.push_back(makeFn("a", {"u1", "u2"}, Fidelity::Recovered)); // N=2
    auto v = verifyModule(m);
    ASSERT_EQ(v.totalUnsupported, 2);

    EXPECT_FALSE(applyVerificationGate(v, 2).failed); // N==k -> pass
    EXPECT_FALSE(applyVerificationGate(v, 3).failed); // N<k  -> pass
    auto fail = applyVerificationGate(v, 1);          // N>k  -> fail
    EXPECT_TRUE(fail.failed);
    EXPECT_NE(fail.error.find("2 unsupported construct(s)"),
              std::string::npos);
    EXPECT_NE(fail.error.find("limit of 1"), std::string::npos);
}
