#ifndef TOPO_CHECK_SYMBOLEXTRACTOR_H
#define TOPO_CHECK_SYMBOLEXTRACTOR_H

#include "topo/AST/ASTNode.h"

#include <optional>
#include <string>
#include <vector>

namespace topo::check {

enum class HostSymbolKind {
    Function,
    Method,
    Constructor,
    Destructor,
    StaticMethod,
    Class,
    Struct,
    Enum,
    // Type-shape declarations beyond classes/structs. Added so language
    // extractors that distinguish them at the source level (TypeScript
    // `interface`, `type X = ...`) do not have to collapse onto
    // `Class`. CompletenessCheck currently routes Interface / TypeAlias
    // through the type-symbol lookup path alongside Class / Struct /
    // Enum, so a `.topo` `class Foo` declaration matches a host
    // `interface Foo` symbol — the .topo author still uses `class` in
    // declarations, but the host extractor is honest about the source
    // shape.
    Interface,
    TypeAlias,
    // Value-shape exports beyond functions / methods. TypeScript
    // `export const X` lifts to Variable instead of the previous
    // `Function` scaffold-fallback.
    Variable,
    // Namespace-like scope containers (TypeScript `export namespace`,
    // future use). Not emitted as a host symbol by current extractors —
    // the scope is recorded only as a qualifiedName prefix — but the
    // tag is reserved here so future extensions don't reintroduce the
    // lossy mapping.
    Namespace,
};

struct HostSymbol {
    std::string qualifiedName; // "engine::core::init"
    std::string simpleName;    // "init"
    HostSymbolKind kind;
    std::string file;
    int line = 0;
    std::string returnType;
    std::vector<std::string> paramTypes;
    bool isConst = false;
    bool isStatic = false;
    std::optional<Visibility> hostVisibility; // source-level access modifier
    std::string enclosingClass;               // non-empty for class methods
};

/// Abstract interface for extracting symbols from host language source files.
/// Each language SDK provides a concrete implementation.
class SymbolExtractor {
public:
    virtual ~SymbolExtractor() = default;

    /// Extract symbols from a single source file.
    virtual std::vector<HostSymbol> extractSymbols(const std::string& filePath) = 0;

    /// Extract symbols from multiple source files.
    std::vector<HostSymbol> extractAll(const std::vector<std::string>& sourceFiles);
};

} // namespace topo::check

#endif // TOPO_CHECK_SYMBOLEXTRACTOR_H
