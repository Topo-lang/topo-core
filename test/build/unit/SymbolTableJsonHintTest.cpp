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
