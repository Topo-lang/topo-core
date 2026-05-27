#ifndef TOPO_SEMA_TYPEBINDER_H
#define TOPO_SEMA_TYPEBINDER_H

#include "topo/Basic/HostLanguage.h"
#include "topo/Sema/TypeRegistry.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace topo {

/// Maps abstract type names (e.g. "integer") to concrete host-language
/// types (e.g. "int32_t" for C++, "i32" for Rust). Users can override
/// defaults via Topo.toml [types] section.
class TypeBinder {
public:
    /// Resolve an abstract type name to its concrete binding.
    /// Returns nullopt if the name is not an abstract type.
    std::optional<std::string> resolve(const std::string& abstractName) const;

    /// Add or override a binding for the given logical kind.
    void addBinding(LogicalTypeKind kind, const std::string& concreteType);

    /// Add or override a binding by abstract name (e.g. "integer" → "int64_t").
    /// Returns false if the name is not a recognized abstract type.
    bool addBindingByName(const std::string& abstractName, const std::string& concreteType);

    /// Create a TypeBinder with default bindings for the given host language.
    static TypeBinder createDefault(HostLanguage lang);

private:
    std::unordered_map<LogicalTypeKind, std::string> bindings_;
};

} // namespace topo

#endif // TOPO_SEMA_TYPEBINDER_H
