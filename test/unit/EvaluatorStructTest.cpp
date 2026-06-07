// Struct-leaf field projection in the query evaluator.
//
// Verifies:
//   1. `sum(p.x)` / `sum(p.y)` / `sum(p.z)` over an AoS `Particle[4]`
//      strided-fold path returns the per-column sum (22, 26, 30 for the
//      tiny_struct.cpp fixture's hard-coded values).
//   2. `sum(p)` without a field selector emits the "field selection
//      required" guidance instead of silently reducing.
//   3. `p.unknown` reports the missing field name and the struct type.
//   4. `shape(p)` / `count(p)` / `dtype(p)` operate on the struct frame
//      without needing a field — they read layout-only metadata, so they
//      must bypass the dtype-supported guard.
//
// The fixture mirrors `topo-lang-cpp/topo-debug/test/fixtures/tiny_struct.cpp`:
// 4 `{float x; float y; float z;}` records initialised to
// {(1,2,3), (4,5,6), (7,8,9), (10,11,12)}. We synthesize the bytes by
// hand (little-endian floats, no padding given 3×4 = 12B stride matches
// natural alignment on the platforms we care about).

#include "topo/Debug/Query/Evaluator.h"
#include "topo/Debug/Query/Parser.h"
#include "topo/Debug/Query/Value.h"

#include <cstring>
#include <gtest/gtest.h>

using namespace topo::debug_query;

namespace {

// Build the 48-byte AoS bytes for Particle p[4].
std::vector<uint8_t> makeParticleBytes() {
    const float vals[12] = {
        1, 2, 3,
        4, 5, 6,
        7, 8, 9,
        10, 11, 12,
    };
    std::vector<uint8_t> bytes(sizeof(vals));
    std::memcpy(bytes.data(), vals, sizeof(vals));
    return bytes;
}

FrameView makeParticleView() {
    LayoutDescriptor layout;
    layout.dtype = "struct";
    layout.shape = {4};
    layout.isStruct = true;
    layout.structLayout.name = "Particle";
    layout.structLayout.strideBytes = 12;
    layout.structLayout.fields = {
        {"x", 0, "f32"},
        {"y", 4, "f32"},
        {"z", 8, "f32"},
    };
    return FrameView::owned("p", makeParticleBytes(), std::move(layout));
}

EvalResult run(const std::string& expr, const Environment& env) {
    std::string parseErr;
    auto ast = parseQuery(expr, parseErr);
    EXPECT_TRUE(ast) << "parse error on `" << expr << "`: " << parseErr;
    return evaluate(*ast, env);
}

} // namespace

TEST(EvaluatorStructFieldAccess, SumOfEachFieldColumn) {
    Environment env;
    env.variables.emplace("p", makeParticleView());

    auto rx = run("sum(p.x)", env);
    ASSERT_TRUE(rx.ok) << rx.error;
    ASSERT_EQ(rx.value.kind, ValueKind::Float);
    EXPECT_DOUBLE_EQ(rx.value.floatVal, 22.0);

    auto ry = run("sum(p.y)", env);
    ASSERT_TRUE(ry.ok) << ry.error;
    ASSERT_EQ(ry.value.kind, ValueKind::Float);
    EXPECT_DOUBLE_EQ(ry.value.floatVal, 26.0);

    auto rz = run("sum(p.z)", env);
    ASSERT_TRUE(rz.ok) << rz.error;
    ASSERT_EQ(rz.value.kind, ValueKind::Float);
    EXPECT_DOUBLE_EQ(rz.value.floatVal, 30.0);
}

TEST(EvaluatorStructFieldAccess, OtherReductionsOverField) {
    // mean/min/max should likewise walk the strided view.
    Environment env;
    env.variables.emplace("p", makeParticleView());

    auto mean = run("mean(p.x)", env);
    ASSERT_TRUE(mean.ok) << mean.error;
    ASSERT_EQ(mean.value.kind, ValueKind::Float);
    EXPECT_DOUBLE_EQ(mean.value.floatVal, 22.0 / 4.0);

    auto minR = run("min(p.x)", env);
    ASSERT_TRUE(minR.ok) << minR.error;
    EXPECT_DOUBLE_EQ(minR.value.floatVal, 1.0);

    auto maxR = run("max(p.x)", env);
    ASSERT_TRUE(maxR.ok) << maxR.error;
    EXPECT_DOUBLE_EQ(maxR.value.floatVal, 10.0);
}

TEST(EvaluatorStructFieldAccess, SumOnStructWithoutFieldErrors) {
    Environment env;
    env.variables.emplace("p", makeParticleView());

    auto r = run("sum(p)", env);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("field selection required"), std::string::npos)
        << r.error;
    // The hint should name one of the actual fields (we suggest `p.x` since
    // it is the first field in declaration order).
    EXPECT_NE(r.error.find("sum(p.x)"), std::string::npos) << r.error;
}

