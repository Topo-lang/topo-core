#include "topo/Build/SymbolTableJson.h"
#include "topo/Sema/SymbolTable.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace topo;

// --- AccessPattern enum round-trip ---

TEST(SymbolTableJsonHint, AccessPatternRoundTrip) {
    auto check = [](AccessPattern p, const std::string& expected) {
        json j;
        to_json(j, p);
        EXPECT_EQ(j.get<std::string>(), expected);
        AccessPattern loaded;
        from_json(j, loaded);
        EXPECT_EQ(loaded, p);
    };
    check(AccessPattern::None, "none");
    check(AccessPattern::Streaming, "streaming");
    check(AccessPattern::Random, "random");
    check(AccessPattern::Tiled, "tiled");
    check(AccessPattern::GatherScatter, "gather_scatter");
}

// --- FunctionSymbol with cardinality + accessPattern(Streaming) + tiledSize=0 ---

TEST(SymbolTableJsonHint, FunctionSymbolStreamingRoundTrip) {
    FunctionSymbol fn;
    fn.qualifiedName = "data::process";
    fn.simpleName = "process";
    fn.visibility = Visibility::Public;
    fn.returnType.nameParts = {"void"};
    fn.cardinality = CardinalityHint{1000, 100000};
    fn.accessPattern = AccessPattern::Streaming;
    fn.tiledSize = 0;

    json j = fn;

    // Verify JSON structure
    ASSERT_TRUE(j.contains("cardinality"));
    EXPECT_EQ(j["cardinality"]["min"], 1000);
    EXPECT_EQ(j["cardinality"]["max"], 100000);
    EXPECT_EQ(j["accessPattern"], "streaming");
    // tiledSize == 0 should be omitted (conditional write)
    EXPECT_FALSE(j.contains("tiledSize"));

    // Deserialize
    FunctionSymbol loaded = j.get<FunctionSymbol>();
    EXPECT_EQ(loaded.qualifiedName, "data::process");
    ASSERT_TRUE(loaded.cardinality.has_value());
    EXPECT_EQ(loaded.cardinality->min, 1000);
    EXPECT_EQ(loaded.cardinality->max, 100000);
    EXPECT_EQ(loaded.accessPattern, AccessPattern::Streaming);
    EXPECT_EQ(loaded.tiledSize, 0);
}

// --- FunctionSymbol with accessPattern(Tiled) + tiledSize=64 ---

TEST(SymbolTableJsonHint, FunctionSymbolTiledRoundTrip) {
    FunctionSymbol fn;
    fn.qualifiedName = "data::tile_process";
    fn.simpleName = "tile_process";
    fn.visibility = Visibility::Private;
    fn.returnType.nameParts = {"void"};
    fn.cardinality = CardinalityHint{500, 50000};
    fn.accessPattern = AccessPattern::Tiled;
    fn.tiledSize = 64;

    json j = fn;

    ASSERT_TRUE(j.contains("cardinality"));
    EXPECT_EQ(j["cardinality"]["min"], 500);
    EXPECT_EQ(j["cardinality"]["max"], 50000);
    EXPECT_EQ(j["accessPattern"], "tiled");
    ASSERT_TRUE(j.contains("tiledSize"));
    EXPECT_EQ(j["tiledSize"], 64);

    FunctionSymbol loaded = j.get<FunctionSymbol>();
    ASSERT_TRUE(loaded.cardinality.has_value());
    EXPECT_EQ(loaded.cardinality->min, 500);
    EXPECT_EQ(loaded.cardinality->max, 50000);
    EXPECT_EQ(loaded.accessPattern, AccessPattern::Tiled);
    EXPECT_EQ(loaded.tiledSize, 64);
}

// --- FunctionSymbol with defaults (no hints) ---

TEST(SymbolTableJsonHint, FunctionSymbolNoHintsRoundTrip) {
    FunctionSymbol fn;
    fn.qualifiedName = "app::run";
    fn.simpleName = "run";
    fn.visibility = Visibility::Internal;
    fn.returnType.nameParts = {"int"};

    json j = fn;

    // None of the hint fields should be present
    EXPECT_FALSE(j.contains("cardinality"));
    EXPECT_FALSE(j.contains("accessPattern"));
    EXPECT_FALSE(j.contains("tiledSize"));

    FunctionSymbol loaded = j.get<FunctionSymbol>();
    EXPECT_FALSE(loaded.cardinality.has_value());
    EXPECT_EQ(loaded.accessPattern, AccessPattern::None);
    EXPECT_EQ(loaded.tiledSize, 0);
}

// --- FunctionSymbol @priority round-trip (was silently dropped) ---

