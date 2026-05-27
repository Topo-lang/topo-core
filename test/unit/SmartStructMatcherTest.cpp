#include "topo/Sema/SmartStructMatcher.h"
#include <gtest/gtest.h>

using namespace topo;

static ReturnParam makeRP(const std::string& typeName, const std::string& name) {
    ReturnParam rp;
    rp.type.nameParts = {typeName};
    rp.name = name;
    return rp;
}

static Parameter makeParam(const std::string& typeName, const std::string& name) {
    Parameter p;
    p.type.nameParts = {typeName};
    p.name = name;
    return p;
}

TEST(SmartStructMatcher, UniqueTypeMatch) {
    std::vector<ReturnParam> source = {
        makeRP("int", "x"),
        makeRP("bool", "y"),
    };
    std::vector<Parameter> target = {
        makeParam("bool", "flag"),
        makeParam("int", "val"),
    };

    auto result = SmartStructMatcher::match(source, target);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.matches.size(), 2u);
    EXPECT_TRUE(result.errors.empty());
}

TEST(SmartStructMatcher, SameTypeNameMatch) {
    std::vector<ReturnParam> source = {
        makeRP("int", "a"),
        makeRP("int", "b"),
    };
    std::vector<Parameter> target = {
        makeParam("int", "b"),
    };

    auto result = SmartStructMatcher::match(source, target);
    EXPECT_TRUE(result.success);
    ASSERT_EQ(result.matches.size(), 1u);
    // Should match source[1] ("b") to target[0] ("b")
    EXPECT_EQ(result.matches[0].first, 1u);
    EXPECT_EQ(result.matches[0].second, 0u);
    // Warnings about name-based resolution
    EXPECT_FALSE(result.warnings.empty());
}

TEST(SmartStructMatcher, SameTypeNoNameMatch) {
    std::vector<ReturnParam> source = {
        makeRP("int", "x"),
        makeRP("int", "y"),
    };
    std::vector<Parameter> target = {
        makeParam("int", "z"),
    };

    auto result = SmartStructMatcher::match(source, target);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errors.empty());
}

TEST(SmartStructMatcher, NoTypeMatch) {
    std::vector<ReturnParam> source = {
        makeRP("int", "x"),
    };
    std::vector<Parameter> target = {
        makeParam("bool", "flag"),
    };

    auto result = SmartStructMatcher::match(source, target);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errors.empty());
}

TEST(SmartStructMatcher, PartialMatchSourceExtra) {
    // Source has extra fields, target is fully matched
    std::vector<ReturnParam> source = {
        makeRP("int", "x"),
        makeRP("bool", "y"),
        makeRP("int", "z"),
    };
    std::vector<Parameter> target = {
        makeParam("bool", "flag"),
    };

    auto result = SmartStructMatcher::match(source, target);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.matches.size(), 1u);
    EXPECT_EQ(result.unmatchedSource.size(), 2u); // x and z are extra
}

TEST(SmartStructMatcher, EmptyTarget) {
    std::vector<ReturnParam> source = {
        makeRP("int", "x"),
    };
    std::vector<Parameter> target = {};

    auto result = SmartStructMatcher::match(source, target);
    EXPECT_TRUE(result.success); // nothing to match
    EXPECT_TRUE(result.matches.empty());
}

TEST(SmartStructMatcher, EmptyBoth) {
    auto result = SmartStructMatcher::match({}, {});
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.matches.empty());
}
