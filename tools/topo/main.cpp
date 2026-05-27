#include "topo/AST/ASTPrinter.h"
#include "topo/Basic/Diagnostic.h"
#include "topo/Analysis/ImportPathCheck.h"
#include "topo/Distribution/BackendCli.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Sema/ImportResolver.h"
#include "topo/Sema/SemanticAnalyzer.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

static void printUsage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <mode> <file.topo> [options]\n"
              << "       " << argv0 << " backend <subcommand> [args]\n"
              << "\nModes:\n"
              << "  --tokens     Dump token stream\n"
              << "  --ast-dump   Dump AST tree\n"
              << "  --check      Semantic analysis (parse + validate)\n"
              << "  --merge      Merge imports into single .topo\n"
              << "\nSubcommands:\n"
              << "  backend      Manage on-demand backend installs (run "
                 "'topo backend --help')\n"
              << "\nOptions:\n"
              << "  -o <output>  Output file (--merge only; default: stdout)\n"
              << "  --check      With --merge: run semantic analysis after merge\n"
              << "\nExamples:\n"
              << "  topo --check myproject.topo              Check syntax and semantics\n"
              << "  topo --tokens myproject.topo             Dump token stream\n"
              << "  topo --merge root.topo -o merged.topo    Merge imports into single file\n"
              << "  topo backend list                        List installed backends\n";
}

static std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "error: cannot open file '" << path << "'\n";
        return "";
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Per-file semantic analysis with import resolution.
// Returns combined SymbolTable; prints errors to stderr. Returns empty table on failure.
static std::pair<topo::SymbolTable, bool> analyzeWithImports(const std::string& filepath) {
    topo::DiagnosticEngine diag;
    topo::ImportResolver resolver(diag);
    auto modules = resolver.resolve({filepath});

    if (diag.hasErrors()) {
        diag.print(std::cerr);
        return {{}, false};
    }

    std::unordered_map<std::string, topo::SymbolTable> symbolCache;

    for (const auto& mod : modules) {
        topo::SymbolTable importedSymbols;
        for (const auto& directive : mod.imports) {
            auto it = symbolCache.find(directive.resolvedPath);
            if (it != symbolCache.end()) {
                auto unresolved = importedSymbols.mergeSelected(it->second, directive.selectedSymbols);
                for (const auto& name : unresolved) {
                    diag.error(directive.location,
                               "symbol '" + name + "' not found in '" + directive.resolvedPath + "'");
                }
            }
        }

        topo::DiagnosticEngine fileDiag;
        topo::SemanticAnalyzer sema(fileDiag);
        auto fileSymbols = sema.analyze(static_cast<const topo::TopoFile&>(*mod.ast), importedSymbols);

        if (fileDiag.hasErrors()) {
            fileDiag.print(std::cerr);
            return {{}, false};
        }
        symbolCache[mod.path] = std::move(fileSymbols);
    }

    if (diag.hasErrors()) {
        diag.print(std::cerr);
        return {{}, false};
    }

    // Build combined symbol table
    topo::SymbolTable combined;
    for (const auto& mod : modules) {
        auto it = symbolCache.find(mod.path);
        if (it != symbolCache.end()) {
            combined.mergeFrom(it->second, /*filterInternal=*/true);
        }
    }

    return {std::move(combined), true};
}