TEST(EvaluatorStructFieldAccess, UnknownFieldErrors) {
    Environment env;
    env.variables.emplace("p", makeParticleView());

    auto r = run("p.unknown", env);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("unknown field"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find("unknown"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find("Particle"), std::string::npos) << r.error;
}

TEST(EvaluatorStructFieldAccess, MeanMinMaxAlsoErrorOnRawStruct) {
    Environment env;
    env.variables.emplace("p", makeParticleView());

    for (const char* op : {"mean", "min", "max"}) {
        auto r = run(std::string(op) + "(p)", env);
        EXPECT_FALSE(r.ok) << op;
        EXPECT_NE(r.error.find("field selection required"), std::string::npos)
            << op << " err: " << r.error;
    }
}

TEST(EvaluatorStructShapeAndCount, LayoutBuiltinsBypassDtypeGuard) {
    Environment env;
    env.variables.emplace("p", makeParticleView());

    auto shape = run("shape(p)", env);
    ASSERT_TRUE(shape.ok) << shape.error;
    ASSERT_EQ(shape.value.kind, ValueKind::IntList);
    ASSERT_EQ(shape.value.intList.size(), 1u);
    EXPECT_EQ(shape.value.intList[0], 4);

    auto count = run("count(p)", env);
    ASSERT_TRUE(count.ok) << count.error;
    ASSERT_EQ(count.value.kind, ValueKind::Int);
    EXPECT_EQ(count.value.intVal, 4);

    auto dtype = run("dtype(p)", env);
    ASSERT_TRUE(dtype.ok) << dtype.error;
    ASSERT_EQ(dtype.value.kind, ValueKind::String);
    EXPECT_EQ(dtype.value.strVal, "struct");
}

// --- Slicing a strided (struct field-access) view ---
//
// Before the fix, sliceFrame stepped dim0 by `rowElems * elemSize`
// (contiguous), so `sum(p.x[1..3])` folded the f32 at bytes 4 and 8
// (which are the FIRST element's y and z fields), yielding 2+3 = 5
// instead of the x column at rows 1 and 2 (4 and 7), which sum to 11.
TEST(EvaluatorStructFieldAccess, SliceOfStridedFieldFoldsStrideSpaced) {
    Environment env;
    env.variables.emplace("p", makeParticleView());

    // x column is {1, 4, 7, 10}; rows [1..3) → {4, 7}.
    auto rx = run("sum(p.x[1..3])", env);
    ASSERT_TRUE(rx.ok) << rx.error;
    ASSERT_EQ(rx.value.kind, ValueKind::Float);
    EXPECT_DOUBLE_EQ(rx.value.floatVal, 11.0);

    // y column is {2, 5, 8, 11}; rows [0..2) → {2, 5} → 7.
    auto ry = run("sum(p.y[0..2])", env);
    ASSERT_TRUE(ry.ok) << ry.error;
    ASSERT_EQ(ry.value.kind, ValueKind::Float);
    EXPECT_DOUBLE_EQ(ry.value.floatVal, 7.0);

    // z column is {3, 6, 9, 12}; max over rows [2..4) → {9, 12} → 12.
    auto mz = run("max(p.z[2..4])", env);
    ASSERT_TRUE(mz.ok) << mz.error;
    ASSERT_EQ(mz.value.kind, ValueKind::Float);
    EXPECT_DOUBLE_EQ(mz.value.floatVal, 12.0);

    // count over a sliced strided view reflects the slice length.
    auto cnt = run("count(p.x[1..3])", env);
    ASSERT_TRUE(cnt.ok) << cnt.error;
    ASSERT_EQ(cnt.value.kind, ValueKind::Int);
    EXPECT_EQ(cnt.value.intVal, 2);
}

// --- Integer overflow in evalBinaryOp returns an error, not UB ---

TEST(EvaluatorArithmeticOverflow, AddOverflowReportsError) {
    Environment env; // no frame needed — pure scalar arithmetic
    auto r = run("9000000000000000000 + 9000000000000000000", env);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("integer overflow"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find("+"), std::string::npos) << r.error;
}

TEST(EvaluatorArithmeticOverflow, MulOverflowReportsError) {
    Environment env;
    auto r = run("9000000000000000000 * 2", env);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("integer overflow"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find("*"), std::string::npos) << r.error;
}

TEST(EvaluatorArithmeticOverflow, SubOverflowReportsError) {
    Environment env;
    // INT64_MIN - 1 underflows.
    auto r = run("-9223372036854775807 - 2", env);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("integer overflow"), std::string::npos) << r.error;
}

TEST(EvaluatorArithmeticOverflow, InRangeArithmeticStillWorks) {
    Environment env;
    auto add = run("2 + 3", env);
    ASSERT_TRUE(add.ok) << add.error;
    ASSERT_EQ(add.value.kind, ValueKind::Int);
    EXPECT_EQ(add.value.intVal, 5);

    auto mul = run("100 * 100", env);
    ASSERT_TRUE(mul.ok) << mul.error;
    ASSERT_EQ(mul.value.kind, ValueKind::Int);
    EXPECT_EQ(mul.value.intVal, 10000);
}
