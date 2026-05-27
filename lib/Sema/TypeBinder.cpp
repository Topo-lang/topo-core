#include "topo/Sema/TypeBinder.h"

namespace topo {

std::optional<std::string> TypeBinder::resolve(const std::string& abstractName) const {
    auto kind = TypeRegistry::classifyAbstractName(abstractName);
    if (!kind) return std::nullopt;

    auto it = bindings_.find(*kind);
    if (it == bindings_.end()) return std::nullopt;
    return it->second;
}

void TypeBinder::addBinding(LogicalTypeKind kind, const std::string& concreteType) {
    bindings_[kind] = concreteType;
}

bool TypeBinder::addBindingByName(const std::string& abstractName, const std::string& concreteType) {
    auto kind = TypeRegistry::classifyAbstractName(abstractName);
    if (!kind) return false;
    bindings_[*kind] = concreteType;
    return true;
}

TypeBinder TypeBinder::createDefault(HostLanguage lang) {
    TypeBinder binder;
    if (lang == HostLanguage::Cpp) {
        binder.addBinding(LogicalTypeKind::Integer, "int32_t");
        binder.addBinding(LogicalTypeKind::UnsignedInteger, "uint32_t");
        binder.addBinding(LogicalTypeKind::Floating, "double");
        binder.addBinding(LogicalTypeKind::Boolean, "bool");
        binder.addBinding(LogicalTypeKind::Text, "std::string");
    } else if (lang == HostLanguage::Rust) {
        binder.addBinding(LogicalTypeKind::Integer, "i32");
        binder.addBinding(LogicalTypeKind::UnsignedInteger, "u32");
        binder.addBinding(LogicalTypeKind::Floating, "f64");
        binder.addBinding(LogicalTypeKind::Boolean, "bool");
        binder.addBinding(LogicalTypeKind::Text, "String");
    } else if (lang == HostLanguage::Java) {
        binder.addBinding(LogicalTypeKind::Integer, "int");
        binder.addBinding(LogicalTypeKind::UnsignedInteger, "int");
        binder.addBinding(LogicalTypeKind::Floating, "double");
        binder.addBinding(LogicalTypeKind::Boolean, "boolean");
        binder.addBinding(LogicalTypeKind::Text, "String");
    } else if (lang == HostLanguage::TypeScript) {
        binder.addBinding(LogicalTypeKind::Integer, "number");
        binder.addBinding(LogicalTypeKind::UnsignedInteger, "number");
        binder.addBinding(LogicalTypeKind::Floating, "number");
        binder.addBinding(LogicalTypeKind::Boolean, "boolean");
        binder.addBinding(LogicalTypeKind::Text, "string");
    } else {
        // Python
        binder.addBinding(LogicalTypeKind::Integer, "int");
        binder.addBinding(LogicalTypeKind::UnsignedInteger, "int");
        binder.addBinding(LogicalTypeKind::Floating, "float");
        binder.addBinding(LogicalTypeKind::Boolean, "bool");
        binder.addBinding(LogicalTypeKind::Text, "str");
    }
    return binder;
}

} // namespace topo
