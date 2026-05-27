#include "topo/Sema/TypeResolver.h"
#include "topo/Stdlib/Types.h"

#include <set>

namespace topo {

TypeResolver::TypeResolver(const SymbolTable& symbols) : symbols_(symbols), registry_(TypeRegistry::createDefault()) {}

TypeResolver::TypeResolver(const SymbolTable& symbols, const TypeRegistry& registry)
    : symbols_(symbols), registry_(registry) {}

void TypeResolver::addTemplateParamNames(const std::vector<TemplateParamDecl>& params) {
    for (const auto& p : params) {
        if (p.kind == TemplateParamDecl::TypeParam || p.kind == TemplateParamDecl::TemplateTemplateParam) {
            templateParamNames_.insert(p.name);
        }
    }
}

void TypeResolver::clearTemplateParamNames() {
    templateParamNames_.clear();
}

bool TypeResolver::isValidType(const TypeNode& type, std::string& reason) const {
    if (type.nameParts.empty()) {
        reason = "empty type";
        return false;
    }

    // 1. Built-in types: single-element void only
    if (type.nameParts.size() == 1) {
        const auto& name = type.nameParts[0];
        if (name == "void") {
            return true;
        }

        // 1b. Stdlib bridging types. The parser sets
        // `stdlibId` for these; defensively fall back to the keyword map
        // when an AST is constructed without that field set (tests).
        stdlib::TypeId stdId =
            (type.stdlibId != stdlib::TypeId::None) ? type.stdlibId : stdlib::fromKeyword(name);
        if (stdId == stdlib::TypeId::Record) {
            // record<...> validity: every field type must itself be a
            // recognized stdlib type (recurse, so nested optional/slice/
            // record compose), and field names must be unique. A bare user
            // type as a field is rejected here because the cross-language
            // byte contract is only defined over stdlib types.
            if (type.recordFields.empty()) {
                reason = "record<...> must declare at least one field";
                return false;
            }
            std::set<std::string> seenNames;
            for (const auto& f : type.recordFields) {
                if (!seenNames.insert(f.name).second) {
                    reason = std::string("duplicate field name '") + f.name + "' in record<...>";
                    return false;
                }
                std::string sub;
                if (!isValidType(f.type(), sub)) {
                    reason = std::string("invalid type of record field '") + f.name + "': " + sub;
                    return false;
                }
            }
            return true;
        }

        if (stdId == stdlib::TypeId::Array) {
            // array<T, N>: exactly one element type plus one compile-time
            // integer N >= 1. T recurses (nested stdlib types compose, same
            // as slice/record); a bare user type is rejected because the
            // cross-language byte contract is only defined over stdlib
            // types. N is carried as the second arg's nonTypeValue.
            if (type.templateArgs.size() != 2) {
                reason = std::string("stdlib type 'array' takes 2 arguments (array<T, N>), got ") +
                         std::to_string(type.templateArgs.size());
                return false;
            }
            const TypeNode& elem = type.templateArgs[0];
            const TypeNode& count = type.templateArgs[1];
            if (elem.nonTypeValue.has_value()) {
                reason = "array<T, N>: first argument must be a type, not an integer";
                return false;
            }
            std::string sub;
            if (!isValidType(elem, sub)) {
                reason = std::string("invalid element type of array<T, N>: ") + sub;
                return false;
            }
            if (!count.nonTypeValue.has_value()) {
                reason = "array<T, N>: N must be an integer literal";
                return false;
            }
            if (*count.nonTypeValue < 1) {
                reason = std::string("array<T, N>: N must be >= 1, got ") +
                         std::to_string(*count.nonTypeValue);
                return false;
            }
            return true;
        }

        if (stdId == stdlib::TypeId::Union) {
            // union<tag: TagT, v1: T1, v2: T2, ...> validity. The field
            // list reuses record's named-field AST vector, so the parse
            // shape is shared; the union-specific rules live here:
            //   - at least 2 fields: one tag + >=1 variant
            //   - the tag (first field) must be an integer scalar so the
            //     discriminant has a well-defined cross-language encoding
            //   - field names unique (a duplicate makes the variant the
            //     discriminant selects ambiguous)
            //   - every field type is itself a recognized stdlib type
            //     (recurse, so nested optional/slice/record/union compose)
            if (type.recordFields.size() < 2) {
                reason = "union<...> must declare a tag field and at least one variant";
                return false;
            }
            // Integer scalars valid as a discriminant tag. bool is included:
            // it is a 1-byte 0/1 integer at the boundary, usable as a
            // two-state tag. Floats are excluded — a non-integral
            // discriminant has no well-defined cross-language encoding.
            auto isIntegerScalarTag = [](stdlib::TypeId id) {
                switch (id) {
                case stdlib::TypeId::Bool:
                case stdlib::TypeId::U8:
                case stdlib::TypeId::I8:
                case stdlib::TypeId::I16:
                case stdlib::TypeId::U16:
                case stdlib::TypeId::I32:
                case stdlib::TypeId::U32:
                case stdlib::TypeId::U64:
                case stdlib::TypeId::I64:
                    return true;
                default:
                    return false;
                }
            };
            const auto& tagField = type.recordFields.front();
            if (!isIntegerScalarTag(tagField.type().stdlibId)) {
                reason = std::string("union<...> tag field '") + tagField.name +
                         "' must be an integer scalar type";
                return false;
            }
            std::set<std::string> seenNames;
            for (const auto& f : type.recordFields) {
                if (!seenNames.insert(f.name).second) {
                    reason = std::string("duplicate field name '") + f.name + "' in union<...>";
                    return false;
                }
                std::string sub;
                if (!isValidType(f.type(), sub)) {
                    reason = std::string("invalid type of union field '") + f.name + "': " + sub;
                    return false;
                }
            }
            return true;
        }

        if (stdId != stdlib::TypeId::None) {
            const unsigned expected = stdlib::typeParamArity(stdId);
            const unsigned actual = static_cast<unsigned>(type.templateArgs.size());
            if (expected != actual) {
                reason = std::string("stdlib type '") + name + "' takes " + std::to_string(expected) +
                         " type parameter(s), got " + std::to_string(actual);
                return false;
            }
            // Recurse into the parameterised T of optional<T> / slice<T>.
            for (const auto& arg : type.templateArgs) {
                std::string sub;
                if (!isValidType(arg, sub)) {
                    reason = std::string("invalid type parameter of '") + name + "': " + sub;
                    return false;
                }
            }
            return true;
        }

        // 2. Check if it's a template parameter name
        if (templateParamNames_.count(name)) {
            return true;
        }

        // 3. Check if it's a using alias
        if (symbols_.findTypeAlias(name) != nullptr) {
            return true;
        }

        // 4. Check if it's an imported type via std::import
        if (symbols_.isImportedType(name)) {
            return true;
        }

        // 5. Check if it's a declared class name
        for (const auto& [qname, cls] : symbols_.classSymbols()) {
            if (cls.simpleName == name) {
                return true;
            }
        }

        // 6. Check if it's a constraint name (used in constraint references)
        for (const auto& [qname, cs] : symbols_.constraintSymbols()) {
            if (cs.simpleName == name) {
                return true;
            }
        }

        // 7. Check if it's an abstract type name (integer, floating, etc.)
        if (TypeRegistry::classifyAbstractName(name)) {
            return true;
        }

        // 8. Built-in generic type: lifetime<scope, T>
        if (name == "lifetime") {
            return true;
        }

        // Unknown single-element type
        reason = "unknown type '" + name + "'";
        return false;
    }

    // 2. std::<backend>::<typeName> — delegate to TypeRegistry
    if (type.nameParts.size() == 3 && type.nameParts[0] == "std") {
        const auto& nsPrefix = type.nameParts[1];
        const auto& typeName = type.nameParts[2];
        if (registry_.isKnownType(nsPrefix, typeName)) {
            return true;
        }
        reason = "unknown std::" + nsPrefix + " type '" + typeName + "'";
        return false;
    }

    // Multi-part type that isn't std::<backend>::X — treat as
    // user type (opaque). Accept any multi-part qualified type as a
    // potentially valid external type reference (e.g. Config, etc.)
    return true;
}

} // namespace topo
