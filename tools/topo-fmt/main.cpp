#include "topo/Basic/Diagnostic.h"
#include "topo/Format/Formatter.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Sema/SemanticAnalyzer.h"
#include "topo/Transform/SemanticVerifier.h"
#include "topo/Transform/TokenStreamRewriter.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void printUsage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [--check|--fix|--diff] <file.topo>\n"
              << "\nModes:\n"
              << "  --check   Report what would be transformed (exit 1 if changes needed)\n"
              << "  --fix     Transform in-place (overwrite file with canonical syntax)\n"
              << "  --diff    Show unified diff of changes\n"
              << "  (default) --diff if changes found, --check if no changes\n"
              << "\nExamples:\n"
              << "  topo-fmt --check myproject.topo    Report needed transforms\n"
              << "  topo-fmt --fix myproject.topo      Normalize syntax in-place\n"
              << "  topo-fmt --diff myproject.topo     Show diff of changes\n";
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

static bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file) {
        std::cerr << "error: cannot write file '" << path << "'\n";
        return false;
    }
    file << content;
    return true;
}

// Simple line-by-line diff output
static void printDiff(const std::string& filepath, const std::string& original, const std::string& formatted) {
    std::istringstream origStream(original);
    std::istringstream fmtStream(formatted);
    std::string origLine, fmtLine;
    std::vector<std::string> origLines, fmtLines;
    while (std::getline(origStream, origLine))
        origLines.push_back(origLine);
    while (std::getline(fmtStream, fmtLine))
        fmtLines.push_back(fmtLine);

    std::cout << "--- " << filepath << " (original)\n";
    std::cout << "+++ " << filepath << " (canonical)\n";

    size_t maxLines = std::max(origLines.size(), fmtLines.size());
    for (size_t i = 0; i < maxLines; ++i) {
        const std::string& ol = (i < origLines.size()) ? origLines[i] : "";
        const std::string& fl = (i < fmtLines.size()) ? fmtLines[i] : "";
        if (ol != fl) {
            if (i < origLines.size()) {
                std::cout << "-" << ol << "\n";
            }
            if (i < fmtLines.size()) {
                std::cout << "+" << fl << "\n";
            }
        } else {
            std::cout << " " << ol << "\n";
        }
    }
}

enum class Mode { Check, Fix, Diff, Default };

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    Mode mode = Mode::Default;
    std::string filepath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--check") {
            mode = Mode::Check;
        } else if (arg == "--fix") {
            mode = Mode::Fix;
        } else if (arg == "--diff") {
            mode = Mode::Diff;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg[0] == '-') {
            std::cerr << "error: unknown option '" << arg << "'\n";
            printUsage(argv[0]);
            return 1;
        } else {
            filepath = arg;
        }
    }

    if (filepath.empty()) {
        std::cerr << "error: no input file specified\n";
        printUsage(argv[0]);
        return 1;
    }

    std::string source = readFile(filepath);
    if (source.empty() && !std::filesystem::exists(filepath)) {
        return 1;
    }

    // Step 1: Lex in lenient mode with comment preservation
    topo::DiagnosticEngine diag;
    topo::Lexer lexer(source, filepath, diag);
    lexer.setLenientMode(true);
    lexer.setPreserveComments(true);

    // Step 2: Multi-token structural normalization
    topo::transform::TokenStreamRewriter rewriter(lexer);
    auto rewrittenTokens = rewriter.rewrite();
    lexer.setTokenBuffer(std::move(rewrittenTokens));

    // Step 3: Parse to AST (reads from token buffer)
    topo::Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();

    if (diag.hasErrors()) {
        diag.print(std::cerr);
        return 1;
    }

    const auto& lexerRecords = lexer.transformRecords();
    const auto& rewriterRecords = rewriter.transformRecords();

    // Step 4: If no transforms needed, report and exit
    if (lexerRecords.empty() && rewriterRecords.empty()) {
        std::cerr << filepath << ": already canonical\n";
        return 0;
    }

    // Step 5: Format the AST to produce canonical output
    topo::format::Formatter formatter;
    std::string formatted = formatter.format(static_cast<const topo::TopoFile&>(*ast), source, lexer.comments());

    // Print transform summary to stderr
    for (const auto& rec : lexerRecords) {
        std::cerr << filepath << ":" << rec.location.line << ":" << rec.location.column << ": " << rec.originalText
                  << " -> " << rec.canonicalText << "\n";
    }
    for (const auto& rec : rewriterRecords) {
        std::cerr << filepath << ":" << rec.location.line << ":" << rec.location.column << ": " << rec.originalText
                  << " -> " << rec.canonicalText << "\n";
    }

    if (mode == Mode::Default) {
        mode = Mode::Diff;
    }

    size_t totalRecords = lexerRecords.size() + rewriterRecords.size();

    switch (mode) {
    case Mode::Check: std::cerr << totalRecords << " transform(s) needed\n"; return 1;

    case Mode::Diff: printDiff(filepath, source, formatted); return 1;

    case Mode::Fix: {
        // Semantic verification: ensure transformation preserves structure.
        // Parse original (lenient) and formatted (strict) → compare SymbolTables.
        topo::DiagnosticEngine diagOrig;
        topo::Lexer lexOrig(source, filepath, diagOrig);
        lexOrig.setLenientMode(true);
        topo::transform::TokenStreamRewriter rwOrig(lexOrig);
        auto tokensOrig = rwOrig.rewrite();
        lexOrig.setTokenBuffer(std::move(tokensOrig));
        topo::Parser parserOrig(lexOrig, diagOrig);
        auto astOrig = parserOrig.parseTopoFile();
        topo::SemanticAnalyzer semaOrig(diagOrig);
        auto symOrig = semaOrig.analyze(static_cast<const topo::TopoFile&>(*astOrig));

        topo::DiagnosticEngine diagFmt;
        topo::Lexer lexFmt(formatted, filepath, diagFmt);
        topo::Parser parserFmt(lexFmt, diagFmt);
        auto astFmt = parserFmt.parseTopoFile();
        topo::SemanticAnalyzer semaFmt(diagFmt);
        auto symFmt = semaFmt.analyze(static_cast<const topo::TopoFile&>(*astFmt));

        topo::transform::SemanticVerifier verifier;
        auto vResult = verifier.verify(symOrig, symFmt);

        if (!vResult.equivalent) {
            std::cerr << "error: semantic verification failed — refusing to write\n";
            for (const auto& diff : vResult.differences) {
                std::cerr << "  " << diff << "\n";
            }
            return 1;
        }

        if (!writeFile(filepath, formatted)) {
            return 1;
        }
        std::cerr << totalRecords << " transform(s) applied\n";
        return 0;
    }

    default: return 1;
    }
}
