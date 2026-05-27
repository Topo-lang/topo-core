// Fuzz target for the Topo Parser.
// Feeds arbitrary byte sequences through Lexer -> Parser and verifies
// the Parser never crashes. Parse errors are expected and silently ignored.

#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string input(reinterpret_cast<const char*>(data), size);
    topo::DiagnosticEngine diag;
    topo::Lexer lexer(input, "<fuzz>", diag);
    topo::Parser parser(lexer, diag);

    // Attempt to parse; errors are expected on random input
    auto ast = parser.parseTopoFile();
    (void)ast;
    return 0;
}
