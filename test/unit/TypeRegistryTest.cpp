#include "topo/Sema/TypeRegistry.h"
#include <gtest/gtest.h>

using namespace topo;

class TypeRegistryTest : public ::testing::Test {
protected:
    TypeRegistry reg = TypeRegistry::createDefault();
};

// --- Rust backend ---

TEST_F(TypeRegistryTest, RustPrimitivesClassified) {
    EXPECT_EQ(reg.classify("std::rust::i32"), LogicalTypeKind::Integer);
    EXPECT_EQ(reg.classify("std::rust::i64"), LogicalTypeKind::Integer);
    EXPECT_EQ(reg.classify("std::rust::u32"), LogicalTypeKind::UnsignedInteger);
    EXPECT_EQ(reg.classify("std::rust::u64"), LogicalTypeKind::UnsignedInteger);
    EXPECT_EQ(reg.classify("std::rust::f64"), LogicalTypeKind::Floating);
    EXPECT_EQ(reg.classify("std::rust::f32"), LogicalTypeKind::Floating);
    EXPECT_EQ(reg.classify("std::rust::bool"), LogicalTypeKind::Boolean);
}

TEST_F(TypeRegistryTest, RustContainersClassified) {
    EXPECT_EQ(reg.classify("std::rust::Vec"), LogicalTypeKind::Sequence);
    EXPECT_EQ(reg.classify("std::rust::VecDeque"), LogicalTypeKind::Sequence);
    EXPECT_EQ(reg.classify("std::rust::LinkedList"), LogicalTypeKind::Sequence);
    EXPECT_EQ(reg.classify("std::rust::HashMap"), LogicalTypeKind::Mapping);
    EXPECT_EQ(reg.classify("std::rust::BTreeMap"), LogicalTypeKind::Mapping);
}

TEST_F(TypeRegistryTest, RustOptionResultOpaque) {
    EXPECT_EQ(reg.classify("std::rust::Option"), LogicalTypeKind::Opaque);
    EXPECT_EQ(reg.classify("std::rust::Result"), LogicalTypeKind::Opaque);
}

TEST_F(TypeRegistryTest, RustSmartPointersOpaque) {
    EXPECT_EQ(reg.classify("std::rust::Box"), LogicalTypeKind::Opaque);
    EXPECT_EQ(reg.classify("std::rust::Rc"), LogicalTypeKind::Opaque);
    EXPECT_EQ(reg.classify("std::rust::Arc"), LogicalTypeKind::Opaque);
    EXPECT_EQ(reg.classify("std::rust::Weak"), LogicalTypeKind::Opaque);
}

TEST_F(TypeRegistryTest, RustTextTypes) {
    EXPECT_EQ(reg.classify("std::rust::String"), LogicalTypeKind::Text);
    EXPECT_EQ(reg.classify("std::rust::str"), LogicalTypeKind::Text);
    EXPECT_EQ(reg.classify("std::rust::char"), LogicalTypeKind::Text);
}

// --- C++ backend ---

TEST_F(TypeRegistryTest, CppContainersClassified) {
    EXPECT_EQ(reg.classify("std::cpp17::vector"), LogicalTypeKind::Sequence);
    EXPECT_EQ(reg.classify("std::cpp17::deque"), LogicalTypeKind::Sequence);
    EXPECT_EQ(reg.classify("std::cpp17::unordered_map"), LogicalTypeKind::Mapping);
    EXPECT_EQ(reg.classify("std::cpp17::map"), LogicalTypeKind::Mapping);
}

// --- Java backend ---

TEST_F(TypeRegistryTest, JavaContainersClassified) {
    EXPECT_EQ(reg.classify("std::java::ArrayList"), LogicalTypeKind::Sequence);
    EXPECT_EQ(reg.classify("std::java::LinkedList"), LogicalTypeKind::Sequence);
    EXPECT_EQ(reg.classify("std::java::HashMap"), LogicalTypeKind::Mapping);
    EXPECT_EQ(reg.classify("std::java::TreeMap"), LogicalTypeKind::Mapping);
}

// --- Cross-backend consistency ---

TEST_F(TypeRegistryTest, CrossBackendConsistency) {
    // Sequence types across all backends
    auto rustVec = reg.classify("std::rust::Vec");
    auto cppVec = reg.classify("std::cpp17::vector");
    auto javaList = reg.classify("std::java::ArrayList");
    ASSERT_TRUE(rustVec.has_value());
    ASSERT_TRUE(cppVec.has_value());
    ASSERT_TRUE(javaList.has_value());
    EXPECT_EQ(*rustVec, *cppVec);
    EXPECT_EQ(*cppVec, *javaList);
    EXPECT_EQ(*rustVec, LogicalTypeKind::Sequence);

    // Mapping types across all backends
    auto rustMap = reg.classify("std::rust::HashMap");
    auto cppMap = reg.classify("std::cpp17::unordered_map");
    auto javaMap = reg.classify("std::java::HashMap");
    ASSERT_TRUE(rustMap.has_value());
    ASSERT_TRUE(cppMap.has_value());
    ASSERT_TRUE(javaMap.has_value());
    EXPECT_EQ(*rustMap, *cppMap);
    EXPECT_EQ(*cppMap, *javaMap);
    EXPECT_EQ(*rustMap, LogicalTypeKind::Mapping);
}

// --- isPrimitive ---

TEST_F(TypeRegistryTest, IsPrimitiveChecks) {
    EXPECT_TRUE(reg.isPrimitive("i32"));
    EXPECT_TRUE(reg.isPrimitive("u64"));
    EXPECT_TRUE(reg.isPrimitive("f64"));
    EXPECT_TRUE(reg.isPrimitive("bool"));
    EXPECT_TRUE(reg.isPrimitive("void"));
    EXPECT_FALSE(reg.isPrimitive("Vec"));
    EXPECT_FALSE(reg.isPrimitive("HashMap"));
    EXPECT_FALSE(reg.isPrimitive("String"));
    EXPECT_FALSE(reg.isPrimitive("Option"));
}

// --- Unknown types ---

TEST_F(TypeRegistryTest, UnknownTypeReturnsNullopt) {
    EXPECT_EQ(reg.classify("std::rust::NonExistent"), std::nullopt);
    EXPECT_EQ(reg.classify("std::cpp17::NonExistent"), std::nullopt);
    EXPECT_EQ(reg.classify("std::unknown::i32"), std::nullopt);
    EXPECT_EQ(reg.classify("not_qualified"), std::nullopt);
    EXPECT_EQ(reg.classify(""), std::nullopt);
}
