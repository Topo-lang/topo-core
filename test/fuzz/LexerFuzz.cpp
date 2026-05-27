// Fuzz target for the Topo Lexer.
// Feeds arbitrary byte sequences and verifies the Lexer never crashes
// or enters an infinite loop.

#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Lexer.h"
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string input(reinterpret_cast<const char*>(data), size);
    topo::DiagnosticEngine diag;
    topo::Lexer lexer(input, "<fuzz>", diag);

    // Consume all tokens until EOF
    for (;;) {
        auto tok = lexer.nextToken();
        if (tok.kind == topo::TokenKind::Eof) break;
    }
    return 0;
}
