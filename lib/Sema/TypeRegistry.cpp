#include "topo/Sema/TypeRegistry.h"

namespace topo {

void TypeRegistry::registerBackend(const std::string& nsPrefix, std::vector<ConcreteTypeBinding> bindings) {
    auto& map = backends_[nsPrefix];
    for (auto& b : bindings) {
        map[b.concreteName] = std::move(b);
    }
}

bool TypeRegistry::isKnownType(const std::string& nsPrefix, const std::string& name) const {
    auto it = backends_.find(nsPrefix);
    if (it == backends_.end()) return false;
    return it->second.count(name) > 0;
}

std::optional<LogicalTypeKind> TypeRegistry::classify(const std::string& qualifiedName) const {
    // Expected format: std::<backend>::<typeName>
    // Parse: find first "::" after "std"
    if (qualifiedName.size() < 5 || qualifiedName.substr(0, 5) != "std::") {
        return std::nullopt;
    }
    auto secondColon = qualifiedName.find("::", 5);
    if (secondColon == std::string::npos) return std::nullopt;

    std::string nsPrefix = qualifiedName.substr(5, secondColon - 5);
    std::string typeName = qualifiedName.substr(secondColon + 2);

    auto backendIt = backends_.find(nsPrefix);
    if (backendIt == backends_.end()) return std::nullopt;

    auto typeIt = backendIt->second.find(typeName);
    if (typeIt == backendIt->second.end()) return std::nullopt;

    return typeIt->second.logicalKind;
}

bool TypeRegistry::isPrimitive(const std::string& name) const {
    // A name is primitive if it maps to Integer, UnsignedInteger,
    // Floating, or Boolean in any backend, or is "void".
    if (name == "void") return true;
    for (const auto& [ns, types] : backends_) {
        auto it = types.find(name);
        if (it != types.end()) {
            auto k = it->second.logicalKind;
            if (k == LogicalTypeKind::Integer || k == LogicalTypeKind::UnsignedInteger ||
                k == LogicalTypeKind::Floating || k == LogicalTypeKind::Boolean) {
                return true;
            }
        }
    }
    return false;
}

std::optional<LogicalTypeKind> TypeRegistry::classifyAbstractName(const std::string& name) {
    if (name == "integer") return LogicalTypeKind::Integer;
    if (name == "unsigned") return LogicalTypeKind::UnsignedInteger;
    if (name == "floating") return LogicalTypeKind::Floating;
    if (name == "boolean") return LogicalTypeKind::Boolean;
    if (name == "text") return LogicalTypeKind::Text;
    return std::nullopt;
}

TypeRegistry TypeRegistry::createDefault() {
    TypeRegistry reg;

    // C++17 backend
    reg.registerBackend("cpp17",
                        {
                            {"int8_t", LogicalTypeKind::Integer},
                            {"int16_t", LogicalTypeKind::Integer},
                            {"int32_t", LogicalTypeKind::Integer},
                            {"int64_t", LogicalTypeKind::Integer},
                            {"uint8_t", LogicalTypeKind::UnsignedInteger},
                            {"uint16_t", LogicalTypeKind::UnsignedInteger},
                            {"uint32_t", LogicalTypeKind::UnsignedInteger},
                            {"uint64_t", LogicalTypeKind::UnsignedInteger},
                            {"size_t", LogicalTypeKind::UnsignedInteger},
                            {"intptr_t", LogicalTypeKind::Integer},
                            {"uintptr_t", LogicalTypeKind::UnsignedInteger},
                            {"ptrdiff_t", LogicalTypeKind::Integer},
                            {"char", LogicalTypeKind::Text},
                            {"char16_t", LogicalTypeKind::Text},
                            {"char32_t", LogicalTypeKind::Text},
                            {"wchar_t", LogicalTypeKind::Text},
                            {"float", LogicalTypeKind::Floating},
                            {"double", LogicalTypeKind::Floating},
                            {"long_double", LogicalTypeKind::Floating},
                            {"int", LogicalTypeKind::Integer},
                            {"bool", LogicalTypeKind::Boolean},
                            // Text types
                            {"string", LogicalTypeKind::Text},
                            {"wstring", LogicalTypeKind::Text},
                            // Sequence types
                            {"vector", LogicalTypeKind::Sequence},
                            {"deque", LogicalTypeKind::Sequence},
                            {"list", LogicalTypeKind::Sequence},
                            {"array", LogicalTypeKind::Sequence},
                            {"span", LogicalTypeKind::Sequence},
                            // Mapping types
                            {"map", LogicalTypeKind::Mapping},
                            {"unordered_map", LogicalTypeKind::Mapping},
                            // Set types
                            {"set", LogicalTypeKind::Sequence},
                            {"unordered_set", LogicalTypeKind::Sequence},
                            // Optional
                            {"optional", LogicalTypeKind::Opaque},
                            // Smart pointers
                            {"unique_ptr", LogicalTypeKind::Opaque},
                            {"shared_ptr", LogicalTypeKind::Opaque},
                            {"weak_ptr", LogicalTypeKind::Opaque},
                        });

    // Rust backend
    reg.registerBackend("rust",
                        {
                            // Primitives — integers
                            {"i8", LogicalTypeKind::Integer},
                            {"i16", LogicalTypeKind::Integer},
                            {"i32", LogicalTypeKind::Integer},
                            {"i64", LogicalTypeKind::Integer},
                            {"i128", LogicalTypeKind::Integer},
                            {"isize", LogicalTypeKind::Integer},
                            {"u8", LogicalTypeKind::UnsignedInteger},
                            {"u16", LogicalTypeKind::UnsignedInteger},
                            {"u32", LogicalTypeKind::UnsignedInteger},
                            {"u64", LogicalTypeKind::UnsignedInteger},
                            {"u128", LogicalTypeKind::UnsignedInteger},
                            {"usize", LogicalTypeKind::UnsignedInteger},
                            // Primitives — floating point
                            {"f32", LogicalTypeKind::Floating},
                            {"f64", LogicalTypeKind::Floating},
                            // Primitives — boolean and char
                            {"bool", LogicalTypeKind::Boolean},
                            {"char", LogicalTypeKind::Text},
                            // Text types
                            {"str", LogicalTypeKind::Text},
                            {"String", LogicalTypeKind::Text},
                            // Sequence types
                            {"Vec", LogicalTypeKind::Sequence},
                            {"VecDeque", LogicalTypeKind::Sequence},
                            {"LinkedList", LogicalTypeKind::Sequence},
                            {"Slice", LogicalTypeKind::Sequence},
                            // Mapping types
                            {"HashMap", LogicalTypeKind::Mapping},
                            {"BTreeMap", LogicalTypeKind::Mapping},
                            // Set types
                            {"HashSet", LogicalTypeKind::Sequence},
                            {"BTreeSet", LogicalTypeKind::Sequence},
                            // Optional / Result
                            {"Option", LogicalTypeKind::Opaque},
                            {"Result", LogicalTypeKind::Opaque},
                            // Smart pointers (ownership mapped at declaration level)
                            {"Box", LogicalTypeKind::Opaque},
                            {"Rc", LogicalTypeKind::Opaque},
                            {"Arc", LogicalTypeKind::Opaque},
                            {"Weak", LogicalTypeKind::Opaque},
                        });

    // Java backend
    reg.registerBackend("java",
                        {
                            {"byte", LogicalTypeKind::Integer},
                            {"short", LogicalTypeKind::Integer},
                            {"int", LogicalTypeKind::Integer},
                            {"long", LogicalTypeKind::Integer},
                            {"float", LogicalTypeKind::Floating},
                            {"double", LogicalTypeKind::Floating},
                            {"boolean", LogicalTypeKind::Boolean},
                            {"char", LogicalTypeKind::Text},
                            {"String", LogicalTypeKind::Text},
                            // Sequence types
                            {"ArrayList", LogicalTypeKind::Sequence},
                            {"LinkedList", LogicalTypeKind::Sequence},
                            {"ArrayDeque", LogicalTypeKind::Sequence},
                            // Mapping types
                            {"HashMap", LogicalTypeKind::Mapping},
                            {"TreeMap", LogicalTypeKind::Mapping},
                            // Set types
                            {"HashSet", LogicalTypeKind::Sequence},
                            {"TreeSet", LogicalTypeKind::Sequence},
                            // Optional
                            {"Optional", LogicalTypeKind::Opaque},
                            // Weak reference
                            {"WeakReference", LogicalTypeKind::Opaque},
                        });

    return reg;
}

} // namespace topo