static int doMerge(const std::string& filepath, int argc, char* argv[]) {
    // Parse extra options: -o and --check
    std::string outputPath;
    bool alsoCheck = false;

    for (int i = 3; i < argc; ++i) {
        if (std::string(argv[i]) == "-o" && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (std::string(argv[i]) == "--check") {
            alsoCheck = true;
        } else {
            std::cerr << "error: unknown option '" << argv[i] << "'\n";
            return 1;
        }
    }

    // Resolve imports from root file
    topo::DiagnosticEngine diag;
    topo::ImportResolver resolver(diag);
    auto modules = resolver.resolve({filepath});

    if (diag.hasErrors()) {
        diag.print(std::cerr);
        return 1;
    }

    // Output: concatenate sources with file attribution (for text output)
    std::ostringstream merged;
    merged << "// Merged from " << modules.size() << " module(s)\n\n";
    for (const auto& mod : modules) {
        std::string source = readFile(mod.path);
        if (source.empty()) continue;
        merged << "// --- " << mod.path << " ---\n";
        merged << source;
        merged << "\n";
    }
    std::string mergedSource = merged.str();

    if (outputPath.empty()) {
        std::cout << mergedSource;
    } else {
        std::ofstream out(outputPath);
        if (!out) {
            std::cerr << "error: cannot open output file '" << outputPath << "'\n";
            return 1;
        }
        out << mergedSource;
        std::cerr << "Merged " << modules.size() << " module(s) → " << outputPath << "\n";
    }

    // Optional: run per-file semantic analysis
    if (alsoCheck) {
        auto [symbols, ok] = analyzeWithImports(filepath);
        if (!ok) return 1;

        std::cerr << "Semantic analysis passed.\n"
                  << "  Functions:    " << symbols.functions().size() << "\n"
                  << "  Type aliases: " << symbols.typeAliases().size() << "\n"
                  << "  Logic blocks: " << symbols.logicBlocks().size() << "\n";
    }

    return 0;
}

int main(int argc, char* argv[]) {
    // `topo backend ...` — on-demand backend distribution CLI. Handled before
    // the .topo file-mode logic because it has its own argument grammar.
    if (argc >= 2 && std::string(argv[1]) == "backend") {
        std::vector<std::string> backendArgs;
        for (int i = 2; i < argc; ++i) backendArgs.emplace_back(argv[i]);
        return topo::dist::runBackendCli(backendArgs);
    }

    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    std::string mode = argv[1];
    std::string filepath = argv[2];

    if (mode != "--tokens" && mode != "--ast-dump" && mode != "--check" && mode != "--merge") {
        std::cerr << "error: unknown mode '" << mode << "'\n";
        printUsage(argv[0]);
        return 1;
    }

    if (mode == "--merge") {
        return doMerge(filepath, argc, argv);
    }

    if (mode == "--check") {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto [symbols, ok] = analyzeWithImports(filepath);
        auto t1 = std::chrono::high_resolution_clock::now();

        if (!ok) return 1;

        auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        std::cout << "Semantic analysis passed.\n"
                  << "  Functions:    " << symbols.functions().size() << "\n"
                  << "  Type aliases: " << symbols.typeAliases().size() << "\n"
                  << "  Logic blocks: " << symbols.logicBlocks().size() << "\n"
                  << "  Classes:      " << symbols.classSymbols().size() << "\n"
                  << "  Constraints:  " << symbols.constraintSymbols().size() << "\n"
                  << "  Timing:       " << totalUs << " us\n";

        // Validate std::import paths (warning-only without full project context)
        {
            topo::analysis::ImportPathConfig importCfg;
            // Use .topo file's directory as base
            namespace fs = std::filesystem;
            importCfg.projectDir = fs::path(filepath).parent_path().string();
            importCfg.warnOnly = true;

            topo::check::CheckResult importResult;
            topo::analysis::checkImportPaths(symbols, importCfg, importResult);

            for (const auto& diag : importResult.diagnostics) {
                if (diag.severity == topo::check::Severity::Warning) {
                    std::cerr << "warning: " << diag.message;
                    if (!diag.file.empty()) {
                        std::cerr << " [" << diag.file;
                        if (diag.line > 0) std::cerr << ":" << diag.line;
                        std::cerr << "]";
                    }
                    std::cerr << "\n";
                }
            }
        }

        return 0;
    }

    std::string source = readFile(filepath);
    if (source.empty() && filepath != "") {
        return 1;
    }

    topo::DiagnosticEngine diag;

    if (mode == "--tokens") {
        topo::Lexer lexer(source, filepath, diag);
        while (true) {
            topo::Token tok = lexer.nextToken();
            std::cout << tok << "\n";
            if (tok.is(topo::TokenKind::Eof)) break;
        }
    } else if (mode == "--ast-dump") {
        topo::Lexer lexer(source, filepath, diag);
        topo::Parser parser(lexer, diag);
        auto ast = parser.parseTopoFile();

        if (diag.hasErrors()) {
            diag.print(std::cerr);
            return 1;
        }

        topo::ASTPrinter printer(std::cout);
        printer.print(*ast);
    }

    if (diag.hasErrors()) {
        diag.print(std::cerr);
        return 1;
    }

    return 0;
}
