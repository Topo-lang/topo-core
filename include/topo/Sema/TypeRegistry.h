#ifndef TOPO_SEMA_TYPEREGISTRY_H
#define TOPO_SEMA_TYPEREGISTRY_H

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace topo {

enum class LogicalTypeKind { Integer, UnsignedInteger, Floating, Boolean, Text, Sequence, Mapping, Void, Opaque };

struct ConcreteTypeBinding {
    std::string concreteName;
    LogicalTypeKind logicalKind;
};

class TypeRegistry {
public:
    // Register a backend namespace with its type bindings.
    // nsPrefix is like "cpp17" or "rust".
    void registerBackend(const std::string& nsPrefix, std::vector<ConcreteTypeBinding> bindings);

    // Check if a type name is known under a given namespace prefix.
    bool isKnownType(const std::string& nsPrefix, const std::string& name) const;

    // Classify a qualified type name (e.g., "std::cpp17::int32_t")
    // into its logical kind.
    std::optional<LogicalTypeKind> classify(const std::string& qualifiedName) const;

    // Check if a simple name is a primitive type in any backend.
    bool isPrimitive(const std::string& name) const;

    // Classify an abstract type name (e.g. "integer", "floating") into
    // its logical kind. Returns nullopt if the name is not abstract.
    static std::optional<LogicalTypeKind> classifyAbstractName(const std::string& name);

    // Create a registry pre-populated with cpp17 and rust bindings.
    static TypeRegistry createDefault();

private:
    // nsPrefix -> (concreteName -> binding)
    std::unordered_map<std::string, std::unordered_map<std::string, ConcreteTypeBinding>> backends_;
};

} // namespace topo

#endif // TOPO_SEMA_TYPEREGISTRY_H
