#ifndef TOPO_SEMA_IMPORTRESOLVER_H
#define TOPO_SEMA_IMPORTRESOLVER_H

#include "topo/AST/ASTNode.h"
#include "topo/Basic/Diagnostic.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace topo {

struct ImportDirective {
    std::string resolvedPath;                 // Absolute path to .topo file
    std::vector<std::string> selectedSymbols; // Empty = import all
    SourceLocation location;                  // For error reporting
};

struct ResolvedModule {
    std::string path;                     // Absolute file path
    std::unique_ptr<TopoFile> ast;        // Parsed AST (ownership held here)
    std::vector<ImportDirective> imports; // Resolved import directives
};

class ImportResolver {
public:
    explicit ImportResolver(DiagnosticEngine& diag);

    // Resolve all imports starting from the given root files.
    // Returns modules in topological order (dependencies first).
    std::vector<ResolvedModule> resolve(const std::vector<std::string>& rootFiles);

private:
    // DFS with cycle detection; appends to order_ in post-order.
    // state: 0=unvisited, 1=in-progress, 2=done
    void discover(const std::string& absPath, std::unordered_map<std::string, int>& state);

    // Parse a single .topo file.
    std::unique_ptr<TopoFile> parseFile(const std::string& absPath);

    // Resolve an import filename relative to the importing file's directory.
    std::string resolveImportPath(const std::string& importerAbsPath, const std::string& importFilename);

    DiagnosticEngine& diag_;
    std::unordered_map<std::string, ResolvedModule> modules_;
    std::vector<std::string> order_; // Post-order (dependencies first)
};

} // namespace topo

#endif // TOPO_SEMA_IMPORTRESOLVER_H
