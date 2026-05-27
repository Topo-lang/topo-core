// Fuzz target for the Topo SemanticAnalyzer.
// Feeds arbitrary byte sequences through Lexer -> Parser -> Sema pipeline.
// Only runs semantic analysis when parsing succeeds (to exercise Sema with
// structurally valid-ish ASTs from corpus mutations).

#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Sema/SemanticAnalyzer.h"
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string input(reinterpret_cast<const char*>(data), size);

    // Step 1: Parse
    topo::DiagnosticEngine parseDiag;
    topo::Lexer lexer(input, "<fuzz>", parseDiag);
    topo::Parser parser(lexer, parseDiag);
    auto ast = parser.parseTopoFile();

    // Step 2: Only run Sema if we got a valid AST (even with minor errors)
    if (ast) {
        topo::DiagnosticEngine semaDiag;
        topo::SemanticAnalyzer sema(semaDiag);
        auto symbols = sema.analyze(*ast);
        (void)symbols;
    }
    return 0;
}
