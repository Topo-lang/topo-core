#include "topo/Sema/ImportResolver.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace topo {

// --- Portable path helpers ---

static std::string normalizePath(const std::string& path) {
    return fs::path(path).generic_string();
}

static std::string getAbsolutePath(const std::string& path) {
    return normalizePath(fs::absolute(path).string());
}

static std::string getDirectory(const std::string& absPath) {
    return normalizePath(fs::path(absPath).parent_path().string());
}

static std::string readFileContents(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// --- ImportResolver ---

ImportResolver::ImportResolver(DiagnosticEngine& diag) : diag_(diag) {}

std::vector<ResolvedModule> ImportResolver::resolve(const std::vector<std::string>& rootFiles) {
    modules_.clear();
    order_.clear();

    std::unordered_map<std::string, int> state;

    for (const auto& root : rootFiles) {
        std::string absPath = getAbsolutePath(root);
        if (state.find(absPath) == state.end()) {
            discover(absPath, state);
            if (diag_.hasErrors()) return {};
        }
    }

    // Build result in topological order (order_ is post-order: deps first)
    std::vector<ResolvedModule> result;
    for (const auto& path : order_) {
        auto it = modules_.find(path);
        if (it != modules_.end()) {
            result.push_back(std::move(it->second));
        }
    }

    return result;
}

void ImportResolver::discover(const std::string& absPath, std::unordered_map<std::string, int>& state) {
    state[absPath] = 1; // In-progress

    auto ast = parseFile(absPath);
    if (!ast) {
        state[absPath] = 2;
        return;
    }

    // Extract import declarations as ImportDirectives
    std::vector<ImportDirective> directives;
    for (const auto& decl : ast->declarations) {
        if (decl->kind == ASTKind::FileImport) {
            const auto& imp = static_cast<const FileImport&>(*decl);
            std::string importFile = imp.path + ".topo";
            std::string resolved = resolveImportPath(absPath, importFile);
            directives.push_back({resolved, imp.selectedSymbols, imp.location});
        }
    }

    // Recursively discover imports
    for (const auto& directive : directives) {
        auto it = state.find(directive.resolvedPath);
        if (it == state.end()) {
            discover(directive.resolvedPath, state);
            if (diag_.hasErrors()) return;
        } else if (it->second == 1) {
            diag_.error(
                directive.location,
                "circular import detected: '" + absPath + "' and '" + directive.resolvedPath + "' form a cycle");
            return;
        }
    }

    // Store the resolved module
    ResolvedModule mod;
    mod.path = absPath;
    mod.ast = std::move(ast);
    mod.imports = std::move(directives);
    modules_[absPath] = std::move(mod);

    order_.push_back(absPath);
    state[absPath] = 2; // Done
}

std::unique_ptr<TopoFile> ImportResolver::parseFile(const std::string& absPath) {
    std::string source = readFileContents(absPath);
    if (source.empty()) {
        std::ifstream check(absPath);
        if (!check) {
            SourceLocation loc;
            loc.file = absPath;
            diag_.error(loc, "cannot open file '" + absPath + "'");
            return nullptr;
        }
    }

    DiagnosticEngine localDiag;
    Lexer lexer(source, absPath, localDiag);
    Parser parser(lexer, localDiag);
    auto ast = parser.parseTopoFile();

    if (localDiag.hasErrors()) {
        localDiag.print(std::cerr);
        SourceLocation loc;
        loc.file = absPath;
        diag_.error(loc, "parse errors in '" + absPath + "'");
        return nullptr;
    }

    return ast;
}

std::string ImportResolver::resolveImportPath(const std::string& importerAbsPath, const std::string& importFilename) {
    std::string dir = getDirectory(importerAbsPath);
    return normalizePath(dir + "/" + importFilename);
}

} // namespace topo