TEST(SymbolTableJsonHint, FunctionSymbolPriorityRoundTrip) {
    auto check = [](PriorityLevel pri, bool expectKey) {
        FunctionSymbol fn;
        fn.qualifiedName = "sched::task";
        fn.simpleName = "task";
        fn.visibility = Visibility::Public;
        fn.returnType.nameParts = {"void"};
        fn.priority = pri;

        json j = fn;
        // Normal is the default and is omitted; everything else is written.
        EXPECT_EQ(j.contains("priority"), expectKey);

        FunctionSymbol loaded = j.get<FunctionSymbol>();
        EXPECT_EQ(loaded.priority, pri);
    };
    check(PriorityLevel::Normal, false);
    check(PriorityLevel::Critical, true);
    check(PriorityLevel::High, true);
    check(PriorityLevel::Low, true);
    check(PriorityLevel::Background, true);
}

// --- TypeNode stdlibId + recordFields round-trip ---
//
// Both were omitted from to_json and never restored, so after a round-trip
// isStdlib() returned false and recordFields was empty — fidelity loss that
// would silently corrupt any future consumer dispatching on the stdlib
// identity or the cross-language record byte layout.

TEST(SymbolTableJsonType, StdlibIdRoundTrip) {
    TypeNode t;
    t.nameParts = {"i64"};
    t.stdlibId = stdlib::TypeId::I64;
    ASSERT_TRUE(t.isStdlib());

    json j = t;
    ASSERT_TRUE(j.contains("stdlibId"));
    EXPECT_EQ(j["stdlibId"], "i64");

    TypeNode loaded = j.get<TypeNode>();
    EXPECT_TRUE(loaded.isStdlib());
    EXPECT_EQ(loaded.stdlibId, stdlib::TypeId::I64);
}

TEST(SymbolTableJsonType, NonStdlibTypeOmitsStdlibId) {
    TypeNode t;
    t.nameParts = {"geom", "Mesh"};
    ASSERT_FALSE(t.isStdlib());

    json j = t;
    EXPECT_FALSE(j.contains("stdlibId"));

    TypeNode loaded = j.get<TypeNode>();
    EXPECT_FALSE(loaded.isStdlib());
    EXPECT_EQ(loaded.stdlibId, stdlib::TypeId::None);
}

TEST(SymbolTableJsonType, RecordFieldsRoundTripPreservesOrder) {
    // record<x: f32, y: f32, z: i64> — order matters for byte layout.
    TypeNode t;
    t.nameParts = {"record"};
    t.stdlibId = stdlib::TypeId::Record;

    auto mkField = [](const std::string& name, const std::string& part,
                      stdlib::TypeId id) {
        TypeNode::RecordField f;
        f.name = name;
        TypeNode ft;
        ft.nameParts = {part};
        ft.stdlibId = id;
        f.typeBox.push_back(std::move(ft));
        return f;
    };
    t.recordFields.push_back(mkField("x", "f32", stdlib::TypeId::F32));
    t.recordFields.push_back(mkField("y", "f32", stdlib::TypeId::F32));
    t.recordFields.push_back(mkField("z", "i64", stdlib::TypeId::I64));

    json j = t;
    ASSERT_TRUE(j.contains("recordFields"));
    ASSERT_EQ(j["recordFields"].size(), 3u);

    TypeNode loaded = j.get<TypeNode>();
    ASSERT_EQ(loaded.recordFields.size(), 3u);
    EXPECT_EQ(loaded.recordFields[0].name, "x");
    EXPECT_EQ(loaded.recordFields[1].name, "y");
    EXPECT_EQ(loaded.recordFields[2].name, "z");
    // Nested TypeNode (the field type) must round-trip too.
    EXPECT_EQ(loaded.recordFields[2].type().stdlibId, stdlib::TypeId::I64);
    EXPECT_EQ(loaded.recordFields[0].type().stdlibId, stdlib::TypeId::F32);
    EXPECT_TRUE(loaded.isStdlib());
    EXPECT_EQ(loaded.stdlibId, stdlib::TypeId::Record);
}

// --- LogicBlockEntry isFlow round-trip (was silently dropped) ---

TEST(SymbolTableJsonLogicBlock, IsFlowRoundTrip) {
    LogicBlockEntry e;
    e.qualifiedName = "engine::pipe";
    e.simpleName = "pipe";
    e.isFlow = true;

    json j = e;
    ASSERT_TRUE(j.contains("isFlow"));
    EXPECT_EQ(j["isFlow"], true);

    LogicBlockEntry loaded = j.get<LogicBlockEntry>();
    EXPECT_TRUE(loaded.isFlow);
}

TEST(SymbolTableJsonLogicBlock, IsFlowDefaultsFalseAndOmitted) {
    LogicBlockEntry e;
    e.qualifiedName = "engine::block";
    e.simpleName = "block";
    // isFlow defaults to false.

    json j = e;
    EXPECT_FALSE(j.contains("isFlow"));

    LogicBlockEntry loaded = j.get<LogicBlockEntry>();
    EXPECT_FALSE(loaded.isFlow);
}
